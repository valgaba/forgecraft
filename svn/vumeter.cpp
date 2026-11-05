#include "vumeter.h"
#include "theme.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

VuMeter::VuMeter(QWidget *parent) : QFrame(parent) {
    setObjectName("VuMeter");
    setMinimumSize(180, 36);
    resize(280, 40);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void VuMeter::setLevels(double left, double right) {
    const double seconds = clock.isValid() ? std::clamp(clock.restart() / 1000.0, 0.001, 0.2) : 0.035;
    const double values[2] = {left, right};
    for (int channel = 0; channel < 2; ++channel) {
        const double value = std::clamp(values[channel], 0.0, 1.0);
        // Ataque rápido y caída lenta, como el medidor de Radit.
        const double response = value > level[channel] ? 0.022 : 0.160;
        level[channel] += (value - level[channel]) * (1.0 - std::exp(-seconds / response));
        if (value >= peak[channel]) peak[channel] = value;
        else peak[channel] = std::max(level[channel], peak[channel] - seconds * 0.35);
    }
    update();
}

void VuMeter::reset() {
    level[0] = level[1] = 0.0;
    peak[0] = peak[1] = 0.0;
    clock.invalidate();
    update();
}
void VuMeter::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), Theme::Panel);
    const int gap = 2;
    const int barHeight = (rect().height() - gap - 2) / 2;
    for (int channel = 0; channel < 2; ++channel) {
        const QRect area(1, 1 + channel * (barHeight + gap), rect().width() - 2, barHeight);
        painter.fillRect(area, Theme::Window);
        QRect bar = area;
        bar.setWidth(int(area.width() * level[channel]));
        painter.fillRect(bar, Theme::Accent);
        if (peak[channel] > 0.002) {
            const int x = area.left() + int((area.width() - 2) * peak[channel]);
            painter.fillRect(QRect(x, area.top(), 2, area.height()), Theme::Text);
        }
    }
    painter.setPen(Theme::Border);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}
