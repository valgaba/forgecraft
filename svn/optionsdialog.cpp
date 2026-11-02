#include "optionsdialog.h"
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

// WAVE_MAPPER: el dispositivo predeterminado del sistema.
static const quint32 DefaultDevice = 0xffffffffu;

OptionsDialog::OptionsDialog(const QVector<QPair<quint32, QString>> &devices, quint32 current, QWidget *parent) : QDialog(parent) {
    setObjectName("optionsDialog");
    setWindowTitle("Opciones generales");
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(14);
    auto *content = new QVBoxLayout; content->setSpacing(10); root->addLayout(content, 1);

    auto *description = new QLabel("Elige por dónde suena la reproducción y la previsualización de los procesos. Los cambios se guardan al aceptar.", this);
    description->setObjectName("processDescription"); description->setWordWrap(true); description->setMaximumWidth(430);
    content->addWidget(description);

    auto *audio = new QGroupBox("Salida de audio", this); audio->setObjectName("optionsGroup");
    auto *audioLayout = new QVBoxLayout(audio); audioLayout->setSpacing(8);
    auto *deviceRow = new QHBoxLayout; deviceRow->setSpacing(8);
    auto *deviceLabel = new QLabel("Dispositivo:", this); deviceLabel->setObjectName("processParameter");
    deviceBox = new QComboBox(this); deviceBox->setObjectName("optionsOutput");
    deviceBox->addItem("Predeterminado del sistema", DefaultDevice);
    for (const auto &device : devices) deviceBox->addItem(device.second, device.first);
    const int index = deviceBox->findData(current);
    deviceBox->setCurrentIndex(index >= 0 ? index : 0);
    testButton = new QPushButton("Probar", this); testButton->setObjectName("optionsTest");
    testButton->setToolTip("Reproduce un tono corto por el dispositivo elegido");
    deviceRow->addWidget(deviceLabel); deviceRow->addWidget(deviceBox, 1); deviceRow->addWidget(testButton);
    audioLayout->addLayout(deviceRow);
    auto *hint = new QLabel("Si el dispositivo elegido no está disponible, se muestra el error al reproducir.", this);
    hint->setObjectName("processFooter"); hint->setWordWrap(true);
    audioLayout->addWidget(hint);
    content->addWidget(audio);
    content->addStretch();

    connect(testButton, &QPushButton::clicked, this, [this] {
        // Si el dispositivo no abre, la reproducción ya avisa con el código de error.
        if (testDevice) testDevice(outputDevice());
    });

    auto *buttons = new QVBoxLayout; buttons->setSpacing(6); root->addLayout(buttons);
    auto *accept = new QPushButton("Aceptar"); accept->setObjectName("optionsAccept"); accept->setDefault(true);
    auto *cancel = new QPushButton("Cancelar"); cancel->setObjectName("optionsCancel");
    auto *help = new QPushButton("Ayuda"); help->setObjectName("optionsHelp");
    buttons->addWidget(accept); buttons->addWidget(cancel); buttons->addWidget(help); buttons->addStretch();
    connect(accept, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(help, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, "Opciones generales",
            "Dispositivo de salida: tarjeta o interfaz por la que se reproduce el audio y la previsualización de los procesos. "
            "«Predeterminado del sistema» usa el dispositivo configurado en Windows.\n\n"
            "Probar reproduce medio segundo de un tono de 440 Hz por el dispositivo elegido, sin tocar el documento.\n\n"
            "El ajuste se guarda para las próximas sesiones.");
    });
    setMinimumWidth(560);
}

quint32 OptionsDialog::outputDevice() const { return deviceBox->currentData().toUInt(); }
