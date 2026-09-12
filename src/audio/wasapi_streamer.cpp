/*
 * WASAPI High-Resolution Audio Loopback & Render Engine Implementation
 * Ultra-low latency capture & high-precision format conversion
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <math.h>

#include "../../include/wasapi_streamer.h"

struct WASAPI_CAPTURE_ENGINE {
    int target_sample_rate;
    int target_channels;
    int target_bit_depth;

    IMMDeviceEnumerator *pEnumerator;
    IMMDevice *pDevice;
    IAudioClient *pAudioClient;
    IAudioCaptureClient *pCaptureClient;
    WAVEFORMATEX *pwfx;

    HANDLE hEvent;
    HANDLE hStopEvent;
    std::thread worker_thread;
    std::atomic<bool> is_running;

    audio_capture_cb callback;
    void *user_data;

    std::atomic<float> peak_left;
    std::atomic<float> peak_right;
};

struct WASAPI_RENDER_ENGINE {
    int sample_rate;
    int channels;
    int bit_depth;

    IMMDeviceEnumerator *pEnumerator;
    IMMDevice *pDevice;
    IAudioClient *pAudioClient;
    IAudioRenderClient *pRenderClient;
    WAVEFORMATEX *pwfx;

    std::atomic<bool> is_running;
};

static void ResampleLinear(const float *src, int src_samples, int src_rate, float *dst, int dst_samples, int dst_rate, int channels) {
    if (src_rate == dst_rate) {
        memcpy(dst, src, src_samples * channels * sizeof(float));
        return;
    }

    double ratio = (double)src_rate / (double)dst_rate;
    for (int i = 0; i < dst_samples; i++) {
        double src_idx = i * ratio;
        int idx0 = (int)src_idx;
        int idx1 = idx0 + 1;
        if (idx1 >= src_samples) idx1 = src_samples - 1;
        float frac = (float)(src_idx - idx0);

        for (int ch = 0; ch < channels; ch++) {
            float s0 = src[idx0 * channels + ch];
            float s1 = src[idx1 * channels + ch];
            dst[i * channels + ch] = s0 + frac * (s1 - s0);
        }
    }
}

static void CaptureWorker(WASAPI_CAPTURE_ENGINE *cap) {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    UINT32 packetLength = 0;
    std::vector<float> float_buffer;
    std::vector<float> resample_buffer;
    std::vector<int32_t> pcm24_buffer;

    while (cap->is_running) {
        DWORD waitResult = WaitForSingleObject(cap->hEvent, 20);
        if (!cap->is_running) break;

        HRESULT hr = cap->pCaptureClient->GetNextPacketSize(&packetLength);
        while (SUCCEEDED(hr) && packetLength > 0) {
            BYTE *pData = NULL;
            UINT32 numFramesAvailable = 0;
            DWORD flags = 0;

            hr = cap->pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
            if (FAILED(hr)) break;

            if (numFramesAvailable > 0 && pData != NULL) {
                int src_channels = cap->pwfx->nChannels;
                int src_rate = cap->pwfx->nSamplesPerSec;
                float p_l = 0.0f;
                float p_r = 0.0f;

                float_buffer.resize(numFramesAvailable * src_channels);

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    memset(float_buffer.data(), 0, float_buffer.size() * sizeof(float));
                } else if (cap->pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                          (cap->pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                           ((WAVEFORMATEXTENSIBLE*)cap->pwfx)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
                    const float *src_f = (const float*)pData;
                    for (size_t i = 0; i < float_buffer.size(); i++) {
                        float_buffer[i] = src_f[i];
                    }
                } else if (cap->pwfx->wBitsPerSample == 16) {
                    const int16_t *src_16 = (const int16_t*)pData;
                    for (size_t i = 0; i < float_buffer.size(); i++) {
                        float_buffer[i] = (float)src_16[i] / 32768.0f;
                    }
                } else if (cap->pwfx->wBitsPerSample == 24 || cap->pwfx->wBitsPerSample == 32) {
                    const int32_t *src_32 = (const int32_t*)pData;
                    for (size_t i = 0; i < float_buffer.size(); i++) {
                        float_buffer[i] = (float)src_32[i] / 2147483648.0f;
                    }
                }

                /* Calculate peak VU levels */
                for (UINT32 f = 0; f < numFramesAvailable; f++) {
                    float l = fabsf(float_buffer[f * src_channels + 0]);
                    float r = (src_channels > 1) ? fabsf(float_buffer[f * src_channels + 1]) : l;
                    if (l > p_l) p_l = l;
                    if (r > p_r) p_r = r;
                }
                cap->peak_left.store(p_l);
                cap->peak_right.store(p_r);

                /* Resample if required */
                int dst_samples = (int)((double)numFramesAvailable * (double)cap->target_sample_rate / (double)src_rate);
                if (dst_samples > 0) {
                    resample_buffer.resize(dst_samples * cap->target_channels);
                    ResampleLinear(float_buffer.data(), numFramesAvailable, src_rate,
                                   resample_buffer.data(), dst_samples, cap->target_sample_rate, cap->target_channels);

                    /* Convert to 24-bit PCM */
                    pcm24_buffer.resize(dst_samples * cap->target_channels);
                    for (size_t i = 0; i < resample_buffer.size(); i++) {
                        float s = resample_buffer[i];
                        if (s > 1.0f) s = 1.0f;
                        if (s < -1.0f) s = -1.0f;
                        pcm24_buffer[i] = (int32_t)(s * 8388607.0f);
                    }

                    if (cap->callback) {
                        cap->callback(pcm24_buffer.data(), dst_samples, cap->target_channels,
                                      cap->target_sample_rate, 24, cap->user_data);
                    }
                }
            }

            cap->pCaptureClient->ReleaseBuffer(numFramesAvailable);
            hr = cap->pCaptureClient->GetNextPacketSize(&packetLength);
        }
    }

    CoUninitialize();
}

HWASAPI_CAPTURE wasapi_capture_create(int target_sample_rate, int target_channels, int target_bit_depth) {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    HWASAPI_CAPTURE cap = new WASAPI_CAPTURE_ENGINE();
    cap->target_sample_rate = target_sample_rate > 0 ? target_sample_rate : 96000;
    cap->target_channels = target_channels > 0 ? target_channels : 2;
    cap->target_bit_depth = target_bit_depth > 0 ? target_bit_depth : 24;
    cap->is_running = false;
    cap->peak_left = 0.0f;
    cap->peak_right = 0.0f;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&cap->pEnumerator);
    if (FAILED(hr)) {
        delete cap;
        return NULL;
    }

    hr = cap->pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &cap->pDevice);
    if (FAILED(hr)) {
        cap->pEnumerator->Release();
        delete cap;
        return NULL;
    }

    hr = cap->pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&cap->pAudioClient);
    if (FAILED(hr)) {
        cap->pDevice->Release();
        cap->pEnumerator->Release();
        delete cap;
        return NULL;
    }

    hr = cap->pAudioClient->GetMixFormat(&cap->pwfx);
    if (FAILED(hr)) {
        cap->pAudioClient->Release();
        cap->pDevice->Release();
        cap->pEnumerator->Release();
        delete cap;
        return NULL;
    }

    cap->hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    cap->hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    REFERENCE_TIME hnsRequestedDuration = 100000; /* 10 ms buffer */
    hr = cap->pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        hnsRequestedDuration,
        0,
        cap->pwfx,
        NULL
    );

    if (FAILED(hr)) {
        /* Fallback without event callback (polling mode) */
        hr = cap->pAudioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK,
            hnsRequestedDuration,
            0,
            cap->pwfx,
            NULL
        );
    } else {
        cap->pAudioClient->SetEventHandle(cap->hEvent);
    }

    hr = cap->pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&cap->pCaptureClient);
    if (FAILED(hr)) {
        CloseHandle(cap->hEvent);
        CloseHandle(cap->hStopEvent);
        CoTaskMemFree(cap->pwfx);
        cap->pAudioClient->Release();
        cap->pDevice->Release();
        cap->pEnumerator->Release();
        delete cap;
        return NULL;
    }

    return cap;
}

int wasapi_capture_start(HWASAPI_CAPTURE hCap, audio_capture_cb callback, void *user_data) {
    if (!hCap || hCap->is_running) return -1;

    hCap->callback = callback;
    hCap->user_data = user_data;
    hCap->is_running = true;

    HRESULT hr = hCap->pAudioClient->Start();
    if (FAILED(hr)) {
        hCap->is_running = false;
        return -1;
    }

    hCap->worker_thread = std::thread(CaptureWorker, hCap);
    return 0;
}

void wasapi_capture_stop(HWASAPI_CAPTURE hCap) {
    if (!hCap || !hCap->is_running) return;

    hCap->is_running = false;
    SetEvent(hCap->hStopEvent);
    SetEvent(hCap->hEvent);

    if (hCap->worker_thread.joinable()) {
        hCap->worker_thread.join();
    }

    hCap->pAudioClient->Stop();
}

void wasapi_capture_destroy(HWASAPI_CAPTURE hCap) {
    if (!hCap) return;
    wasapi_capture_stop(hCap);

    if (hCap->pCaptureClient) hCap->pCaptureClient->Release();
    if (hCap->pAudioClient) hCap->pAudioClient->Release();
    if (hCap->pDevice) hCap->pDevice->Release();
    if (hCap->pEnumerator) hCap->pEnumerator->Release();
    if (hCap->pwfx) CoTaskMemFree(hCap->pwfx);
    if (hCap->hEvent) CloseHandle(hCap->hEvent);
    if (hCap->hStopEvent) CloseHandle(hCap->hStopEvent);

    delete hCap;
}

int wasapi_capture_get_format(HWASAPI_CAPTURE hCap, int *sample_rate, int *channels, int *bit_depth) {
    if (!hCap || !hCap->pwfx) return -1;
    if (sample_rate) *sample_rate = hCap->pwfx->nSamplesPerSec;
    if (channels) *channels = hCap->pwfx->nChannels;
    if (bit_depth) *bit_depth = hCap->pwfx->wBitsPerSample;
    return 0;
}

void wasapi_capture_get_levels(HWASAPI_CAPTURE hCap, float *peak_left, float *peak_right) {
    if (!hCap) {
        if (peak_left) *peak_left = 0.0f;
        if (peak_right) *peak_right = 0.0f;
        return;
    }
    if (peak_left) *peak_left = hCap->peak_left.load();
    if (peak_right) *peak_right = hCap->peak_right.load();
}

HWASAPI_RENDER wasapi_render_create(int sample_rate, int channels, int bit_depth) {
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    HWASAPI_RENDER ren = new WASAPI_RENDER_ENGINE();
    ren->sample_rate = sample_rate > 0 ? sample_rate : 96000;
    ren->channels = channels > 0 ? channels : 2;
    ren->bit_depth = bit_depth > 0 ? bit_depth : 24;
    ren->is_running = false;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&ren->pEnumerator);
    if (FAILED(hr)) { delete ren; return NULL; }

    hr = ren->pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &ren->pDevice);
    if (FAILED(hr)) { ren->pEnumerator->Release(); delete ren; return NULL; }

    hr = ren->pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&ren->pAudioClient);
    if (FAILED(hr)) { ren->pDevice->Release(); ren->pEnumerator->Release(); delete ren; return NULL; }

    hr = ren->pAudioClient->GetMixFormat(&ren->pwfx);
    if (FAILED(hr)) { ren->pAudioClient->Release(); ren->pDevice->Release(); ren->pEnumerator->Release(); delete ren; return NULL; }

    REFERENCE_TIME hnsDuration = 100000; /* 10 ms */
    hr = ren->pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsDuration, 0, ren->pwfx, NULL);
    if (FAILED(hr)) {
        CoTaskMemFree(ren->pwfx);
        ren->pAudioClient->Release();
        ren->pDevice->Release();
        ren->pEnumerator->Release();
        delete ren;
        return NULL;
    }

    hr = ren->pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&ren->pRenderClient);
    if (FAILED(hr)) {
        CoTaskMemFree(ren->pwfx);
        ren->pAudioClient->Release();
        ren->pDevice->Release();
        ren->pEnumerator->Release();
        delete ren;
        return NULL;
    }

    return ren;
}

int wasapi_render_start(HWASAPI_RENDER hRen) {
    if (!hRen || hRen->is_running) return -1;
    HRESULT hr = hRen->pAudioClient->Start();
    if (SUCCEEDED(hr)) {
        hRen->is_running = true;
        return 0;
    }
    return -1;
}

int wasapi_render_write(HWASAPI_RENDER hRen, const void *p_pcm, int samples_per_ch) {
    if (!hRen || !hRen->is_running || !p_pcm || samples_per_ch <= 0) return -1;

    UINT32 bufferFrameCount = 0;
    UINT32 numPaddingFrames = 0;
    hRen->pAudioClient->GetBufferSize(&bufferFrameCount);
    hRen->pAudioClient->GetCurrentPadding(&numPaddingFrames);

    UINT32 numFramesAvailable = bufferFrameCount - numPaddingFrames;
    if (numFramesAvailable < (UINT32)samples_per_ch) {
        samples_per_ch = (int)numFramesAvailable;
    }
    if (samples_per_ch <= 0) return 0;

    BYTE *pData = NULL;
    HRESULT hr = hRen->pRenderClient->GetBuffer(samples_per_ch, &pData);
    if (FAILED(hr)) return -1;

    /* Copy audio data according to mix format */
    int bytes_per_frame = hRen->pwfx->nBlockAlign;
    memcpy(pData, p_pcm, samples_per_ch * bytes_per_frame);

    hRen->pRenderClient->ReleaseBuffer(samples_per_ch, 0);
    return samples_per_ch;
}

void wasapi_render_stop(HWASAPI_RENDER hRen) {
    if (!hRen || !hRen->is_running) return;
    hRen->is_running = false;
    hRen->pAudioClient->Stop();
}

void wasapi_render_destroy(HWASAPI_RENDER hRen) {
    if (!hRen) return;
    wasapi_render_stop(hRen);
    if (hRen->pRenderClient) hRen->pRenderClient->Release();
    if (hRen->pAudioClient) hRen->pAudioClient->Release();
    if (hRen->pDevice) hRen->pDevice->Release();
    if (hRen->pEnumerator) hRen->pEnumerator->Release();
    if (hRen->pwfx) CoTaskMemFree(hRen->pwfx);
    delete hRen;
}
