#pragma once
#include "audio.h"
#include <QWidget>
#include <functional>

class Waveform : public QWidget {
    Q_OBJECT
public:
    explicit Waveform(QWidget *parent = nullptr);
    AudioData *audio = nullptr;
    qint64 anchor = 0, cursor = 0, playhead = -1;
    qint64 start = 0;
    double zoom = 1;
    // Copiar la selección arrastrándola fuera de la forma de onda.
    std::function<void(qint64 first, qint64 last, const QPoint &global)> dragOut;
    qint64 first() const { return qMin(anchor, cursor); }
    qint64 last() const { return qMax(anchor, cursor); }
    qint64 frameAt(int x) const { return at(x); }
    int pixelAt(qint64 frame) const { return xAt(frame); }
    qint64 visibleFrames() const;
    void zoomBy(double factor);
    void resetView();
    void invalidateCache() { cacheData = nullptr; }
signals:
    void selectionChanged();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    bool draggingSelection = false;   // la pulsación empezó dentro de la selección
    bool draggingOut = false;         // ya se superó el umbral de arrastre
    bool pressMoved = false;          // el puntero se movió desde la pulsación
    int adjustingEdge = 0;            // -1 inicio de selección, +1 final, 0 ninguno
    qint64 edgeStart = 0, edgeEnd = 0;
    QPoint pressGlobal;
    void updateSelectionCursor(const QPoint &pos);
    qint64 at(int x) const;
    int xAt(qint64 frame) const;
    void ensurePeaks();
    void peaks(qint64 first, qint64 last, int channel, float &low, float &high) const;
    const float *cacheData = nullptr;
    qsizetype cacheSize = 0;
    int cacheChannels = 0;
    QVector<float> minima, maxima;
};
