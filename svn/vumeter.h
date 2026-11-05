#pragma once
#include <QElapsedTimer>
#include <QFrame>

// Vúmetro horizontal de dos canales al estilo de Radit: barra de nivel con
// caída suave y marca de pico. Se alimenta desde el temporizador principal.
class VuMeter : public QFrame {
    Q_OBJECT
public:
    explicit VuMeter(QWidget *parent = nullptr);
    void setLevels(double left, double right);
    // Bajada inmediata a cero (al detener la reproducción o la grabación).
    void reset();
protected:
    void paintEvent(QPaintEvent *) override;
private:
    double level[2] = {0.0, 0.0};
    double peak[2] = {0.0, 0.0};
    QElapsedTimer clock;
};
