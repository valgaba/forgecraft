#pragma once
#include "dsp.h"
#include <QObject>
#include <QFutureWatcher>
#include <QTimer>
#include <functional>
#include <memory>

// Recalcula la preescucha en segundo plano y publica solo el último ajuste.
class ProcessPreview : public QObject {
public:
    ProcessPreview(const AudioData &source, qint64 first, qint64 last, const QString &operation, QObject *parent = nullptr);
    ~ProcessPreview() override;
    void setValues(const Dsp::Values &values);
    void setActive(bool active);
    void setBypass(bool bypass);
    bool active() const { return enabled; }
    std::function<bool(const AudioData &, std::function<void()>)> playAudio;
    std::function<void()> stopAudio;
    std::function<void(bool)> activeChanged;
    std::function<void(const QString &)> statusChanged;
private:
    struct Work { AudioData dry, wet; QString error; quint64 revision = 0; bool canceled = false; };
    AudioData source, dry, wet;
    qint64 first, last;
    QString operation;
    Dsp::Values parameters;
    bool enabled = false, bypass = false, pending = false, outputStarted = false;
    quint64 revision = 0;
    QTimer refresh;
    QFutureWatcher<Work> watcher;
    std::shared_ptr<std::atomic_bool> cancel;
    void request(int delay);
    void rebuild();
    void ready();
    void playCurrent();
    void status(const QString &text);
};
