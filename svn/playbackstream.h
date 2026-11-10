#pragma once
#include "audio.h"
#include <memory>

// Transporte finito para audio grande. Un hilo dedicado mantiene una cola
// pequeña, sin convertir ni enviar toda la sesión a waveOut de una vez.
class PlaybackStream {
public:
    PlaybackStream();
    ~PlaybackStream();
    bool start(void *handle, const AudioData &audio, qint64 first, qint64 last, bool floating, QString &error);
    void stop();
    bool finished() const;
    qint64 position() const;
    QString error() const;
    qint64 bufferedBytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
