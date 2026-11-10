#include "newaudiodialog.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

// Capacidad máxima del almacenamiento temporal de audio.
static qint64 capacityFrames(int channels) { return (256LL * 1024 * 1024 * 1024 / qint64(sizeof(float))) / qMax(1, channels); }

static QString clockText(qint64 frames, int rate) {
    const qint64 ms = frames * 1000 / qMax(1, rate);
    return QString("%1:%2:%3").arg(ms / 3600000, 2, 10, QChar('0')).arg(ms / 60000 % 60, 2, 10, QChar('0')).arg(ms / 1000 % 60, 2, 10, QChar('0'));
}

NewAudioDialog::NewAudioDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("newAudioDialog");
    setWindowTitle("Documento nuevo");
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(14);
    auto *content = new QVBoxLayout; content->setSpacing(10); root->addLayout(content, 1);

    auto *heading = new QLabel("Configura el documento vacío. Después podrás grabarlo, pegar audio o guardarlo como WAV o MP3.", this);
    heading->setObjectName("processDescription"); heading->setWordWrap(true); heading->setMaximumWidth(460); content->addWidget(heading);

    auto *rateRow = new QHBoxLayout; rateRow->setSpacing(8);
    auto *rateLabel = new QLabel("Frecuencia de muestreo (8.000 a 192.000 Hz):", this); rateLabel->setObjectName("processParameter");
    rateBox = new QComboBox(this); rateBox->setObjectName("newRate");
    for (int rate : {8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000}) rateBox->addItem(QString::number(rate) + " Hz", rate);
    rateBox->setCurrentIndex(rateBox->findData(44100));
    rateRow->addWidget(rateLabel); rateRow->addWidget(rateBox, 1); content->addLayout(rateRow);

    auto *groups = new QHBoxLayout; groups->setSpacing(12); content->addLayout(groups);
    auto *depthBox = new QGroupBox("Profundidad", this); depthBox->setObjectName("newGroup");
    auto *depthLayout = new QVBoxLayout(depthBox); depthLayout->setSpacing(4);
    depthGroup = new QButtonGroup(this);
    const QVector<QPair<QString, int>> depths{{"8 bits", 8}, {"16 bits", 16}, {"24 bits", 24}, {"32 bits float", 32}};
    for (const auto &depth : depths) {
        auto *button = new QRadioButton(depth.first, depthBox);
        button->setObjectName(QString("newDepth%1").arg(depth.second));
        depthGroup->addButton(button, depth.second);
        depthLayout->addWidget(button);
        if (depth.second == 16) button->setChecked(true);
    }
    groups->addWidget(depthBox, 1);

    auto *channelBox = new QGroupBox("Canales", this); channelBox->setObjectName("newGroup");
    auto *channelLayout = new QVBoxLayout(channelBox); channelLayout->setSpacing(4);
    channelGroup = new QButtonGroup(this);
    auto *mono = new QRadioButton("Mono", channelBox); mono->setObjectName("newChannels1"); channelGroup->addButton(mono, 1); channelLayout->addWidget(mono);
    auto *stereo = new QRadioButton("Estéreo", channelBox); stereo->setObjectName("newChannels2"); channelGroup->addButton(stereo, 2); stereo->setChecked(true); channelLayout->addWidget(stereo);
    groups->addWidget(channelBox, 1);

    auto *timeRow = new QHBoxLayout; timeRow->setSpacing(8);
    auto *timeLabel = new QLabel("Tiempo máximo de edición:", this); timeLabel->setObjectName("processFooter");
    maxTime = new QLabel(this); maxTime->setObjectName("newMaxTime"); maxTime->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    timeRow->addWidget(timeLabel); timeRow->addWidget(maxTime, 1); content->addLayout(timeRow);

    auto *buttons = new QVBoxLayout; buttons->setSpacing(6); root->addLayout(buttons);
    auto *createButton = new QPushButton("Crear"); createButton->setObjectName("newCreate"); createButton->setDefault(true);
    auto *cancelButton = new QPushButton("Cancelar"); cancelButton->setObjectName("newCancel");
    auto *helpButton = new QPushButton("Ayuda"); helpButton->setObjectName("newHelp");
    buttons->addWidget(createButton); buttons->addWidget(cancelButton); buttons->addWidget(helpButton); buttons->addStretch();
    connect(createButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(helpButton, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, "Documento nuevo",
            "Frecuencia de muestreo: 44.100 Hz o 48.000 Hz son lo habitual; 96.000 y 192.000 Hz duplican o cuadruplican la memoria.\n\n"
            "Profundidad: 16 bits es el formato de la grabación y del guardado por omisión. 24 bits y 32 float se guardan en WAV con esa profundidad; el audio interno siempre es de 32 bits en coma flotante.\n\n"
            "Canales: mono o estéreo.\n\n"
            "El audio grande utiliza archivos temporales en disco. La capacidad real depende del espacio libre, con un máximo de 256 GB de muestras por versión.");
    });
    connect(rateBox, &QComboBox::currentIndexChanged, this, [this] { updateMaxTime(); });
    connect(channelGroup, &QButtonGroup::idToggled, this, [this] { updateMaxTime(); });
    updateMaxTime();
    setMinimumWidth(560);
}

int NewAudioDialog::rate() const { return rateBox->currentData().toInt(); }
int NewAudioDialog::channels() const { return channelGroup->checkedId(); }
int NewAudioDialog::bitDepth() const { return depthGroup->checkedId(); }
bool NewAudioDialog::floatFormat() const { return bitDepth() == 32; }

void NewAudioDialog::updateMaxTime() {
    const qint64 frames = capacityFrames(channels());
    maxTime->setText(clockText(frames, rate()));
}
