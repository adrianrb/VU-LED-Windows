#include "audio/capture.h"

#include <cmath>
#include <cstdint>

// ksmedia.h do MinGW-w64 não fornece a definição deste GUID; definimos manualmente.
static const GUID GUID_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
    0x00000003, 0x0000, 0x0010,
    {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

AudioCapture gAudio;

void AudioCapture::Shutdown() {
    if (capture) { capture->Release(); capture = nullptr; }
    if (client)  { client->Release();  client  = nullptr; }
    if (mix)     { CoTaskMemFree(mix); mix      = nullptr; }
    if (device)  { device->Release();  device   = nullptr; }
    ready = false;
}

bool AudioCapture::Init() {
    Shutdown();
    HRESULT hr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                          CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                          (void**)&enumerator);
    if (FAILED(hr)) return false;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) { Shutdown(); return false; }
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          (void**)&client);
    if (FAILED(hr)) { Shutdown(); return false; }
    hr = client->GetMixFormat(&mix);
    if (FAILED(hr)) { Shutdown(); return false; }

    // Determina o formato: float (padrão) ou PCM 16-bit.
    if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE* ext = (WAVEFORMATEXTENSIBLE*)mix;
        isFloat = IsEqualGUID(ext->SubFormat, GUID_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    } else {
        isFloat = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    }
    channels = mix->nChannels;

    // Loopback: captura o que o renderizador de destino está tocando.
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_LOOPBACK,
                            0, 0, mix, nullptr);
    if (FAILED(hr)) { Shutdown(); return false; }
    hr = client->GetService(__uuidof(IAudioCaptureClient), (void**)&capture);
    if (FAILED(hr)) { Shutdown(); return false; }
    hr = client->Start();
    if (FAILED(hr)) { Shutdown(); return false; }

    ready = true;
    return true;
}

// Drena todos os pacotes disponíveis e devolve o pico por canal (0..1).
void AudioCapture::Poll(float& peakL, float& peakR) {
    peakL = peakR = 0.0f;
    if (!ready) {
        // Sem áudio (dispositivo removido, etc.): tenta reinit a cada ~1 s.
        retryAccum += 0.010;
        if (retryAccum >= 1.0) { retryAccum = 0.0; Init(); }
        return;
    }

    for (;;) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        HRESULT hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
        if (hr == AUDCLNT_S_BUFFER_EMPTY) break;
        if (FAILED(hr)) { Shutdown(); return; }

        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
            if (isFloat) {
                const float* p = (const float*)data;
                for (UINT32 i = 0; i < frames; ++i) {
                    float l = std::fabs(p[i * channels]);
                    float r = (channels > 1) ? std::fabs(p[i * channels + 1]) : l;
                    if (l > peakL) peakL = l;
                    if (r > peakR) peakR = r;
                }
            } else {
                const int16_t* p = (const int16_t*)data;
                for (UINT32 i = 0; i < frames; ++i) {
                    float l = std::fabs(p[i * channels] / 32768.0f);
                    float r = (channels > 1)
                            ? std::fabs(p[i * channels + 1] / 32768.0f) : l;
                    if (l > peakL) peakL = l;
                    if (r > peakR) peakR = r;
                }
            }
        }
        capture->ReleaseBuffer(frames);
    }
}
