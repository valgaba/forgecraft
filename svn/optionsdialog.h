#pragma once
#include <QDialog>
#include <QPair>
#include <QVector>
#include <functional>

class QComboBox;
class QPushButton;

// Opciones generales de la aplicación. Por ahora: el dispositivo de salida de
// audio, con un botón para probar el tono en el dispositivo elegido.
class OptionsDialog : public QDialog {
    Q_OBJECT
public:
    OptionsDialog(const QVector<QPair<quint32, QString>> &outputs, quint32 currentOutput, const QVector<QPair<quint32, QString>> &inputs, quint32 currentInput, QWidget *parent = nullptr);
    quint32 outputDevice() const;
    quint32 inputDevice() const;
    // Reproduce un tono corto en el dispositivo indicado. Devuelve false si no
    // se pudo abrir.
    std::function<bool(quint32)> testDevice;
private:
    QComboBox *deviceBox, *inputBox;
    QPushButton *testButton;
};
