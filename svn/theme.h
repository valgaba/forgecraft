#pragma once
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QString>

// Tokens de la identidad visual RADIT para ForgeCraft.
//
// Los valores coinciden con `style/forgecraft.qss`, que es la fuente principal
// de la apariencia. Este encabezado existe para lo que la hoja de estilo no
// alcanza: la paleta de respaldo de Qt (flechas de menú, controles que dibuja
// el estilo nativo), las constantes con nombre de los widgets pintados a mano
// (Waveform y VuMeter) y el tinte de los iconos del tema.
//
// Referencia: docs/analisis-estilo-radit.md §3.2 y §9.
namespace Theme {

// Fondos.
inline const QColor Window(0x18, 0x1e, 0x2c);
inline const QColor Panel(0x20, 0x24, 0x2e);
inline const QColor PanelRaised(0x25, 0x2b, 0x39);
inline const QColor Hover(0x30, 0x38, 0x4b);
inline const QColor Pressed(0x1d, 0x22, 0x30);

// Bordes.
inline const QColor Border(0x33, 0x3b, 0x4f);
inline const QColor BorderMid(0x53, 0x5b, 0x6a);
inline const QColor BorderSoft(0x68, 0x6b, 0x74);

// Texto.
inline const QColor Text(0xdf, 0xdb, 0xd2);
inline const QColor TextDim(0x8a, 0x8d, 0x96);

// Acento y estados.
inline const QColor Accent(0x80, 0xa4, 0xae);
inline const QColor AccentDim(0x5f, 0x8a, 0x96);
inline const QColor Selection(0x4e, 0x4d, 0x7a);
inline const QColor MenuBarSelected(0x39, 0x38, 0x5b);
inline const QColor Playhead(0xd9, 0x6b, 0x6b);
inline const QColor RecordDot(0xc0, 0x39, 0x2b);
inline const QColor SelectionBand(0x2c, 0x36, 0x48);
// Fantasma del fragmento que se arrastra: ámbar, para que destaque sobre la onda azul.
inline const QColor DragGhost(0xe5, 0xc0, 0x7b);
// Marcas de posición: rojo, para distinguirlas del cursor y del acento.
inline const QColor Marker(0xcf, 0x4f, 0x4f);

// Métricas de la forma de onda (antes literales dispersos en paintEvent).
inline constexpr int RulerHeight = 27;   // franja superior de la regla de tiempo
inline constexpr int GutterWidth = 28;   // franja izquierda de amplitudes
inline constexpr int SelectionHandle = 5; // agarre de los extremos de la selección
inline constexpr int RulerTextPx = 10;   // tamaño del texto de la regla
inline constexpr int MeterTextPx = 10;   // tamaño del texto de los medidores
inline constexpr int MeterBarWidth = 17; // ancho de cada barra del medidor
inline constexpr int MeterGap = 26;      // separación entre canales
inline constexpr int MeterTop = 24;      // margen superior de las barras
inline constexpr int MeterBottom = 10;   // margen inferior de las barras

// Paleta de respaldo. La hoja QSS manda sobre los widgets que estiliza; esta
// paleta cubre lo que dibuja el estilo (flechas, botones de los spin boxes,
// marcas de los menús) para que no quede ningún resto claro del tema anterior.
QPalette palette();

// Icono del tema desde los recursos incrustados (`:/icons/<nombre>.svg`).
// Los SVG son los de Radit, teñidos con el acento de su identidad visual.
QIcon icon(const QString &name);

} // namespace Theme
