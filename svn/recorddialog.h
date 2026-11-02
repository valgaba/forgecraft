#pragma once
#include "recorder.h"
#include <QDialog>
#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class LevelMeter;

class RecordDialog : public QDialog {
    Q_OBJECT
public:
    RecordDialog(const AudioData &destination, qint64 first, qint64 last, QWidget *parent = nullptr, Recorder *source = nullptr);
    AudioData take() const { return recorder->buffer.audio; }
    // Cada toma terminada se entrega a este destino: el documento enfocado, al final.
    std::function<bool(const AudioData &)> applyTake;
    void reject() override;
private:
    enum State { Idle, Prepared, Capturing, Paused, Stopped } state = Idle;
    Recorder *recorder;
    QComboBox *deviceBox, *rateBox, *channelsBox;
    QDoubleSpinBox *durationBox;
    QLabel *attributes, *timeLabel, *capacityLabel, *stateLabel;
    LevelMeter *meters;
    QPushButton *recordButton, *pauseButton, *stopButton, *newButton;
    int targetRate, targetChannels;
    qint64 targetFrames, targetFirst, targetEnd;
    bool switching = false;
    int rate() const;
    int channels() const;
    bool openInput(bool record, bool report = true);
    void startMonitoring();
    void startRecord();
    void pauseRecord();
    bool deliverTake();
    void stopCapture();
    void stopRecord();
    void newTake();
    void updateUi();
};
