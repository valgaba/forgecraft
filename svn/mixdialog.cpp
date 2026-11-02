#include "mixdialog.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <cmath>

namespace {
// Valores en décimas de dB: -60 dB (silencio) a +15 dB.
constexpr int Silence = -600;
constexpr int MaxDb = 150;
double gainFromTenths(int tenths) { return tenths <= -600 ? 0.0 : std::pow(10.0, tenths / 200.0); }
QString textFromTenths(int tenths) { return tenths <= -600 ? QStringLiteral("−∞ dB") : QString::number(tenths / 10.0, 'f', 1) + " dB"; }
QString clockText(qint64 frames, int rate) {
    const qint64 ms = frames * 1000 / qMax(1, rate);
    return QString("%1:%2:%3").arg(ms / 3600000, 2, 10, QChar('0')).arg(ms / 60000 % 60, 2, 10, QChar('0')).arg(ms / 1000 % 60, 2, 10, QChar('0'));
}
}

MixDialog::MixDialog(qint64 fragmentFrames, int rate, QWidget *parent) : QDialog(parent), sampleRate(rate) {
    setObjectName("mixDialog");
    setWindowTitle("Mezclar selección");
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(14);
    auto *content = new QVBoxLayout; content->setSpacing(10); root->addLayout(content, 1);

    auto *description = new QLabel("El fragmento se suma al audio del documento de destino desde el punto donde lo sueltas. Ajusta el volumen de cada parte, desplázalo si hace falta y escucha el resultado con Previsualizar.", this);
    description->setObjectName("processDescription"); description->setWordWrap(true); description->setMaximumWidth(470);
    content->addWidget(description);

    auto *nameRow = new QHBoxLayout; nameRow->setSpacing(8);
    auto *nameLabel = new QLabel("Nombre:", this); nameLabel->setObjectName("processParameter");
    presetBox = new QComboBox(this); presetBox->setObjectName("mixPreset");
    presetBox->addItem("Sin nombre", QPoint(0, 0));
    presetBox->addItem("50/50 (igual volumen)", QPoint(-60, -60));
    presetBox->addItem("Normal (sin fundidos)", QPoint(0, 0));
    presetBox->addItem("Voz sobre mezcla", QPoint(0, -120));
    presetBox->addItem("Instrumento sobre mezcla", QPoint(-120, 0));
    nameRow->addWidget(nameLabel); nameRow->addWidget(presetBox, 1); content->addLayout(nameRow);
    connect(presetBox, &QComboBox::currentIndexChanged, this, [this](int index) { applyPreset(index); });

    // Dos paneles con slider vertical, como el Mix de Sound Forge.
    auto *panels = new QHBoxLayout; panels->setSpacing(10); content->addLayout(panels);
    auto panel = [this, panels](const QString &title, const QString &name, int value, QSlider *&slider, QLabel *&label) {
        auto *box = new QGroupBox(title, this); box->setObjectName("mixPanel");
        auto *layout = new QVBoxLayout(box); layout->setContentsMargins(10, 8, 10, 8); layout->setSpacing(6);
        label = new QLabel(this); label->setObjectName("mixValue"); label->setAlignment(Qt::AlignCenter);
        slider = new QSlider(Qt::Vertical, this); slider->setObjectName(name);
        slider->setRange(Silence, MaxDb); slider->setValue(value);
        slider->setFixedHeight(118); slider->setMinimumWidth(34);
        auto *caption = new QLabel("Volumen", this); caption->setObjectName("processCaption"); caption->setAlignment(Qt::AlignCenter);
        layout->addWidget(label); layout->addWidget(slider, 0, Qt::AlignHCenter); layout->addWidget(caption);
        panels->addWidget(box, 1);
        connect(slider, &QSlider::valueChanged, this, [this] { updateLabels(); });
    };
    panel("Origen", "mixSource", 0, sourceSlider, sourceValue);
    panel("Destino", "mixTarget", 0, targetSlider, targetValue);

    auto *footer = new QHBoxLayout; footer->setSpacing(10); content->addLayout(footer);
    auto *tuneLabel = new QLabel("Desplazamiento:", this); tuneLabel->setObjectName("processParameter");
    tuneBox = new QDoubleSpinBox(this); tuneBox->setObjectName("mixTune"); tuneBox->setDecimals(0); tuneBox->setRange(-500, 500);
    tuneBox->setSingleStep(10); tuneBox->setSuffix(" ms"); tuneBox->setFixedWidth(92);
    auto *lengthLabel = new QLabel("Duración:", this); lengthLabel->setObjectName("processFooter");
    auto *length = new QLabel(clockText(fragmentFrames, rate), this); length->setObjectName("processFooter");
    footer->addWidget(tuneLabel); footer->addWidget(tuneBox); footer->addSpacing(14);
    footer->addWidget(lengthLabel); footer->addWidget(length); footer->addStretch();
    content->addLayout(footer);

    auto *buttons = new QVBoxLayout; buttons->setSpacing(6); root->addLayout(buttons);
    previewButton = new QPushButton("Previsualizar"); previewButton->setObjectName("mixPreview"); previewButton->setCheckable(true);
    previewButton->setToolTip("Reproduce la mezcla con los volúmenes actuales, sin aplicarla");
    auto *apply = new QPushButton("Aplicar"); apply->setObjectName("mixApply"); apply->setDefault(true);
    auto *cancel = new QPushButton("Cancelar"); cancel->setObjectName("mixCancel");
    auto *help = new QPushButton("Ayuda"); help->setObjectName("mixHelp");
    buttons->addWidget(previewButton); buttons->addWidget(apply); buttons->addWidget(cancel); buttons->addWidget(help); buttons->addStretch();
    connect(previewButton, &QPushButton::toggled, this, [this](bool on) {
        if (!on) { stopListening(); return; }
        if (!preview) { QSignalBlocker block(previewButton); previewButton->setChecked(false); return; }
        const bool started = preview(sourceGain(), targetGain(), tuneFrames(), [this] {
            QSignalBlocker block(previewButton); previewButton->setChecked(false);
        });
        if (!started) { QSignalBlocker block(previewButton); previewButton->setChecked(false); }
    });
    connect(apply, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(help, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, "Mezclar selección",
            "El fragmento se suma al audio que ya hay en el documento de destino, desde el punto donde lo sueltas. Si el fragmento sobrepasa el final, el documento se amplía.\n\n"
            "Volumen del origen: nivel con el que entra lo arrastrado (de silencio a +15 dB).\n"
            "Volumen del destino: nivel del audio que ya había en esa zona; bajarlo permite que el fragmento destaque, como en un «voice over».\n\n"
            "Desplazamiento: mueve el fragmento hacia atrás o hacia delante hasta medio segundo para ajustarlo al pulso o al inicio de una frase.\n\n"
            "Previsualizar reproduce la mezcla sin aplicarla; al mover un volumen o cerrar el cuadro se detiene. Cada mezcla es una sola operación de deshacer.");
    });
    // Cualquier cambio deja la escucha anterior sin validez.
    connect(tuneBox, &QDoubleSpinBox::valueChanged, this, [this] { if (previewButton->isChecked()) previewButton->setChecked(false); });
    updateLabels();
    setMinimumWidth(640);
}

double MixDialog::sourceGain() const { return gainFromTenths(sourceSlider->value()); }
double MixDialog::targetGain() const { return gainFromTenths(targetSlider->value()); }
qint64 MixDialog::tuneFrames() const { return qint64(std::llround(tuneBox->value() / 1000.0 * sampleRate)); }

void MixDialog::done(int result) {
    stopListening();
    QDialog::done(result);
}

void MixDialog::stopListening() {
    if (previewButton && previewButton->isChecked()) { QSignalBlocker block(previewButton); previewButton->setChecked(false); }
    if (stopPreview) stopPreview();
}

void MixDialog::updateLabels() {
    sourceValue->setText(textFromTenths(sourceSlider->value()));
    targetValue->setText(textFromTenths(targetSlider->value()));
    if (previewButton && previewButton->isChecked()) previewButton->setChecked(false);
}

void MixDialog::applyPreset(int index) {
    const QPoint values = presetBox->itemData(index).toPoint();
    sourceSlider->setValue(values.x());
    targetSlider->setValue(values.y());
    updateLabels();
}
