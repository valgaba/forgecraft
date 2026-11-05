#pragma once
#include "audio.h"
#include <QWidget>
#include <functional>

class Waveform : public QWidget {
    Q_OBJECT
public:
    explicit Waveform(QWidget *parent = nullptr);
    struct PeakData { QVector<float> minima, maxima; bool canceled = false; };
    static PeakData preparePeaks(const AudioData &audio, const AudioData::ReadProgress &progress = {});
    void setPreparedPeaks(PeakData data);
    AudioData *audio = nullptr;
    qint64 anchor = 0, cursor = 0, playhead = -1;
    bool followPlayback = true;
    void setPlayhead(qint64 frame);
    qint64 start = 0;
    double zoom = 1;
    // Copiar la selección arrastrándola fuera de la forma de onda.
    std::function<void(qint64 first, qint64 last, const QPoint &global)> dragOut;
    // Aviso mientras se arrastra: duración y ventana de destino bajo el puntero.
    std::function<void(qint64 first, qint64 last, const QPoint &global)> dragStatus;
    std::function<void()> dragToggleMode;
    bool selectionDragActive() const { return draggingOut; }
    // Doble clic sobre el banderín de una marca para renombrarla.
    std::function<void(int index)> markerRename;
    // Menú contextual (botón derecho) sobre el banderín de una marca.
    std::function<void(int index, const QPoint &global)> markerMenu;
    int selectedMarker = -1;
    // Marca cuyo banderín está en esa posición (regla), o -1.
    int markerAt(const QPoint &pos) const;
    // Fantasma del fragmento que se está arrastrando, dibujado en el destino.
    void setDropPreview(const float *samples, int channels, qint64 first, qint64 last, qint64 at, bool mix);
    void clearDropPreview();
    std::function<void()> dragFinished;
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
    int hoverMarker = -1;
    const float *previewSamples = nullptr;
    int previewChannels = 0;
    qint64 previewFirst = 0, previewLast = 0, previewAt = -1;
    qint64 edgeStart = 0, edgeEnd = 0;
    QPoint pressGlobal;
    void updateSelectionCursor(const QPoint &pos);
    void updateHover(const QPoint &pos);
    qint64 at(int x) const;
    int xAt(qint64 frame) const;
    void ensurePeaks();
    void peaks(qint64 first, qint64 last, int channel, float &low, float &high) const;
    const float *cacheData = nullptr;
    qsizetype cacheSize = 0;
    int cacheChannels = 0;
    QVector<float> minima, maxima;
};
