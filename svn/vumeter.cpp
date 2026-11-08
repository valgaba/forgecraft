#include "vumeter.h"
#include "theme.h"
#include <QPainter>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

namespace {
double meterFraction(double amplitude) {
    return amplitude > 0 ? std::clamp((20.0 * std::log10(amplitude) + 60.0) / 60.0, 0.0, 1.0) : 0.0;
}
}

VuMeter::VuMeter(QWidget *parent, Qt::Orientation direction) : QFrame(parent), orientation(direction) {
    setObjectName("VuMeter");
    setMinimumSize(180, 28);
    resize(380, 28);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    if (orientation == Qt::Vertical) {
        setMinimumSize(88, 180); resize(88, 260);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }
    setAttribute(Qt::WA_OpaquePaintEvent);
    animation.setTimerType(Qt::PreciseTimer);
    animation.setInterval(16);
    connect(&animation, &QTimer::timeout, this, &VuMeter::animate);
}

void VuMeter::setLevels(double left, double right) {
    target[0] = std::clamp(left, 0.0, 1.0);
    target[1] = std::clamp(right, 0.0, 1.0);
    if (!animation.isActive()) { clock.start(); animation.start(); }
}

void VuMeter::animate() {
    const double seconds = std::clamp(clock.nsecsElapsed() / 1e9, 0.001, 0.2);
    clock.restart();
    bool silent = true;
    for (int channel = 0; channel < 2; ++channel) {
        const double value = target[channel];
        // Ataque rápido y caída lenta, como el medidor de Radit.
        const double response = value > level[channel] ? 0.022 : 0.160;
        level[channel] += (value - level[channel]) * (1.0 - std::exp(-seconds / response));
        if (value >= peak[channel]) peak[channel] = value;
        else peak[channel] = std::max(level[channel], peak[channel] - seconds * 0.35);
        silent &= value < 0.001 && level[channel] < 0.001 && peak[channel] < 0.001;
    }
    if (silent) { animation.stop(); level[0] = level[1] = peak[0] = peak[1] = 0.0; }
    update();
}

void VuMeter::reset() {
    animation.stop();
    target[0] = target[1] = 0.0;
    level[0] = level[1] = 0.0;
    peak[0] = peak[1] = 0.0;
    clock.invalidate();
    update();
}
void VuMeter::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), Theme::Panel);
    if (orientation == Qt::Vertical) {
        QFont labels = font(); labels.setPixelSize(10); painter.setFont(labels);
        const int top = 23, bottom = height() - 22, span = bottom - top;
        const int barWidth = 18, left = 6, right = width() - 6 - barWidth;
        painter.setPen(Theme::TextDim);
        painter.drawText(QRect(left, 0, barWidth, 20), Qt::AlignCenter, "L");
        painter.drawText(QRect(right, 0, barWidth, 20), Qt::AlignCenter, "R");
        painter.drawText(QRect(0, height() - 20, width(), 18), Qt::AlignCenter, "dBFS");
        for (int db : {-60, -48, -36, -24, -12, -6, 0}) {
            const int y = bottom - int(span * (db + 60) / 60.0);
            painter.drawText(QRect(left + barWidth, y - 7, right - left - barWidth, 14), Qt::AlignCenter, QString::number(db));
        }
        for (int channel = 0; channel < 2; ++channel) {
            const int x = channel == 0 ? left : right;
            const QRect area(x, top, barWidth, span + 1);
            painter.fillRect(area, Theme::Window);
            const int levelHeight = int(span * meterFraction(level[channel]));
            painter.fillRect(QRect(x + 1, bottom - levelHeight, barWidth - 2, levelHeight), Theme::Accent);
            if (meterFraction(peak[channel]) > 0) {
                const int y = bottom - int(span * meterFraction(peak[channel]));
                painter.fillRect(QRect(x + 1, y, barWidth - 2, 2), Theme::Text);
            }
            painter.setPen(Theme::Border); painter.drawRect(area);
        }
        return;
    }
    const int rulerHeight = 12, gap = 1;
    const int barHeight = (height() - rulerHeight - 2 * gap - 2) / 2;
    const int rulerTop = 1 + barHeight + gap;
    QFont scale = font(); scale.setPixelSize(9); painter.setFont(scale);
    const QFontMetrics metrics(scale);
    const int span = width() - 2;
    for (int db : {-60, -48, -36, -24, -12, -6, 0}) {
        const int x = 1 + int(span * (db + 60) / 60.0);
        const QString text = QString::number(db);
        const int labelWidth = metrics.horizontalAdvance(text);
        const int labelX = std::clamp(x - labelWidth / 2, 1, qMax(1, width() - labelWidth - 1));
        painter.setPen(Theme::TextDim);
        painter.drawText(labelX, rulerTop + 9, text);
        const int tickX = qMin(x, width() - 2);
        painter.drawLine(tickX, rulerTop, tickX, rulerTop + 1);
        painter.drawLine(tickX, rulerTop + rulerHeight - 2, tickX, rulerTop + rulerHeight - 1);
    }
    for (int channel = 0; channel < 2; ++channel) {
        const int top = channel == 0 ? 1 : rulerTop + rulerHeight + gap;
        const QRect area(1, top, width() - 2, barHeight);
        painter.fillRect(area, Theme::Window);
        QRect bar = area;
        bar.setWidth(int(area.width() * meterFraction(level[channel])));
        painter.fillRect(bar, Theme::Accent);
        if (meterFraction(peak[channel]) > 0) {
            const int x = area.left() + int((area.width() - 2) * meterFraction(peak[channel]));
            painter.fillRect(QRect(x, area.top(), 2, area.height()), Theme::Text);
        }
    }
    painter.setPen(Theme::Border);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}
