#include "audiofiledialogs.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>

namespace {
QComboBox *encoding(QFormLayout *layout) {
    auto *box = new QComboBox;
    for (const auto &value : {"s16le", "s24le", "s32le", "f32le", "s16be", "s24be", "s32be", "f32be", "u8", "s8"}) box->addItem(value, value);
    layout->addRow("Codificación PCM:", box); return box;
}
QSpinBox *number(QFormLayout *layout, const QString &label, int minimum, int maximum, int value) {
    auto *box = new QSpinBox; box->setRange(minimum, maximum); box->setValue(value); layout->addRow(label, box); return box;
}
bool accept(QDialog &dialog, QFormLayout *layout) {
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons); return dialog.exec() == QDialog::Accepted;
}
}
bool chooseRawAudio(QWidget *parent, RawAudioOptions &options) {
    QDialog dialog(parent); dialog.setObjectName("rawAudioDialog"); dialog.setWindowTitle("Importar audio RAW / PCM"); auto *layout = new QFormLayout(&dialog);
    auto *format = encoding(layout); format->setCurrentText(options.encoding);
    auto *rate = number(layout, "Frecuencia (Hz):", 8000, 384000, options.rate);
    auto *channels = number(layout, "Canales:", 1, 2, options.channels);
    if (!accept(dialog, layout)) return false;
    options.encoding = format->currentData().toString(); options.rate = rate->value(); options.channels = channels->value(); return true;
}
bool chooseAudioExport(QWidget *parent, const AudioFormats::Format &format, const AudioData &audio, AudioExportOptions &options) {
    QDialog dialog(parent); dialog.setObjectName("audioExportDialog"); dialog.setWindowTitle("Guardar " + format.title); auto *layout = new QFormLayout(&dialog);
    auto *rate = number(layout, "Frecuencia (Hz):", 8000, 384000, audio.rate);
    auto *channels = number(layout, "Canales:", 1, 2, audio.channels);
    if (format.id == "amr") { rate->setValue(8000); channels->setValue(1); rate->setEnabled(false); channels->setEnabled(false); }
    if (format.id == "opus" || format.id == "webm" || format.id == "ac3") { rate->setValue(48000); rate->setEnabled(false); }
    // Estos codificadores admiten un conjunto discreto de frecuencias.
    if (format.id == "wma" || format.id == "mp2" || format.id == "ogg" || format.id == "m4a" || format.id == "aac") {
        rate->setValue(44100); rate->setEnabled(false);
    }
    QSpinBox *compression = nullptr; QComboBox *bitrate = nullptr, *pcm = nullptr, *depth = nullptr;
    if (format.lossy) {
        bitrate = new QComboBox;
        if (format.id == "amr") { bitrate->addItem("12,2 kbps", 12); bitrate->setEnabled(false); }
        else {
            for (int value : {64, 96, 128, 160, 192, 224, 256, 320}) bitrate->addItem(QString::number(value) + " kbps", value);
            if (format.id == "ac3") for (int value : {384, 448, 512, 576, 640}) bitrate->addItem(QString::number(value) + " kbps", value);
            bitrate->setCurrentIndex(qMax(0, bitrate->findData(options.bitrate)));
        }
        layout->addRow("Bitrate:", bitrate);
    } else if (format.id == "raw") pcm = encoding(layout);
    else {
        depth = new QComboBox;
        depth->addItem("16 bits PCM", 16); depth->addItem("24 bits PCM", 24);
        if (format.encoder.isEmpty() || format.id == "wv") { depth->addItem("32 bits PCM", 32); depth->addItem("32 bits float", 0); }
        depth->setCurrentIndex(qMax(0, depth->findData(audio.floatFormat ? 0 : audio.bitDepth)));
        layout->addRow("Codificación:", depth);
        if (!format.encoder.isEmpty()) compression = number(layout, "Compresión:", 0, format.id == "wv" ? 8 : 12, options.compression);
    }
    if (!accept(dialog, layout)) return false;
    options.rate = rate->value(); options.channels = channels->value();
    if (bitrate) options.bitrate = bitrate->currentData().toInt();
    if (compression) options.compression = compression->value();
    if (pcm) options.rawEncoding = pcm->currentData().toString();
    if (depth) { options.bits = depth->currentData().toInt(); options.floating = options.bits == 0; if (options.floating) options.bits = 32; }
    return true;
}
