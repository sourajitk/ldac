#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <vector>
#include <string>
#include <iostream>

#pragma pack(push, 1)
struct WavHeader {
    char     riff[4];        /* "RIFF" */
    uint32_t fileSize;
    char     wave[4];        /* "WAVE" */
    char     fmt[4];         /* "fmt " */
    uint32_t fmtSize;
    uint16_t audioFormat;    /* 1 = PCM */
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char     data[4];        /* "data" */
    uint32_t dataSize;
};
#pragma pack(pop)

static bool WriteWav24(const std::string& path, int sample_rate, int channels, const std::vector<int32_t>& samples) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    uint32_t num_samples_total = (uint32_t)samples.size();
    uint32_t data_bytes = num_samples_total * 3;

    WavHeader hdr = {0};
    memcpy(hdr.riff, "RIFF", 4);
    hdr.fileSize = sizeof(WavHeader) - 8 + data_bytes;
    memcpy(hdr.wave, "WAVE", 4);
    memcpy(hdr.fmt, "fmt ", 4);
    hdr.fmtSize = 16;
    hdr.audioFormat = 1;
    hdr.numChannels = (uint16_t)channels;
    hdr.sampleRate = (uint32_t)sample_rate;
    hdr.bitsPerSample = 24;
    hdr.byteRate = sample_rate * channels * 3;
    hdr.blockAlign = channels * 3;
    memcpy(hdr.data, "data", 4);
    hdr.dataSize = data_bytes;

    fwrite(&hdr, 1, sizeof(hdr), f);

    /* Write 24-bit little endian samples */
    for (size_t i = 0; i < samples.size(); i++) {
        int32_t s = samples[i];
        if (s > 8388607) s = 8388607;
        if (s < -8388608) s = -8388608;
        uint8_t b[3];
        b[0] = (uint8_t)(s & 0xFF);
        b[1] = (uint8_t)((s >> 8) & 0xFF);
        b[2] = (uint8_t)((s >> 16) & 0xFF);
        fwrite(b, 1, 3, f);
    }

    fclose(f);
    return true;
}

int main() {
    std::cout << "Generating High-Resolution (96kHz / 24-bit) LDAC Audio Test Samples...\n";

    const int sample_rate = 96000;
    const int channels = 2;
    const double duration = 5.0; /* 5 seconds */
    const size_t total_samples = (size_t)(sample_rate * duration);

    /* 1. Logarithmic Frequency Sweep (20 Hz -> 42,000 Hz) */
    {
        std::vector<int32_t> sweep(total_samples * channels);
        double f_start = 20.0;
        double f_end = 42000.0;
        double amplitude = 8388607.0 * 0.7; /* -3 dBFS */

        for (size_t i = 0; i < total_samples; i++) {
            double t = (double)i / sample_rate;
            /* Logarithmic chirp */
            double freq = f_start * pow(f_end / f_start, t / duration);
            double phase = 2.0 * 3.14159265358979323846 * f_start * ((pow(f_end / f_start, t / duration) - 1.0) / log(f_end / f_start)) * duration;
            int32_t val = (int32_t)(amplitude * sin(phase));

            sweep[i * 2 + 0] = val; /* Left */
            sweep[i * 2 + 1] = val; /* Right */
        }

        std::string path = "samples/sample_sweep_20Hz_to_42kHz_96k24b.wav";
        if (WriteWav24(path, sample_rate, channels, sweep)) {
            std::cout << "  [✓] Created: " << path << " (96 kHz / 24-bit Ultrasonic Sweep)\n";
        }
    }

    /* 2. Dual-Tone Acoustic Fidelity Test (1,000 Hz Left, 12,000 Hz Right + 32,000 Hz ultrasonic harmonic) */
    {
        std::vector<int32_t> dual(total_samples * channels);
        double amplitude = 8388607.0 * 0.6;

        for (size_t i = 0; i < total_samples; i++) {
            double t = (double)i / sample_rate;
            double left = sin(2.0 * 3.14159265358979323846 * 1000.0 * t);
            /* Right channel combines 12 kHz audible tone with 32 kHz ultrasonic high-res tone */
            double right = 0.7 * sin(2.0 * 3.14159265358979323846 * 12000.0 * t) +
                           0.3 * sin(2.0 * 3.14159265358979323846 * 32000.0 * t);

            dual[i * 2 + 0] = (int32_t)(amplitude * left);
            dual[i * 2 + 1] = (int32_t)(amplitude * right);
        }

        std::string path = "samples/sample_dualtone_96k24b.wav";
        if (WriteWav24(path, sample_rate, channels, dual)) {
            std::cout << "  [✓] Created: " << path << " (96 kHz / 24-bit Dual-Tone Harmonic)\n";
        }
    }

    std::cout << "\nTest sample files ready in .\\samples\\!\n";
    return 0;
}
