// «Acerca de» de ForgeCraft, adaptado de la ventana FormAbout de Radit.
#include "aboutdialog.h"
#include "theme.h"
#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("FormAbout");
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_StyledBackground);
    setWindowTitle("Acerca de ForgeCraft");
    setWindowIcon(qApp->windowIcon());
    setMinimumSize(780, 320);
    resize(900, 350);
    auto *columns = new QHBoxLayout(this);
    columns->setContentsMargins(40, 28, 40, 26);
    columns->setSpacing(24);
    auto label = [this](const QString &text, const QString &name) {
        auto *item = new QLabel(text, this);
        item->setObjectName(name);
        item->setAlignment(Qt::AlignCenter);
        item->setWordWrap(true);
        return item;
    };

    auto *left = new QVBoxLayout; left->setSpacing(12); left->addStretch();
    left->addWidget(label("Editor de audio para Windows, con la identidad visual de Radit. WAV PCM y float, MP3, 37 procesos y grabación.", "AboutDescription"));
    left->addSpacing(10);
    left->addWidget(label("Desarrollado por", "AboutMuted"));
    left->addWidget(label("Victor Algaba", "AboutAuthor"));
    left->addStretch();
    auto *website = label("<a href=\"https://www.radit.org\" style=\"color:#80A4AE;text-decoration:none\">www.radit.org</a>", "AboutWebsite");
    website->setOpenExternalLinks(true);
    website->setToolTip("Radit: proyecto de origen del diseño y los iconos");
    left->addWidget(website);
    left->addWidget(label("Diseño e iconos basados en Radit", "AboutMuted"));
    columns->addLayout(left, 1);

    auto *center = new QVBoxLayout; center->setSpacing(5); center->addStretch();
    auto *logo = label({}, "AboutLogo");
    logo->setFixedSize(190, 180);
    logo->setAccessibleName("Logotipo de ForgeCraft: letra F");
    logo->setPixmap(QPixmap(QStringLiteral(":/icons/ForgeCraftAppIcon.png")).scaled(190, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    center->addWidget(logo, 0, Qt::AlignHCenter);
    center->addWidget(label("forgecraft", "AboutName"));
    center->addStretch();
    const QDateTime build = QFileInfo(QCoreApplication::applicationFilePath()).lastModified().toLocalTime();
    const QString revision = build.isValid() ? "rev. " + build.toString("yyyyMMdd-HHmmss") : QString();
    center->addWidget(label("Versión " + QCoreApplication::applicationVersion() + " " + revision, "AboutVersion"));
    columns->addLayout(center, 1);

    auto *right = new QVBoxLayout; right->setSpacing(8); right->addStretch();
    right->addSpacing(6);
    right->addWidget(label("Qt 6.7 · MinGW 11.2", "AboutMuted"));
    right->addWidget(label("mpg123 · LAME", "AboutCredits"));
    right->addSpacing(6);
    right->addWidget(label("GNU GPL v3 o posterior", "AboutCredits"));
    right->addSpacing(8);
    right->addWidget(label("Compilación: " + build.toString("dd/MM/yyyy HH:mm"), "AboutBuildDate"));
    right->addWidget(label("Esc para cerrar", "AboutMuted"));
    right->addStretch();
    right->addWidget(label("© " + QString::number(build.date().year()) + " Victor Algaba", "AboutCopyright"));
    columns->addLayout(right, 1);
}

void AboutDialog::showEvent(QShowEvent *event) {
    const QRect bounds = parentWidget() ? parentWidget()->window()->frameGeometry() : screen()->availableGeometry();
    move(bounds.center() - rect().center());
    QDialog::showEvent(event);
}

void AboutDialog::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) { close(); return; }
    QDialog::keyPressEvent(event);
}

// Línea decorativa de acento, como en la ventana de Radit.
void AboutDialog::paintEvent(QPaintEvent *event) {
    QDialog::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal middle = height() * 0.67;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(Theme::Accent.red(), Theme::Accent.green(), Theme::Accent.blue(), 32));
    painter.drawRect(QRectF(0, middle, width() * 0.36, 5));
    painter.setBrush(QColor(Theme::Accent.red(), Theme::Accent.green(), Theme::Accent.blue(), 14));
    painter.drawRect(QRectF(width() * 0.61, middle, width() * 0.39, 2));
    painter.setBrush(Theme::Accent);
    painter.drawRect(QRectF(0, middle, 30, 5));
    painter.drawPolygon(QPolygonF{QPointF(35, middle), QPointF(42, middle), QPointF(37, middle + 5), QPointF(30, middle + 5)});
}
