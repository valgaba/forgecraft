#pragma once
#include "recorder.h"
#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
class LevelMeter;

class RecordDialog : public QDialog {
    Q_OBJECT
public:
    RecordDialog(const AudioData &destination, qint64 first, qint64 last, QWidget *parent = nullptr, Recorder *source = nullptr);
    AudioData take() const { return recorder->buffer.audio; }
    Recording::Mode mode() const;
    qint64 position() const;
    qint64 selectionEnd() const { return targetEnd; }
    void accept() override;
    void reject() override;
private:
    enum State { Idle, Prepared, Capturing, Paused, Stopped } state = Idle;
    Recorder *recorder;
    QComboBox *deviceBox, *rateBox, *channelsBox, *modeBox;
    QDoubleSpinBox *positionBox, *durationBox;
    QCheckBox *monitorBox, *dcBox;
    QLabel *attributes, *timeLabel, *capacityLabel, *stateLabel;
    LevelMeter *meters;
    QPushButton *prepareButton, *recordButton, *pauseButton, *stopButton, *useButton, *newButton;
    int targetRate, targetChannels;
    qint64 targetFrames, targetFirst, targetEnd;
    bool switching = false;
    int rate() const;
    int channels() const;
    bool openInput(bool record);
    void startRecord();
    void pauseRecord();
    void stopRecord();
    void prepare();
    void newTake();
    void changeMode();
    void updateUi();
};
