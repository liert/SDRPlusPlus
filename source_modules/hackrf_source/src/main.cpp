#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>
#include <core.h>
#include <gui/style.h>
#include <config.h>
#include <gui/widgets/stepped_slider.h>
#include <gui/smgui.h>

#ifndef __ANDROID__
#include <libhackrf/hackrf.h>
#else
#include <android_backend.h>
#include <hackrf.h>
#endif

#define CONCAT(a, b) ((std::string(a) + b).c_str())

SDRPP_MOD_INFO{
    /* Name:            */ "hackrf_source",
    /* Description:     */ "HackRF source module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

const char* AGG_MODES_STR = "Off\0Low\0High\0";

const char* sampleRatesTxt = "20MHz\00016MHz\00010MHz\0008MHz\0005MHz\0004MHz\0002MHz\000";

const int sampleRates[] = {
    20000000,
    16000000,
    10000000,
    8000000,
    5000000,
    4000000,
    2000000,
};

const int bandwidths[] = {
    1750000,
    2500000,
    3500000,
    5000000,
    5500000,
    6000000,
    7000000,
    8000000,
    9000000,
    10000000,
    12000000,
    14000000,
    15000000,
    20000000,
    24000000,
    28000000,
};

const char* bandwidthsTxt = "1.75MHz\0"
                            "2.5MHz\0"
                            "3.5MHz\0"
                            "5MHz\0"
                            "5.5MHz\0"
                            "6MHz\0"
                            "7MHz\0"
                            "8MHz\0"
                            "9MHz\0"
                            "10MHz\0"
                            "12MHz\0"
                            "14MHz\0"
                            "15MHz\0"
                            "20MHz\0"
                            "24MHz\0"
                            "28MHz\0"
                            "Auto\0";

class HackRFSourceModule : public ModuleManager::Instance {
public:
    inline static HackRFSourceModule* instance = nullptr;

    HackRFSourceModule(std::string name) {
        this->name = name;
        instance = this;

        hackrf_init();

        sampleRate = 8000000;
        srId = 2; // 8 MSPS

        lna = 32;
        vga = 20;
        amp = false;
        biasT = false;

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        refresh();

        config.acquire();
        std::string confSerial = config.conf["device"];
        config.release();
        selectBySerial(confSerial);

        sigpath::sourceManager.registerSource("HackRF", &handler);
    }

    ~HackRFSourceModule() {
        stop(this);
        hackrf_exit();
        sigpath::sourceManager.unregisterSource("HackRF");
    }

    void postInit() {}

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }

    void refresh() {
        devList.clear();
        devListTxt = "";

#ifndef __ANDROID__
        uint64_t serials[256];
        hackrf_device_list_t* _devList = hackrf_device_list();

        for (int i = 0; i < _devList->devicecount; i++) {
            // Skip devices that are in use
            if (_devList->serial_numbers[i] == NULL) { continue; }

            // Save the device serial number
            devList.push_back(_devList->serial_numbers[i]);
            devListTxt += (char*)(_devList->serial_numbers[i] + 16);
            devListTxt += '\0';
        }

        hackrf_device_list_free(_devList);
#else
        int vid, pid;
        devFd = backend::getDeviceFD(vid, pid, backend::HACKRF_VIDPIDS);
        if (devFd < 0) { return; }
        std::string fakeName = "HackRF USB";
        devList.push_back("fake_serial");
        devListTxt += fakeName;
        devListTxt += '\0';
#endif
    }

    void selectFirst() {
        if (devList.size() != 0) {
            selectBySerial(devList[0]);
            return;
        }
        selectedSerial = "";
    }

    void selectBySerial(std::string serial) {
        if (std::find(devList.begin(), devList.end(), serial) == devList.end()) {
            selectFirst();
            return;
        }

        bool created = false;
        config.acquire();
        if (!config.conf["devices"].contains(serial)) {
            config.conf["devices"][serial]["sampleRate"] = 2000000;
            config.conf["devices"][serial]["biasT"] = false;
            config.conf["devices"][serial]["amp"] = false;
            config.conf["devices"][serial]["lnaGain"] = 0;
            config.conf["devices"][serial]["vgaGain"] = 0;
            config.conf["devices"][serial]["bandwidth"] = 16;
        }
        config.release(created);

        // Set default values
        srId = 0;
        sampleRate = 2000000;
        biasT = false;
        amp = false;
        lna = 0;
        vga = 0;
        bwId = 16;

        // Load from config if available and validate
        if (config.conf["devices"][serial].contains("sampleRate")) {
            int psr = config.conf["devices"][serial]["sampleRate"];
            for (int i = 0; i < 7; i++) {
                if (sampleRates[i] == psr) {
                    sampleRate = psr;
                    srId = i;
                }
            }
        }
        if (config.conf["devices"][serial].contains("biasT")) {
            biasT = config.conf["devices"][serial]["biasT"];
        }
        if (config.conf["devices"][serial].contains("amp")) {
            amp = config.conf["devices"][serial]["amp"];
        }
        if (config.conf["devices"][serial].contains("lnaGain")) {
            lna = config.conf["devices"][serial]["lnaGain"];
        }
        if (config.conf["devices"][serial].contains("vgaGain")) {
            vga = config.conf["devices"][serial]["vgaGain"];
        }
        if (config.conf["devices"][serial].contains("bandwidth")) {
            bwId = config.conf["devices"][serial]["bandwidth"];
            bwId = std::clamp<int>(bwId, 0, 16);
        }

        selectedSerial = serial;
    }

private:
    static void menuSelected(void* ctx) {
        HackRFSourceModule* _this = (HackRFSourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        flog::info("HackRFSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        HackRFSourceModule* _this = (HackRFSourceModule*)ctx;
        flog::info("HackRFSourceModule '{0}': Menu Deselect!", _this->name);
    }

    int bandwidthIdToBw(int id) {
        if (id == 16) { return hackrf_compute_baseband_filter_bw(sampleRate); }
        return bandwidths[id];
    }

    static void start(void* ctx) {
        HackRFSourceModule* _this = (HackRFSourceModule*)ctx;
        if (_this->running) { return; }

        std::lock_guard<std::mutex> lock(_this->devMtx);

        _this->refresh();
        if (_this->devList.empty()) {
            flog::error("Tried to start HackRF but no HackRF devices found on USB!");
            return;
        }

        hackrf_error err = HACKRF_ERROR_NOT_FOUND;
        if (!_this->selectedSerial.empty()) {
            err = (hackrf_error)hackrf_open_by_serial(_this->selectedSerial.c_str(), &_this->openDev);
        }
        if (err != HACKRF_SUCCESS) {
            // Fallback: open any available HackRF
            err = (hackrf_error)hackrf_open(&_this->openDev);
            if (err == HACKRF_SUCCESS && !_this->devList.empty()) {
                _this->selectedSerial = _this->devList[0];
            }
        }

        if (err != HACKRF_SUCCESS) {
            flog::error("Could not open HackRF {0}: {1}", _this->selectedSerial, hackrf_error_name(err));
            return;
        }

        hackrf_set_sample_rate(_this->openDev, _this->sampleRate);
        hackrf_set_baseband_filter_bandwidth(_this->openDev, _this->bandwidthIdToBw(_this->bwId));
        hackrf_set_freq(_this->openDev, _this->freq);

        hackrf_set_antenna_enable(_this->openDev, _this->biasT ? 1 : 0);
        hackrf_set_amp_enable(_this->openDev, _this->amp ? 1 : 0);
        hackrf_set_lna_gain(_this->openDev, (uint32_t)_this->lna);
        hackrf_set_vga_gain(_this->openDev, (uint32_t)_this->vga);

        err = (hackrf_error)hackrf_start_rx(_this->openDev, callback, _this);
        if (err != HACKRF_SUCCESS) {
            flog::error("Could not start HackRF RX stream: {0}", hackrf_error_name(err));
            hackrf_close(_this->openDev);
            _this->openDev = nullptr;
            return;
        }

        _this->running = true;
        flog::info("HackRFSourceModule '{0}': Start RX streaming successfully!", _this->name);
    }

    static void stop(void* ctx) {
        HackRFSourceModule* _this = (HackRFSourceModule*)ctx;
        std::lock_guard<std::mutex> lock(_this->devMtx);
        if (!_this->running && !_this->openDev) { return; }
        _this->running = false;
        _this->stream.stopWriter();

        if (_this->openDev) {
            hackrf_stop_rx(_this->openDev);
            hackrf_error err = (hackrf_error)hackrf_close(_this->openDev);
            if (err != HACKRF_SUCCESS) {
                flog::error("Could not close HackRF {0}: {1}", _this->selectedSerial, hackrf_error_name(err));
            }
            _this->openDev = nullptr;
        }
        _this->stream.clearWriteStop();
        flog::info("HackRFSourceModule '{0}': Stop and released USB device!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        HackRFSourceModule* _this = (HackRFSourceModule*)ctx;
        std::lock_guard<std::mutex> lock(_this->devMtx);
        if (_this->running && _this->openDev) {
            hackrf_set_freq(_this->openDev, freq);
        }
        _this->freq = freq;
        flog::info("HackRFSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        HackRFSourceModule* _this = (HackRFSourceModule*)ctx;

        if (_this->running) { SmGui::BeginDisabled(); }
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Combo(CONCAT("##_hackrf_dev_sel_", _this->name), &_this->devId, _this->devListTxt.c_str())) {
            _this->selectBySerial(_this->devList[_this->devId]);
            core::setInputSampleRate(_this->sampleRate);
            config.acquire();
            config.conf["device"] = _this->selectedSerial;
            config.release(true);
        }

        if (SmGui::Combo(CONCAT("##_hackrf_sr_sel_", _this->name), &_this->srId, sampleRatesTxt)) {
            _this->sampleRate = sampleRates[_this->srId];
            core::setInputSampleRate(_this->sampleRate);
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["sampleRate"] = _this->sampleRate;
            config.release(true);
        }

        SmGui::SameLine();
        SmGui::FillWidth();
        SmGui::ForceSync();
        if (SmGui::Button(CONCAT("Refresh##_hackrf_refr_", _this->name))) {
            _this->refresh();
            _this->selectBySerial(_this->selectedSerial);
            core::setInputSampleRate(_this->sampleRate);
        }

        if (_this->running) { SmGui::EndDisabled(); }

        SmGui::LeftLabel("Bandwidth");
        SmGui::FillWidth();
        if (SmGui::Combo(CONCAT("##_hackrf_bw_sel_", _this->name), &_this->bwId, bandwidthsTxt)) {
            if (_this->running) {
                hackrf_set_baseband_filter_bandwidth(_this->openDev, _this->bandwidthIdToBw(_this->bwId));
            }
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["bandwidth"] = _this->bwId;
            config.release(true);
        }

        SmGui::LeftLabel("LNA Gain");
        SmGui::FillWidth();
        if (SmGui::SliderFloatWithSteps(CONCAT("##_hackrf_lna_", _this->name), &_this->lna, 0, 40, 8, SmGui::FMT_STR_FLOAT_DB_NO_DECIMAL)) {
            if (_this->running) {
                hackrf_set_lna_gain(_this->openDev, _this->lna);
            }
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["lnaGain"] = (int)_this->lna;
            config.release(true);
        }

        SmGui::LeftLabel("VGA Gain");
        SmGui::FillWidth();
        if (SmGui::SliderFloatWithSteps(CONCAT("##_hackrf_vga_", _this->name), &_this->vga, 0, 62, 2, SmGui::FMT_STR_FLOAT_DB_NO_DECIMAL)) {
            if (_this->running) {
                hackrf_set_vga_gain(_this->openDev, _this->vga);
            }
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["vgaGain"] = (int)_this->vga;
            config.release(true);
        }

        if (SmGui::Checkbox(CONCAT("Bias-T##_hackrf_bt_", _this->name), &_this->biasT)) {
            if (_this->running) {
                hackrf_set_antenna_enable(_this->openDev, _this->biasT);
            }
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["biasT"] = _this->biasT;
            config.release(true);
        }

        if (SmGui::Checkbox(CONCAT("Amp Enabled##_hackrf_amp_", _this->name), &_this->amp)) {
            if (_this->running) {
                hackrf_set_amp_enable(_this->openDev, _this->amp);
            }
            config.acquire();
            config.conf["devices"][_this->selectedSerial]["amp"] = _this->amp;
            config.release(true);
        }
    }

public:
    static void applyAllGains(float lnaVal, float vgaVal, bool ampVal, bool biasTVal) {
        if (!instance) return;
        std::lock_guard<std::mutex> lock(instance->devMtx);
        instance->lna = lnaVal;
        instance->vga = vgaVal;
        instance->amp = ampVal;
        instance->biasT = biasTVal;
        if (instance->running && instance->openDev) {
            hackrf_set_lna_gain(instance->openDev, (uint32_t)lnaVal);
            hackrf_set_vga_gain(instance->openDev, (uint32_t)vgaVal);
            hackrf_set_amp_enable(instance->openDev, ampVal ? 1 : 0);
            hackrf_set_antenna_enable(instance->openDev, biasTVal ? 1 : 0);
        }
    }
    static void setLnaGain(float gain) {
        if (!instance) return;
        std::lock_guard<std::mutex> lock(instance->devMtx);
        instance->lna = gain;
        if (instance->running && instance->openDev) {
            hackrf_set_lna_gain(instance->openDev, (uint32_t)gain);
        }
    }
    static void setVgaGain(float gain) {
        if (!instance) return;
        std::lock_guard<std::mutex> lock(instance->devMtx);
        instance->vga = gain;
        if (instance->running && instance->openDev) {
            hackrf_set_vga_gain(instance->openDev, (uint32_t)gain);
        }
    }
    static void setAmp(bool amp) {
        if (!instance) return;
        std::lock_guard<std::mutex> lock(instance->devMtx);
        instance->amp = amp;
        if (instance->running && instance->openDev) {
            hackrf_set_amp_enable(instance->openDev, amp ? 1 : 0);
        }
    }
    static void setBiasT(bool biasT) {
        if (!instance) return;
        std::lock_guard<std::mutex> lock(instance->devMtx);
        instance->biasT = biasT;
        if (instance->running && instance->openDev) {
            hackrf_set_antenna_enable(instance->openDev, biasT ? 1 : 0);
        }
    }
    static void setSampleRateValue(int sr) {
        if (!instance) return;
        std::lock_guard<std::mutex> lock(instance->devMtx);
        instance->sampleRate = sr;
        if (instance->running && instance->openDev) {
            hackrf_set_sample_rate(instance->openDev, sr);
            hackrf_set_baseband_filter_bandwidth(instance->openDev, hackrf_compute_baseband_filter_bw(sr * 0.75));
        }
    }

private:
    static int callback(hackrf_transfer* transfer) {
        HackRFSourceModule* _this = (HackRFSourceModule*)transfer->rx_ctx;
        if (!_this || !_this->running) return -1;
        volk_8i_s32f_convert_32f((float*)_this->stream.writeBuf, (int8_t*)transfer->buffer, 128.0f, transfer->valid_length);
        if (!_this->stream.swap(transfer->valid_length / 2)) {
            if (!_this->running) return -1;
            return 0;
        }
        return 0;
    }

    std::string name;
    hackrf_device* openDev;
    bool enabled = true;
    dsp::stream<dsp::complex_t> stream;
    int sampleRate;
    SourceManager::SourceHandler handler;
    bool running = false;
    double freq;
    std::string selectedSerial = "";
    int devId = 0;
    int srId = 0;
    int bwId = 16;
    bool biasT = false;
    bool amp = false;
    float lna = 0;
    float vga = 0;

#ifdef __ANDROID__
    int devFd = -1;
#endif

    std::vector<std::string> devList;
    std::string devListTxt;
    std::mutex devMtx;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["devices"] = json({});
    def["device"] = "";
    config.setPath(core::args["root"].s() + "/hackrf_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new HackRFSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(ModuleManager::Instance* instance) {
    delete (HackRFSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}

extern "C" {
    MOD_EXPORT void hackrf_apply_gain(float lna, float vga, bool amp, bool biasT) {
        HackRFSourceModule::applyAllGains(lna, vga, amp, biasT);
    }
}
