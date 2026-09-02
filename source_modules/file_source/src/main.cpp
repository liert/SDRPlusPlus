#define NOMINMAX
#include <imgui.h>
#include <utils/flog.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <gui/file_dialogs.h>
#include <signal_path/signal_path.h>
#include <wavreader.h>
#include <core.h>
#include <gui/widgets/file_select.h>
#include <filesystem>
#include <regex>
#include <gui/tuner.h>
#include <algorithm>
#include <stdexcept>
#include <vector>

#define CONCAT(a, b) ((std::string(a) + b).c_str())

static inline std::filesystem::path toFsPath(const std::string& utf8Str) {
#if defined(_WIN32)
    return std::filesystem::u8path(utf8Str);
#else
    return std::filesystem::path(utf8Str);
#endif
}

SDRPP_MOD_INFO{
    /* Name:            */ "file_source",
    /* Description:     */ "Universal WAV and Raw IQ file source module for SDR++",
    /* Author:          */ "Ryzerth & Refactored",
    /* Version:         */ 1, 0, 0,
    /* Max instances    */ 1
};

ConfigManager config;

class FileSourceModule : public ModuleManager::Instance {
public:
    inline static FileSourceModule* instance = nullptr;

    FileSourceModule(std::string name) : fileSelect("", { "IQ & WAV Files", "*.wav *.iq *.bin *.raw", "All Files", "*" }) {
        this->name = name;
        instance = this;

        config.acquire();
        std::string savedPath = config.conf.contains("path") ? (std::string)config.conf["path"] : "";
        loopMode = config.conf.contains("loop") ? (bool)config.conf["loop"] : true;
        fileSelect.setPath(savedPath, true);
        config.release();

        if (!savedPath.empty()) {
            try {
                if (std::filesystem::is_regular_file(toFsPath(savedPath))) {
                    loadFile(savedPath);
                }
            } catch (...) {}
        }

        handler.ctx = this;
        handler.selectHandler = menuSelected;
        handler.deselectHandler = menuDeselected;
        handler.menuHandler = menuHandler;
        handler.startHandler = start;
        handler.stopHandler = stop;
        handler.tuneHandler = tune;
        handler.stream = &stream;

        sigpath::sourceManager.registerSource("File Source", &handler);
        sigpath::sourceManager.registerSource("File", &handler);
        flog::info("FileSourceModule registered: 'File Source' and 'File'");
    }

    ~FileSourceModule() {
        stop(this);
        sigpath::sourceManager.unregisterSource("File Source");
        sigpath::sourceManager.unregisterSource("File");
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

    void loadFile(const std::string& path) {
        if (reader != NULL) {
            reader->close();
            delete reader;
            reader = NULL;
        }
        try {
            reader = new WavReader(path, (uint32_t)sampleRate);
            if (!reader->isValid()) {
                delete reader;
                reader = NULL;
                flog::error("Failed to open file: {0}", path);
                return;
            }
            if (reader->isWavFile()) {
                sampleRate = reader->getSampleRate();
                formatType = FORMAT_WAV;
            } else {
                std::string ext = toFsPath(path).extension().string();
                if (ext == ".iq" || ext == ".raw") {
                    formatType = FORMAT_RAW_INT8;
                }
            }
            core::setInputSampleRate(sampleRate);
            std::string filename = toFsPath(path).filename().string();
            centerFreq = getFrequency(filename);
            currentFilePath = path;
            fileSelect.setPath(path, false);
            flog::info("FileSource loaded: {0} (SR: {1} Hz, Freq: {2} Hz)", path, sampleRate, centerFreq);
        }
        catch (const std::exception& e) {
            flog::error("Error loading file: {0}", e.what());
        }
    }

    void openFileDialog() {
        std::string initialDir = "";
        try {
            if (!currentFilePath.empty()) {
                initialDir = toFsPath(currentFilePath).parent_path().string();
            }
        } catch (...) {}

        auto file = pfd::open_file("Open IQ or WAV File", initialDir, { "IQ & WAV Files", "*.wav *.iq *.bin *.raw", "All Files", "*" });
        std::vector<std::string> res = file.result();
        if (!res.empty() && !res[0].empty()) {
            loadFile(res[0]);
            config.acquire();
            config.conf["path"] = res[0];
            config.conf["loop"] = loopMode;
            config.release(true);
        }
    }

private:
    static void menuSelected(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        core::setInputSampleRate(_this->sampleRate);
        flog::info("FileSourceModule '{0}': Menu Select!", _this->name);
    }

    static void menuDeselected(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        flog::info("FileSourceModule '{0}': Menu Deselect!", _this->name);
    }

    static void start(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        if (_this->running) { return; }

        if (_this->reader == NULL) {
            // Try auto-discovering sample IQ file
            std::vector<std::string> candidates = {
                "fresh_pairing_2400_8.iq",
                "../fresh_pairing_2400_8.iq",
                "webui/public/fresh_pairing_2400_8.iq"
            };
            for (const auto& c : candidates) {
                if (std::filesystem::is_regular_file(toFsPath(c))) {
                    _this->loadFile(c);
                    break;
                }
            }
        }

        if (_this->reader == NULL) {
            flog::warn("FileSource: Cannot start, no IQ file loaded!");
            return;
        }

        _this->running = true;
        _this->workerThread = std::thread(worker, _this);
        flog::info("FileSourceModule '{0}': Start streaming IQ file!", _this->name);
    }

    static void stop(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        if (!_this->running) { return; }
        _this->running = false;
        _this->stream.stopWriter();
        if (_this->workerThread.joinable()) {
            _this->workerThread.join();
        }
        _this->stream.clearWriteStop();
        if (_this->reader) {
            _this->reader->rewind();
        }
        flog::info("FileSourceModule '{0}': Stop!", _this->name);
    }

    static void tune(double freq, void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        _this->centerFreq = freq;
        flog::info("FileSourceModule '{0}': Tune: {1}!", _this->name, freq);
    }

    static void menuHandler(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;

        // Big Browse File Button
        if (ImGui::Button("📂 浏览选择文件 (Browse File)...", ImVec2(-1, 26))) {
            _this->openFileDialog();
        }

        // File Path Text Input
        if (_this->fileSelect.render("##file_source_" + _this->name)) {
            _this->loadFile(_this->fileSelect.path);
            config.acquire();
            config.conf["path"] = _this->fileSelect.path;
            config.conf["loop"] = _this->loopMode;
            config.release(true);
        }

        // Status indicator
        if (_this->reader != NULL && _this->reader->isValid()) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "状态: 就绪 (%s)",
                               _this->running ? "正在播放" : "已停止");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "状态: 请点击上方按钮选择文件");
        }

        ImGui::Spacing();
        if (ImGui::Checkbox("循环回放 (Loop Playback)", &_this->loopMode)) {
            config.acquire();
            config.conf["loop"] = _this->loopMode;
            config.release(true);
        }

        ImGui::Spacing();
        const char* formatNames[] = { "自动 (Auto/WAV)", "标准 WAV (RIFF)", "Raw Int8 (HackRF 8位)", "Raw Int16 (16位PCM)", "Raw Float32" };
        ImGui::LeftLabel("数据编码");
        ImGui::FillWidth();
        ImGui::Combo("##file_format", (int*)&_this->formatType, formatNames, IM_ARRAYSIZE(formatNames));

        float srM = (float)(_this->sampleRate / 1000000.0);
        ImGui::LeftLabel("采样率 (MSPS)");
        ImGui::FillWidth();
        if (ImGui::InputFloat("##file_sr", &srM, 1.0f, 2.0f, "%.3f")) {
            if (srM > 0.001f) {
                _this->sampleRate = (double)srM * 1000000.0;
                if (_this->reader) _this->reader->setSampleRate((uint32_t)_this->sampleRate);
                core::setInputSampleRate(_this->sampleRate);
            }
        }
    }

    static void worker(void* ctx) {
        FileSourceModule* _this = (FileSourceModule*)ctx;
        double sampleRate = (std::max)(_this->sampleRate, 1000.0);
        int blockSize = (std::min)((int)(sampleRate / 100.0f), (int)STREAM_BUFFER_SIZE);

        if (_this->formatType == FORMAT_RAW_INT8) {
            std::vector<int8_t> inBuf(blockSize * 2);
            while (_this->running) {
                size_t read = _this->reader->readSamples(inBuf.data(), blockSize * 2 * sizeof(int8_t), _this->loopMode);
                for (int i = 0; i < blockSize; i++) {
                    _this->stream.writeBuf[i] = dsp::complex_t{
                        (float)inBuf[2 * i] / 128.0f,
                        (float)inBuf[2 * i + 1] / 128.0f
                    };
                }
                if (!_this->stream.swap(blockSize)) { break; }
                if (!_this->loopMode && read < blockSize * 2 * sizeof(int8_t)) {
                    _this->running = false;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(9500)); // ~100 FPS paced
            }
        } else if (_this->formatType == FORMAT_RAW_FLOAT32) {
            while (_this->running) {
                size_t read = _this->reader->readSamples(_this->stream.writeBuf, blockSize * sizeof(dsp::complex_t), _this->loopMode);
                if (!_this->stream.swap(blockSize)) { break; }
                if (!_this->loopMode && read < blockSize * sizeof(dsp::complex_t)) {
                    _this->running = false;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(9500));
            }
        } else {
            // Standard 16-bit WAV / Int16
            std::vector<int16_t> inBuf(blockSize * 2);
            while (_this->running) {
                size_t read = _this->reader->readSamples(inBuf.data(), blockSize * 2 * sizeof(int16_t), _this->loopMode);
                volk_16i_s32f_convert_32f((float*)_this->stream.writeBuf, inBuf.data(), 32768.0f, blockSize * 2);
                if (!_this->stream.swap(blockSize)) { break; }
                if (!_this->loopMode && read < blockSize * 2 * sizeof(int16_t)) {
                    _this->running = false;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(9500));
            }
        }
    }

    double getFrequency(std::string filename) {
        std::regex expr("[0-9]+Hz");
        std::smatch matches;
        std::regex_search(filename, matches, expr);
        if (matches.empty()) { return 2400000000.0; } // Default 2.4 GHz for H12
        std::string freqStr = matches[0].str();
        return std::atof(freqStr.substr(0, freqStr.size() - 2).c_str());
    }

public:
    FileSelect fileSelect;
    std::string name;
    dsp::stream<dsp::complex_t> stream;
    SourceManager::SourceHandler handler;
    WavReader* reader = NULL;
    bool running = false;
    bool enabled = true;
    double sampleRate = 8000000.0;
    std::thread workerThread;

    double centerFreq = 2400000000.0;
    FileFormatType formatType = FORMAT_RAW_INT8;
    std::string currentFilePath = "";
    bool loopMode = true;
};

MOD_EXPORT void _INIT_() {
    json def = json({});
    def["path"] = "";
    def["loop"] = true;
    config.setPath(core::args["root"].s() + "/file_source_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    return new FileSourceModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (FileSourceModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}

extern "C" {
    MOD_EXPORT void file_source_set_path(const char* path, bool loop) {
        if (FileSourceModule::instance && path) {
            FileSourceModule::instance->loadFile(path);
            FileSourceModule::instance->loopMode = loop;
        }
    }
}
