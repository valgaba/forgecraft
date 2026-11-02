#pragma once
#include <QDialog>
#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;

// Cuadro de mezcla: el fragmento arrastrado se suma al audio del destino con un
// volumen para cada uno, como el Mix de Sound Forge. Los valores van en décimas
// de dB; por debajo de -60 dB el volumen es silencio.
class MixDialog : public QDialog {
    Q_OBJECT
public:
    MixDialog(qint64 fragmentFrames, int rate, QWidget *parent = nullptr);
    double sourceGain() const;
    double targetGain() const;
    qint64 tuneFrames() const;
    // Reproduce el resultado con los valores actuales sin aplicarlo; `finished`
    // se llama al terminar la escucha. Devuelve false si no se pudo empezar.
    std::function<bool(double sourceGain, double targetGain, qint64 tune, std::function<void()> finished)> preview;
    std::function<void()> stopPreview;
    void done(int result) override;
private:
    QSlider *sourceSlider = nullptr, *targetSlider = nullptr;
    QComboBox *presetBox = nullptr;
    QDoubleSpinBox *tuneBox = nullptr;
    QLabel *sourceValue = nullptr, *targetValue = nullptr;
    QPushButton *previewButton = nullptr;
    int sampleRate;
    void updateLabels();
    void applyPreset(int index);
    void stopListening();
};
