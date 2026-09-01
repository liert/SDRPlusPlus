#pragma once
#include <stdint.h>
#include <string.h>
#include <fstream>
#include <string>
#include <algorithm>
#include <filesystem>

#define WAV_SIGNATURE       "RIFF"
#define WAV_TYPE            "WAVE"
#define WAV_FORMAT_MARK     "fmt "
#define WAV_DATA_MARK       "data"
#define WAV_SAMPLE_TYPE_PCM 1

enum FileFormatType {
    FORMAT_AUTO = 0,
    FORMAT_WAV,
    FORMAT_RAW_INT8,     // HackRF signed int8
    FORMAT_RAW_INT16,    // 16-bit signed int
    FORMAT_RAW_FLOAT32   // 32-bit float IQ
};

class WavReader {
public:
    WavReader(std::string path, uint32_t fallbackSampleRate = 8000000) {
        _sampleRate = fallbackSampleRate;
#if defined(_WIN32)
        file.open(std::filesystem::u8path(path), std::ios::binary);
#else
        file.open(path.c_str(), std::ios::binary);
#endif
        if (!file.is_open()) {
            valid = false;
            return;
        }

        // Try reading WAV header
        file.read((char*)&hdr, sizeof(WavHeader_t));
        if (file.gcount() >= 12 && memcmp(hdr.signature, "RIFF", 4) == 0 && memcmp(hdr.fileType, "WAVE", 4) == 0) {
            isWav = true;
            _sampleRate = hdr.sampleRate ? hdr.sampleRate : fallbackSampleRate;
            dataOffset = sizeof(WavHeader_t);
            valid = true;
        } else {
            // Raw binary IQ file (e.g. HackRF .iq)
            isWav = false;
            dataOffset = 0;
            file.clear();
            file.seekg(0, std::ios::beg);
            valid = true;
        }
    }

    uint32_t getSampleRate() const {
        return _sampleRate;
    }

    void setSampleRate(uint32_t sr) {
        if (sr > 0) _sampleRate = sr;
    }

    bool isValid() const {
        return valid;
    }

    bool isWavFile() const {
        return isWav;
    }

    uint16_t getBitDepth() const {
        return isWav ? hdr.bitDepth : 16;
    }

    uint16_t getChannelCount() const {
        return isWav ? hdr.channelCount : 2;
    }

    size_t readSamples(void* data, size_t size, bool loop = true) {
        if (!file.is_open()) return 0;
        char* _data = (char*)data;
        file.read(_data, size);
        size_t read = (size_t)file.gcount();
        if (read < size) {
            if (loop) {
                // Loop playback from beginning of data
                file.clear();
                file.seekg(dataOffset, std::ios::beg);
                file.read(&_data[read], size - read);
                read += (size_t)file.gcount();
            } else {
                // Not looping: fill remainder with zero
                memset(&_data[read], 0, size - read);
            }
        }
        bytesRead += read;
        return read;
    }

    void rewind() {
        if (!file.is_open()) return;
        file.clear();
        file.seekg(dataOffset, std::ios::beg);
        bytesRead = 0;
    }

    void close() {
        if (file.is_open()) {
            file.close();
        }
    }

private:
    struct WavHeader_t {
        char signature[4];           // "RIFF"
        uint32_t fileSize;           // data bytes + sizeof(WavHeader_t) - 8
        char fileType[4];            // "WAVE"
        char formatMarker[4];        // "fmt "
        uint32_t formatHeaderLength; // Always 16
        uint16_t sampleType;         // PCM (1)
        uint16_t channelCount;
        uint32_t sampleRate;
        uint32_t bytesPerSecond;
        uint16_t bytesPerSample;
        uint16_t bitDepth;
        char dataMarker[4];          // "data"
        uint32_t dataSize;
    };

    WavHeader_t hdr;
    bool valid = false;
    bool isWav = false;
    uint32_t _sampleRate = 8000000;
    std::streamoff dataOffset = 0;
    std::ifstream file;
    size_t bytesRead = 0;
};
