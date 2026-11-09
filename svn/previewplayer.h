#pragma once
#include "audio.h"
#include <QObject>
#include <QTimer>
#include <QThread>
#include <atomic>
#include <functional>
#include <memory>

// Salida continua de preescucha: bloques cortos, bucle y cambios sin reabrir
// el dispositivo ni sobrescribir memoria que waveOut todavía está leyendo.
class PreviewPlayer : public QObject {
public:
    explicit PreviewPlayer(QObject *parent = nullptr);
    ~PreviewPlayer() override;
    bool start(const AudioData &audio, quint32 device, QString &error);
    bool updateAudio(const AudioData &audio, QString &error);
    bool setMuted(bool muted);
    void stop();
    bool active() const;
    std::function<void(double, double)> levels;
    std::function<void(const QString &)> failed;
private:
    friend int previewLoopTests();
    struct Native;
    std::unique_ptr<Native> native;
    QThread audioThread;
    QObject *worker = nullptr;
    QTimer *timer = nullptr;
    std::atomic_bool running{false};
    std::atomic<quint64> generation{0};
    AudioData current, previous, pending;
    bool hasPending = false;
    bool muted = false; // Se accede solo desde el hilo de audio.
    qint64 position = 0;
    int fadeLeft = 0, fadeFrames = 0;
    void pump();
    void fill(int index);
    bool startOutput(const AudioData &audio, quint32 device, QString &error);
    bool updateOutput(const AudioData &audio, QString &error);
    void stopOutput();
    void postLevels(double left, double right);
};
