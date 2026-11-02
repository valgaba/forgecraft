#pragma once
#include <QDialog>

class QComboBox;
class QButtonGroup;
class QLabel;

// Cuadro de documento nuevo, como la ventana «New Window» de los editores
// clásicos: frecuencia, profundidad de muestreo, canales y tiempo máximo de
// edición, con el estilo de Radit.
class NewAudioDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewAudioDialog(QWidget *parent = nullptr);
    int rate() const;
    int channels() const;
    int bitDepth() const;
    bool floatFormat() const;
private:
    QComboBox *rateBox;
    QButtonGroup *depthGroup, *channelGroup;
    QLabel *maxTime;
    void updateMaxTime();
};
