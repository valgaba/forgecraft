#include "theme.h"
#include <QDebug>

namespace Theme {

QPalette palette() {
    QPalette p;
    p.setColor(QPalette::Window, Window);
    p.setColor(QPalette::WindowText, Text);
    p.setColor(QPalette::Base, PanelRaised);
    p.setColor(QPalette::AlternateBase, Panel);
    p.setColor(QPalette::Text, Text);
    p.setColor(QPalette::Button, Panel);
    p.setColor(QPalette::ButtonText, Text);
    p.setColor(QPalette::BrightText, Accent);
    p.setColor(QPalette::Highlight, Selection);
    p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::ToolTipBase, Border);
    p.setColor(QPalette::ToolTipText, TextDim);
    p.setColor(QPalette::PlaceholderText, TextDim);
    p.setColor(QPalette::Light, BorderMid);
    p.setColor(QPalette::Midlight, Border);
    p.setColor(QPalette::Mid, BorderMid);
    p.setColor(QPalette::Dark, QColor(0x10, 0x14, 0x1b));
    p.setColor(QPalette::Shadow, QColor(0x0b, 0x0e, 0x14));
    p.setColor(QPalette::Link, Accent);
    p.setColor(QPalette::LinkVisited, Selection);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, BorderSoft);
    }
    p.setColor(QPalette::Disabled, QPalette::Base, Panel);
    p.setColor(QPalette::Disabled, QPalette::Button, Panel);
    p.setColor(QPalette::Disabled, QPalette::Highlight, Border);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, TextDim);
    return p;
}

QIcon icon(const QString &name) {
    const QIcon result(QStringLiteral(":/icons/") + name + QStringLiteral(".svg"));
    // Sin el motor de iconos SVG (plugin qsvgicon) el icono queda vacío.
    if (result.isNull()) qWarning("No se pudo cargar el icono %s", qUtf8Printable(name));
    return result;
}

} // namespace Theme
