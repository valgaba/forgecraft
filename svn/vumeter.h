#pragma once
#include <QElapsedTimer>
#include <QFrame>
#include <QTimer>

// Vúmetro horizontal de dos canales al estilo de Radit: barra de nivel con
// caída suave y marca de pico. La animación es independiente del muestreo.
class VuMeter : public QFrame {
    Q_OBJECT
public:
    explicit VuMeter(QWidget *parent = nullptr, Qt::Orientation orientation = Qt::Horizontal);
    QSize sizeHint() const override { return orientation == Qt::Vertical ? QSize(88, 260) : QSize(380, 28); }
    void setLevels(double left, double right);
    // Bajada inmediata a cero (al detener la reproducción o la grabación).
    void reset();
protected:
    void paintEvent(QPaintEvent *) override;
private:
    void animate();
    double target[2] = {0.0, 0.0};
    double level[2] = {0.0, 0.0};
    double peak[2] = {0.0, 0.0};
    QElapsedTimer clock;
    QTimer animation;
    Qt::Orientation orientation;
};
