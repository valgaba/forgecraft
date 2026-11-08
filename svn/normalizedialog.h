#pragma once
#include "normalization.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QTimer>
#include <functional>
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QRadioButton;
class QLabel;
class QPushButton;
class QGroupBox;
class VuMeter;

class NormalizeDialog : public QDialog {
public:
    NormalizeDialog(const AudioData &audio, qint64 first, qint64 last, const QString &name, QWidget *parent = nullptr);
    ~NormalizeDialog() override;
    Dsp::Values values() const;
    qint64 firstFrame() const { return first; }
    qint64 lastFrame() const { return last; }
    VuMeter *meters = nullptr;
    std::function<bool(const AudioData &, std::function<void()>)> previewAudio;
    std::function<void()> stopAudio;
private:
    AudioData source, dry, wet;
    qint64 first = 0, last = 0;
    Dsp::NormalizeLevels levels;
    bool scanned = false, loading = false, playing = false;
    struct PreviewWork {
        AudioData audio;
        Dsp::NormalizeLevels levels;
        QString error;
        quint64 revision = 0;
        bool canceled = false;
    };
    QFutureWatcher<PreviewWork> previewWorker;
    QTimer refreshTimer;
    std::shared_ptr<std::atomic_bool> refreshCancel;
    quint64 revision = 0;
    bool pendingPreview = false;
    QComboBox *presets, *clipping;
    QDoubleSpinBox *target, *ignore;
    QSpinBox *attack, *release;
    QRadioButton *peakMode, *rmsMode;
    QCheckBox *contour, *reuse, *bypass;
    QGroupBox *scanGroup;
    QLabel *peakValue, *rmsValue, *selection, *previewStatus;
    QPushButton *preview, *deletePreset;
    void settingsChanged(bool analysisChanged);
    void loadValues(const Dsp::Values &values);
    void refreshPresets();
    void updateSelection();
    bool scan();
    void listen(bool on);
    void playCurrent();
    void endPreview();
    void requestPreviewUpdate();
    void rebuildPreview();
    void previewReady();
};
