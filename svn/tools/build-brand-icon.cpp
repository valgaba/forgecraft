// Genera el PNG y el ICO de la aplicación a partir del arte SVG y de la letra.
// Qt SVG no aplica el recorte del dibujo, así que la letra se usa como máscara
// de transparencia, igual que en el icono de Radit.
#include <QGuiApplication>
#include <QSvgRenderer>
#include <QPainter>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (app.arguments().size() != 2) return 1;
    QDir icons(app.arguments()[1]);
    QSvgRenderer artwork(icons.filePath("ForgeCraftAppIcon.svg")), letter(icons.filePath("ForgeCraftLogo.svg"));
    if (!artwork.isValid() || !letter.isValid()) return 2;
    QImage image(512, 512, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QImage mask(image.size(), image.format()); mask.fill(Qt::transparent);
    { QPainter painter(&image); artwork.render(&painter); }
    { QPainter painter(&mask); letter.render(&painter); }
    { QPainter painter(&image); painter.setCompositionMode(QPainter::CompositionMode_DestinationIn); painter.drawImage(0, 0, mask); }
    if (!image.save(icons.filePath("ForgeCraftAppIcon.png"))) return 3;
    QList<QByteArray> pngs; const QList<int> sizes{16, 24, 32, 48, 64, 128, 256};
    for (int size : sizes) {
        QImage scaled = image.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        if (!scaled.save(&buffer, "PNG")) return 4;
        pngs.append(bytes);
    }
    QFile file(icons.filePath("forgecraft.ico"));
    if (!file.open(QIODevice::WriteOnly)) return 5;
    QDataStream data(&file); data.setByteOrder(QDataStream::LittleEndian);
    data << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * sizes.size();
    for (int index = 0; index < sizes.size(); ++index) {
        const int size = sizes[index];
        data << quint8(size == 256 ? 0 : size) << quint8(size == 256 ? 0 : size) << quint8(0) << quint8(0)
             << quint16(1) << quint16(32) << quint32(pngs[index].size()) << offset;
        offset += pngs[index].size();
    }
    for (const auto &bytes : pngs) data.writeRawData(bytes.constData(), bytes.size());
    return data.status() == QDataStream::Ok ? 0 : 6;
}
