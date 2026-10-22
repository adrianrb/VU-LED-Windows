// Captura WASAPI loopback do dispositivo de render padrão.
// A cada tick drena os pacotes disponíveis e devolve o pico por canal (0..1).
#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>

struct AudioCapture {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice*           device     = nullptr;
    IAudioClient*        client     = nullptr;
    IAudioCaptureClient* capture    = nullptr;
    WAVEFORMATEX*        mix        = nullptr;
    bool  isFloat = true;
    int   channels = 0;
    bool  ready = false;
    double retryAccum = 0.0;

    void Shutdown();
    bool Init();
    void Poll(float& peakL, float& peakR);
};

extern AudioCapture gAudio;
