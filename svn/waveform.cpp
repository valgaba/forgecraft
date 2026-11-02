#include "waveform.h"
#include "theme.h"
#include <QApplication>
#include <QPainter>
#include <QFontMetrics>
#include <QPolygonF>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <algorithm>

namespace {
QString markerTime(qint64 frames, int rate) {
    const qint64 ms = frames * 1000 / qMax(1, rate);
    return QString("%1:%2:%3,%4").arg(ms / 3600000, 2, 10, QChar('0')).arg(ms / 60000 % 60, 2, 10, QChar('0')).arg(ms / 1000 % 60, 2, 10, QChar('0')).arg(ms % 1000, 3, 10, QChar('0'));
}
}

Waveform::Waveform(QWidget *parent) : QWidget(parent) { setMinimumSize(320, 100); setFocusPolicy(Qt::StrongFocus); setMouseTracking(true); }
qint64 Waveform::visibleFrames() const { return audio ? qMax(qint64(16), qint64(audio->frames() / zoom)) : 16; }
qint64 Waveform::at(int x) const { return audio ? std::clamp(start + qint64(double(x - Theme::GutterWidth) / qMax(1, width() - Theme::GutterWidth - 2) * visibleFrames()), qint64(0), audio->frames()) : 0; }
int Waveform::xAt(qint64 frame) const { return Theme::GutterWidth + int(double(frame - start) / visibleFrames() * (width() - Theme::GutterWidth - 2)); }
void Waveform::ensurePeaks() {
    const float *data = audio->samples.constData();
    if (cacheData == data && cacheSize == audio->samples.size() && cacheChannels == audio->channels) return;
    cacheData = data; cacheSize = audio->samples.size(); cacheChannels = audio->channels;
    qint64 blocks = (audio->frames() + 127) / 128;
    minima.fill(0, blocks * cacheChannels); maxima.fill(0, blocks * cacheChannels);
    float *lo = minima.data(), *hi = maxima.data();
    for (qint64 f = 0; f < audio->frames(); ++f) for (int c = 0; c < cacheChannels; ++c) {
        qint64 index = (f / 128) * cacheChannels + c; float value = data[f * cacheChannels + c]; lo[index] = qMin(lo[index], value); hi[index] = qMax(hi[index], value);
    }
}
void Waveform::peaks(qint64 first, qint64 last, int channel, float &low, float &high) const {
    while (first < last && first % 128) { float value = cacheData[first * cacheChannels + channel]; low = qMin(low, value); high = qMax(high, value); ++first; }
    while (first + 128 <= last) { qint64 index = (first / 128) * cacheChannels + channel; low = qMin(low, minima[index]); high = qMax(high, maxima[index]); first += 128; }
    while (first < last) { float value = cacheData[first * cacheChannels + channel]; low = qMin(low, value); high = qMax(high, value); ++first; }
}
void Waveform::resetView() { zoom = 1; start = 0; anchor = cursor = 0; update(); emit selectionChanged(); }
void Waveform::zoomBy(double factor) {
    if (!audio) return;
    zoom = std::clamp(zoom * factor, 1.0, qMax(1.0, audio->frames() / 16.0));
    start = std::clamp(cursor - visibleFrames() / 2, qint64(0), qMax(qint64(0), audio->frames() - visibleFrames())); update(); emit selectionChanged();
}
void Waveform::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Theme::Window);
    QFont labels = font(); labels.setPixelSize(Theme::RulerTextPx); p.setFont(labels);
    const int left = Theme::GutterWidth, ruler = Theme::RulerHeight, right = width() - 2, bottom = height() - 2;
    p.fillRect(0, 0, width(), ruler, Theme::Panel);
    p.fillRect(0, ruler, left, bottom - ruler, Theme::Panel);
    p.setPen(Theme::Border); p.drawRect(rect().adjusted(0, 0, -1, -1)); p.drawLine(left, 0, left, bottom); p.drawLine(left, ruler - 1, right, ruler - 1);
    if (!audio) return;
    bool empty = !audio->frames();
    if (!empty) ensurePeaks();
    for (int i = 0; i <= 10; ++i) {
        int x = left + (right - left) * i / 10;
        p.setPen(Theme::TextDim); p.drawLine(x, 20, x, ruler - 1);
        qint64 ms = empty ? i * 3 : (start + visibleFrames() * i / 10) * 1000 / audio->rate;
        QString text = QString("%1:%2:%3,%4").arg(ms / 3600000, 2, 10, QChar('0')).arg(ms / 60000 % 60, 2, 10, QChar('0')).arg(ms / 1000 % 60, 2, 10, QChar('0')).arg(ms % 1000, 3, 10, QChar('0'));
        if (x + 78 < right) p.drawText(x + 2, 13, text);
        for (int j = 1; j < 5; ++j) { int tick = x + (right - left) * j / 50; p.drawLine(tick, 24, tick, ruler - 1); }
    }
    const double band = double(bottom - ruler) / audio->channels;
    for (int c = 0; c < audio->channels; ++c) {
        const int top = ruler + int(c * band), mid = top + int(band / 2); double amplitude = band * 0.47;
        if (empty) { p.fillRect(QRect(left + 1, top, right - left - 1, int(band)), Theme::Panel); }
        p.setPen(Theme::Border); p.drawLine(left + 1, mid, right, mid);
        p.setPen(Theme::BorderMid); p.drawLine(0, top, right, top);
        p.setPen(Theme::TextDim); p.drawText(4, top + int(band * .26), "−6,0"); p.drawText(5, mid + 3, "−Inf."); p.drawText(4, top + int(band * .77), "−6,0");
        if (empty) continue;
        for (int x = left; x <= right; ++x) {
            qint64 a = at(x), b = qMin(audio->frames(), qMax(a + 1, at(x + 1)));
            float lo = 0, hi = 0;
            peaks(a, b, c, lo, hi);
            bool selected = first() != last() && a >= first() && a < last();
            if (selected) { p.fillRect(QRect(x, top + 1, 1, int(band) - 2), Theme::SelectionBand); }
            p.setPen(selected ? Theme::Text : Theme::Accent); p.drawLine(x, mid - int(hi * amplitude), x, mid - int(lo * amplitude));
        }
    }
    // Fantasma del fragmento arrastrado: se dibuja en el destino, en la posición
    // donde caería, para ver el tramo antes de soltarlo.
    if (previewSamples && previewAt >= 0 && !empty) {
        const qint64 frames = qMax(qint64(1), previewLast - previewFirst);
        const int startX = xAt(previewAt), endX = xAt(previewAt + frames);
        const int fromX = qMax(left + 1, startX), toX = qMin(right, endX);
        if (toX > fromX && previewChannels > 0) {
            const QColor ghost = Theme::DragGhost;
            p.fillRect(QRect(fromX, ruler, toX - fromX, bottom - ruler), QColor(ghost.red(), ghost.green(), ghost.blue(), 52));
            const double ghostBand = double(bottom - ruler) / audio->channels;
            p.setPen(QColor(ghost.red(), ghost.green(), ghost.blue(), 235));
            for (int channel = 0; channel < audio->channels; ++channel) {
                const int mid = ruler + int(channel * ghostBand) + int(ghostBand / 2);
                const double amplitude = ghostBand * 0.47;
                const int source = qMin(channel, previewChannels - 1);
                for (int x = fromX; x < toX; ++x) {
                    const int span = qMax(1, endX - startX);
                    qint64 a = previewFirst + qint64(double(x - startX) / span * frames);
                    qint64 b = previewFirst + qint64(double(x + 1 - startX) / span * frames);
                    a = qBound(previewFirst, a, previewLast - 1);
                    b = qBound(a + 1, b, previewLast);
                    float low = 0, high = 0;
                    for (qint64 frame = a; frame < b; ++frame) {
                        const float value = previewSamples[frame * previewChannels + source];
                        low = qMin(low, value); high = qMax(high, value);
                    }
                    p.drawLine(x, mid - int(high * amplitude), x, mid - int(low * amplitude));
                }
            }
            p.setPen(QPen(ghost, 1));
            if (startX >= left) p.drawLine(startX, ruler, startX, bottom);
            if (endX <= right) p.drawLine(endX, ruler, endX, bottom);
        }
    }
    // Marcas: banderín en la regla y línea de puntos sobre la onda. La marca
    // seleccionada o bajo el puntero muestra su nombre.
    for (int index = 0; index < audio->markers.size(); ++index) {
        const Marker &marker = audio->markers[index];
        const int x = xAt(marker.frame);
        if (x < left || x > right) continue;
        const bool highlighted = index == selectedMarker || index == hoverMarker;
        const QColor markerColor = highlighted ? Theme::Marker.lighter(135) : Theme::Marker;
        p.setPen(QPen(markerColor, 1, Qt::DashLine));
        p.drawLine(x, ruler, x, bottom);
        p.setPen(Qt::NoPen);
        p.setBrush(markerColor);
        p.drawPolygon(QPolygonF{QPointF(x, 2), QPointF(x + 7, 5), QPointF(x + 7, 12), QPointF(x, 9)});
        p.drawRect(QRectF(x - 0.5, 2, 1.5, ruler - 3));
        if (highlighted) {
            // Etiqueta sobre la onda, con fondo, para que se lea siempre.
            const QString label = QString("%1  %2").arg(marker.name, markerTime(marker.frame, audio->rate));
            const QFontMetrics metrics(p.font());
            const QRect box(x + 8, ruler + 2, metrics.horizontalAdvance(label) + 10, metrics.height() + 2);
            p.setPen(Qt::NoPen); p.setBrush(Theme::Panel);
            p.drawRect(box);
            p.setPen(Theme::Text);
            p.drawText(box.adjusted(5, 0, -5, 0), Qt::AlignCenter, label);
        }
    }
    p.save(); p.setClipRect(QRect(left, ruler, right - left, bottom - ruler));
    if (!empty) { p.setPen(Theme::Text); p.drawLine(xAt(cursor), ruler, xAt(cursor), bottom); }
    if (playhead >= 0) { p.setPen(Theme::Playhead); p.drawLine(xAt(playhead), ruler, xAt(playhead), bottom); }
    p.restore();
}
// El puntero avisa cuando está sobre un extremo de la selección: se puede
// arrastrar para ampliarla o reducirla.
void Waveform::updateSelectionCursor(const QPoint &pos) {
    Qt::CursorShape shape = Qt::ArrowCursor;
    if (adjustingEdge == 0 && first() != last() && pos.x() > Theme::GutterWidth) {
        const int distance = qMin(qAbs(pos.x() - xAt(first())), qAbs(pos.x() - xAt(last())));
        if (distance <= Theme::SelectionHandle) shape = Qt::SplitHCursor;
    }
    if (QWidget::cursor().shape() == shape) return;
    if (shape == Qt::ArrowCursor) QWidget::unsetCursor(); else QWidget::setCursor(shape);
}

void Waveform::setDropPreview(const float *samples, int channels, qint64 first, qint64 last, qint64 at) {
    previewSamples = samples; previewChannels = channels;
    previewFirst = first; previewLast = last; previewAt = at;
    update();
}
void Waveform::clearDropPreview() {
    if (!previewSamples) return;
    previewSamples = nullptr; previewChannels = 0; previewAt = -1;
    update();
}
int Waveform::markerAt(const QPoint &pos) const {
    if (!audio || pos.y() > Theme::RulerHeight) return -1;
    for (int index = 0; index < audio->markers.size(); ++index)
        if (qAbs(pos.x() - xAt(audio->markers[index].frame)) <= 7) return index;
    return -1;
}
void Waveform::updateHover(const QPoint &pos) {
    const int index = markerAt(pos);
    if (index >= 0) {
        if (index != hoverMarker) { hoverMarker = index; update(); }
        setCursor(Qt::PointingHandCursor);
        return;
    }
    if (hoverMarker >= 0) { hoverMarker = -1; update(); }
    // Fuera de los banderines manda el cursor de los extremos de la selección.
    updateSelectionCursor(pos);
}
void Waveform::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    const int markerIndex = markerAt(e->position().toPoint());
    if (markerIndex >= 0) {
        selectedMarker = markerIndex;
        anchor = cursor = audio->markers[markerIndex].frame;
        draggingOut = false; draggingSelection = false; pressMoved = false; adjustingEdge = 0;
        update(); emit selectionChanged();
        return;
    }
    const int x = int(e->position().x());
    const qint64 frame = at(x);
    draggingOut = false;
    draggingSelection = false;
    pressMoved = false;
    adjustingEdge = 0;
    pressGlobal = e->globalPosition().toPoint();
    // Cerca de un extremo de la selección se ajusta ese extremo, aunque la
    // pulsación caiga fuera de la selección.
    if (first() != last() && x > Theme::GutterWidth) {
        const int fromStart = qAbs(x - xAt(first())), fromEnd = qAbs(x - xAt(last()));
        if (fromStart <= Theme::SelectionHandle && fromStart <= fromEnd) adjustingEdge = -1;
        else if (fromEnd <= Theme::SelectionHandle) adjustingEdge = 1;
    }
    if (adjustingEdge) {
        edgeStart = first(); edgeEnd = last();
        setCursor(Qt::SplitHCursor);
        return;
    }
    // Una pulsación dentro de la selección puede acabar arrastrándola fuera como
    // documento nuevo; mientras tanto no se toca la selección.
    draggingSelection = first() != last() && frame >= first() && frame <= last() && x > Theme::GutterWidth;
    if (!draggingSelection) { anchor = cursor = frame; update(); emit selectionChanged(); }
}

void Waveform::mouseMoveEvent(QMouseEvent *e) {
    if (!(e->buttons() & Qt::LeftButton)) { updateHover(e->position().toPoint()); return; }
    const QPoint global = e->globalPosition().toPoint();
    if (!pressMoved && (global - pressGlobal).manhattanLength() > QApplication::startDragDistance()) pressMoved = true;
    if (adjustingEdge) {
        const qint64 frame = at(int(e->position().x()));
        if (adjustingEdge < 0) { anchor = qBound(qint64(0), frame, edgeEnd); cursor = edgeEnd; }
        else { anchor = edgeStart; cursor = qBound(edgeStart, frame, audio ? audio->frames() : frame); }
        update(); emit selectionChanged();
        return;
    }
    if (draggingSelection) {
        // Pulsar dentro de la selección siempre la arrastra: no se cambia mientras
        // el botón siga pulsado, para poder sacarla del lienzo sin perderla.
        if (!rect().contains(e->position().toPoint()) && !draggingOut && pressMoved) {
            draggingOut = true;
            setCursor(Qt::DragMoveCursor);
        }
        if (draggingOut && dragStatus) dragStatus(first(), last(), global);
        return;
    }
    cursor = at(int(e->position().x()));
    update(); emit selectionChanged();
}

void Waveform::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) { QWidget::mouseReleaseEvent(e); return; }
    if (adjustingEdge) {
        adjustingEdge = 0;
        update(); emit selectionChanged();
        updateSelectionCursor(e->position().toPoint());
        return;
    }
    const qint64 selectedFirst = first(), selectedLast = last();
    const bool startedInside = draggingSelection;
    const bool outside = !rect().contains(e->position().toPoint());
    const bool copyOut = draggingOut && outside;
    const bool dragged = draggingOut;
    const bool moved = pressMoved;
    draggingOut = false;
    draggingSelection = false;
    pressMoved = false;
    if (dragged && dragFinished) dragFinished();
    if (copyOut) {
        unsetCursor();
        if (dragOut && selectedLast > selectedFirst) dragOut(selectedFirst, selectedLast, e->globalPosition().toPoint());
        return;
    }
    if (dragged) { unsetCursor(); return; }   // soltada dentro: la selección se conserva
    unsetCursor();
    // Un clic (sin arrastre) dentro de una selección la reduce al punto pulsado.
    if (startedInside && !moved) { anchor = cursor = at(int(e->position().x())); update(); emit selectionChanged(); }
    updateSelectionCursor(e->position().toPoint());
}

void Waveform::mouseDoubleClickEvent(QMouseEvent *e) {
    const int markerIndex = markerAt(e->position().toPoint());
    if (markerIndex >= 0) { if (markerRename) markerRename(markerIndex); return; }
    if (!audio) return;
    // Doble clic en la zona entre dos marcas: selecciona ese tramo.
    const qint64 frame = at(int(e->position().x()));
    qint64 before = -1, after = -1;
    for (const auto &marker : audio->markers) {
        if (marker.frame <= frame && (before < 0 || marker.frame > before)) before = marker.frame;
        if (marker.frame >= frame && (after < 0 || marker.frame < after)) after = marker.frame;
    }
    if (before >= 0 && after > before) { anchor = before; cursor = after; }
    else { anchor = 0; cursor = audio->frames(); }
    update(); emit selectionChanged();
}
void Waveform::wheelEvent(QWheelEvent *e) {
    if (!audio) return;
    if (e->modifiers() & Qt::ControlModifier) zoomBy(e->angleDelta().y() > 0 ? 2 : 0.5);
    else { start = std::clamp(start - qint64(e->angleDelta().y() / 120.0 * visibleFrames() / 8), qint64(0), qMax(qint64(0), audio->frames() - visibleFrames())); update(); emit selectionChanged(); }
}

