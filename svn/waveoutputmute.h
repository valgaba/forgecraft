#pragma once
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

// Volumen de una instancia waveOut: nunca se pasa un identificador de dispositivo.
// Se recupera el nivel previo al desmutear y antes de cerrar el stream.
struct WaveOutputMute {
    DWORD volume = 0xffffffffu;
    bool applied = false;
    bool set(HWAVEOUT handle, bool muted) {
        if (!handle || muted == applied) return true;
        if (muted && waveOutGetVolume(handle, &volume) != MMSYSERR_NOERROR) return false;
        if (waveOutSetVolume(handle, muted ? 0 : volume) != MMSYSERR_NOERROR) return false;
        applied = muted; return true;
    }
};
#endif
