#pragma once
#include "audio.h"
#include <QObject>
#include <QTimer>
#include <memory>

struct RecordingDevice { quint32 id; QString name; };

// PCM conversion and take accumulation are independent of the input device.
// Monitoring updates meters/filter state without adding samples to the take.
struct CaptureBuffer {
    AudioData audio;
    bool dcAdjust = false;
    bool muted = false;
    qint64 limitFrames = 0;
    double peaks[2] = {0, 0};
    double previousInput[2] = {0, 0}, previousOutput[2] = {0, 0};
    void reset(int rate, int channels);
    bool consume(const char *pcm, qsizetype bytes, bool store);
    qint64 capacityFrames() const;
};

namespace Recording {
enum Mode { NewDocument, Insert, ReplaceSelection };
struct MergeResult { AudioData audio; qint64 first = 0, last = 0; QString error; };
MergeResult merge(const AudioData &original, const AudioData &take, Mode mode, qint64 position, qint64 selectionEnd);
}

class Recorder : public QObject {
    Q_OBJECT
public:
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;
    static QVector<RecordingDevice> devices();
    virtual bool start(quint32 device, int rate, int channels, bool record, QString &error);
    virtual void stop();
    virtual bool active() const;
    void clear(int rate, int channels);
    CaptureBuffer buffer;
signals:
    void updated();
    void failed(const QString &message);
    void limitReached();
protected:
    bool storing = false;
private:
    struct Native;
    std::unique_ptr<Native> native;
    QTimer timer;
    void poll();
    bool drain(bool requeue);
};
