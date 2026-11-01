#include "recorddialog.h"
#include "mainwindow.h"
#include "theme.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QPainter>
#include <QDesktopServices>
#include <QUrl>
#include <QStyle>
#include <cmath>

static QString clockText(qint64 frames, int rate) {
    qint64 ms = frames * 1000 / rate;
    return QString("%1:%2:%3,%4").arg(ms / 3600000, 2, 10, QChar('0')).arg(ms / 60000 % 60, 2, 10, QChar('0')).arg(ms / 1000 % 60, 2, 10, QChar('0')).arg(ms % 1000, 3, 10, QChar('0'));
}
RecordDialog::RecordDialog(const AudioData &destination, qint64 first, qint64 last, QWidget *parent, Recorder *source)
    : QDialog(parent), recorder(source ? source : new Recorder), targetRate(destination.rate), targetChannels(destination.channels), targetFrames(destination.frames()), targetFirst(first), targetEnd(last) {
    recorder->setParent(this); setObjectName("recordDialog"); setWindowTitle("Grabar — ForgeCraft"); setMinimumWidth(610);
    auto *root = new QHBoxLayout(this); auto *left = new QVBoxLayout; root->addLayout(left, 1); auto *right = new QVBoxLayout; root->addLayout(right);
    auto *heading = new QHBoxLayout; attributes = new QLabel; heading->addWidget(attributes, 1); newButton = new QPushButton("Nueva toma…"); newButton->setObjectName("recordNew"); heading->addWidget(newButton); left->addLayout(heading);
    auto *line = new QFrame; line->setFrameStyle(QFrame::HLine | QFrame::Sunken); left->addWidget(line);
    auto *body = new QHBoxLayout; left->addLayout(body); auto *form = new QFormLayout; body->addLayout(form, 1);
    deviceBox = new QComboBox; deviceBox->setObjectName("recordDevice"); deviceBox->setMinimumWidth(240);
    for (const auto &device : Recorder::devices()) deviceBox->addItem(device.name, device.id);
    if (source && !deviceBox->count()) deviceBox->addItem("Fuente de prueba", 0u);
    form->addRow("Entrada:", deviceBox);
    modeBox = new QComboBox; modeBox->setObjectName("recordMode"); modeBox->addItems({"Toma en ventana nueva", "Insertar en el cursor", "Sustituir la selección"}); form->addRow("Modo:", modeBox);
    rateBox = new QComboBox; rateBox->setObjectName("recordRate"); for (int rate : {8000,16000,22050,32000,44100,48000,96000,192000}) rateBox->addItem(QString::number(rate) + " Hz", rate);
    if (rateBox->findData(targetRate) < 0) rateBox->addItem(QString::number(targetRate) + " Hz", targetRate);
    rateBox->setCurrentIndex(rateBox->findData(targetRate));
    channelsBox = new QComboBox; channelsBox->setObjectName("recordChannels"); channelsBox->addItem("Mono", 1); channelsBox->addItem("Estéreo", 2); channelsBox->setCurrentIndex(targetChannels - 1);
    auto *formatRow = new QHBoxLayout; formatRow->addWidget(rateBox); formatRow->addWidget(channelsBox); form->addRow("Formato PCM 16-bit:", formatRow);
    positionBox = new QDoubleSpinBox; positionBox->setObjectName("recordPosition"); positionBox->setDecimals(3); positionBox->setRange(0, destination.duration()); positionBox->setSuffix(" s"); positionBox->setValue(double(first) / targetRate); form->addRow("Inicio en documento:", positionBox);
    durationBox = new QDoubleSpinBox; durationBox->setObjectName("recordDuration"); durationBox->setDecimals(3); durationBox->setRange(0, 10000); durationBox->setSpecialValueText("Sin límite de tiempo"); durationBox->setSuffix(" s"); form->addRow("Duración máxima:", durationBox);
    monitorBox = new QCheckBox("Monitorizar entrada (medidores)"); monitorBox->setObjectName("recordMonitor"); monitorBox->setToolTip("Muestra niveles sin grabar. No envía el micrófono a los altavoces."); form->addRow(monitorBox);
    dcBox = new QCheckBox("DC adjust · filtro a 20 Hz"); dcBox->setObjectName("recordDc"); form->addRow(dcBox);
    meters = new LevelMeter; meters->setMinimumHeight(205); meters->setFixedWidth(68); body->addWidget(meters);
    auto *transport = new QHBoxLayout; transport->setSpacing(2); left->addLayout(transport);
    prepareButton = new QPushButton("Preparar"); prepareButton->setObjectName("recordPrepare"); transport->addWidget(prepareButton);
    recordButton = new QPushButton("Grabar"); recordButton->setObjectName("recordStart");
    QPixmap dot(14,14); dot.fill(Qt::transparent); { QPainter p(&dot); p.setBrush(Theme::RecordDot); p.setPen(Theme::RecordDot.darker(150)); p.drawEllipse(2,2,9,9); } recordButton->setIcon(QIcon(dot)); recordButton->setShortcut(QKeySequence("Alt+R")); transport->addWidget(recordButton);
    pauseButton = new QPushButton("Pausa"); pauseButton->setObjectName("recordPause"); pauseButton->setIcon(Theme::icon("pause")); transport->addWidget(pauseButton);
    stopButton = new QPushButton("Detener"); stopButton->setObjectName("recordStop"); stopButton->setIcon(Theme::icon("stop")); transport->addWidget(stopButton);
    auto *timing = new QFormLayout; left->addLayout(timing); timeLabel = new QLabel; timeLabel->setObjectName("recordTime"); capacityLabel = new QLabel; stateLabel = new QLabel; stateLabel->setObjectName("recordState"); timing->addRow("Tiempo grabado:", timeLabel); timing->addRow("Tiempo disponible¹:", capacityLabel); timing->addRow("Estado:", stateLabel);
    auto *note = new QLabel("¹ Según el límite de 512 MB por toma en memoria.\nLa toma no se añade al editor hasta pulsar «Usar toma»."); note->setWordWrap(true); left->addWidget(note);
    useButton = new QPushButton("Usar toma"); useButton->setObjectName("recordUse"); right->addWidget(useButton);
    auto *close = new QPushButton("Cerrar"); close->setObjectName("recordClose"); right->addWidget(close);
    auto *settings = new QPushButton("Entradas…"); right->addWidget(settings);
    auto *help = new QPushButton("Ayuda"); right->addWidget(help); right->addStretch();
    for (auto *button : findChildren<QPushButton *>()) button->setAutoDefault(false);
    connect(newButton, &QPushButton::clicked, this, &RecordDialog::newTake);
    connect(prepareButton, &QPushButton::clicked, this, &RecordDialog::prepare);
    connect(recordButton, &QPushButton::clicked, this, &RecordDialog::startRecord);
    connect(pauseButton, &QPushButton::clicked, this, &RecordDialog::pauseRecord);
    connect(stopButton, &QPushButton::clicked, this, &RecordDialog::stopRecord);
    connect(useButton, &QPushButton::clicked, this, &RecordDialog::accept); connect(close, &QPushButton::clicked, this, &RecordDialog::reject);
    connect(settings, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl("ms-settings:sound")); });
    connect(help, &QPushButton::clicked, this, [this] { QMessageBox::information(this, "Grabación", "Selecciona una entrada y pulsa Preparar para ver sus niveles, o Grabar para iniciar la toma. Pausa y Continuar no añaden el tiempo de pausa. Detener conserva la toma; Grabar vuelve a continuarla. Nueva toma descarta la anterior tras confirmación. Usar toma la añade al editor y permite deshacer.\n\nSi Windows deniega la entrada, revisa los permisos del micrófono para aplicaciones de escritorio.\n\nNo incluye grabación remota, sincronización externa ni creación automática de regiones."); });
    connect(modeBox, &QComboBox::currentIndexChanged, this, &RecordDialog::changeMode);
    connect(monitorBox, &QCheckBox::toggled, this, [this](bool enabled) { if (switching || state == Capturing) return; if (enabled) prepare(); else stopRecord(); });
    connect(dcBox, &QCheckBox::toggled, this, [this](bool enabled) { recorder->buffer.dcAdjust = enabled; });
    connect(durationBox, &QDoubleSpinBox::valueChanged, this, [this] { updateUi(); });
    auto formatChanged = [this] { if (switching) return; if (!recorder->buffer.audio.frames()) recorder->clear(rate(), channels()); updateUi(); };
    connect(rateBox, &QComboBox::currentIndexChanged, this, formatChanged); connect(channelsBox, &QComboBox::currentIndexChanged, this, formatChanged);
    connect(recorder, &Recorder::updated, this, &RecordDialog::updateUi);
    connect(recorder, &Recorder::limitReached, this, [this] { state = Stopped; updateUi(); stateLabel->setText("Límite alcanzado. Puedes usar la toma."); });
    connect(recorder, &Recorder::failed, this, [this](const QString &error) { state = Stopped; updateUi(); QMessageBox::warning(this, "Grabación", error); });
    recorder->clear(rate(), channels()); changeMode();
}
int RecordDialog::rate() const { return rateBox->currentData().toInt(); }
int RecordDialog::channels() const { return channelsBox->currentData().toInt(); }
Recording::Mode RecordDialog::mode() const { return Recording::Mode(modeBox->currentIndex()); }
qint64 RecordDialog::position() const { return mode() == Recording::ReplaceSelection ? targetFirst : qint64(std::llround(positionBox->value() * targetRate)); }
void RecordDialog::changeMode() {
    if (mode() != Recording::NewDocument && !recorder->buffer.audio.frames() && !recorder->active()) {
        switching = true; rateBox->setCurrentIndex(rateBox->findData(targetRate)); channelsBox->setCurrentIndex(targetChannels - 1); switching = false; recorder->clear(rate(), channels());
    }
    updateUi();
}
bool RecordDialog::openInput(bool record) {
    if (deviceBox->currentIndex() < 0) return false;
    recorder->buffer.dcAdjust = dcBox->isChecked();
    recorder->buffer.limitFrames = durationBox->value() > 0 ? qint64(std::llround(durationBox->value() * rate())) : 0;
    QString error;
    if (!recorder->start(deviceBox->currentData().toUInt(), rate(), channels(), record, error)) {
        state = Stopped; updateUi(); QMessageBox::warning(this, "Grabación", error); return false;
    }
    return true;
}
void RecordDialog::prepare() { if (state == Capturing) return; if (openInput(false)) { state = Prepared; QSignalBlocker block(monitorBox); monitorBox->setChecked(true); updateUi(); } }
void RecordDialog::startRecord() { if (state == Capturing) return; if (openInput(true)) { state = Capturing; updateUi(); } }
void RecordDialog::pauseRecord() {
    if (state == Paused) { startRecord(); return; }
    if (state != Capturing) return;
    recorder->stop(); state = Paused;
    if (monitorBox->isChecked()) openInput(false);
    updateUi();
}
void RecordDialog::stopRecord() { recorder->stop(); state = recorder->buffer.audio.frames() ? Stopped : Idle; { QSignalBlocker block(monitorBox); monitorBox->setChecked(false); } updateUi(); }
void RecordDialog::newTake() {
    stopRecord();
    if (take().frames() && QMessageBox::question(this, "Nueva toma", "¿Descartar la toma actual y empezar otra?", QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) return;
    recorder->clear(rate(), channels()); state = Idle; updateUi();
}
void RecordDialog::accept() { stopRecord(); if (take().frames()) QDialog::accept(); }
void RecordDialog::reject() {
    stopRecord();
    if (take().frames()) {
        auto result = QMessageBox::question(this, "Toma sin añadir", "¿Añadir la toma al editor antes de cerrar?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (result == QMessageBox::Cancel) return;
        if (result == QMessageBox::Save) { QDialog::accept(); return; }
    }
    QDialog::reject();
}
void RecordDialog::updateUi() {
    bool recording = state == Capturing, hasAudio = recorder->buffer.audio.frames() > 0, running = recorder->active();
    deviceBox->setEnabled(!running); modeBox->setEnabled(!running && !hasAudio);
    rateBox->setEnabled(!running && !hasAudio && mode() == Recording::NewDocument); channelsBox->setEnabled(rateBox->isEnabled());
    positionBox->setEnabled(!running && mode() == Recording::Insert); durationBox->setEnabled(!running);
    recordButton->setEnabled(!recording && deviceBox->count()); recordButton->setText(recording ? "Grabando…" : hasAudio ? "Continuar" : "Grabar");
    prepareButton->setEnabled(!recording && deviceBox->count()); pauseButton->setEnabled(recording || state == Paused); pauseButton->setText(state == Paused ? "Continuar" : "Pausa"); stopButton->setEnabled(running || state == Paused); useButton->setEnabled(hasAudio); newButton->setEnabled(!recording);
    attributes->setText(QString("Atributos: %1 Hz; 16-bit; %2").arg(rate()).arg(channels() == 2 ? "Estéreo" : "Mono"));
    const auto &buffer = recorder->buffer;
    timeLabel->setText(clockText(buffer.audio.frames(), buffer.audio.rate));
    qint64 capacity = durationBox->value() > 0 ? qMin(qint64(durationBox->value() * rate()), qint64(512 * 1024 * 1024 / sizeof(float) / channels())) : qint64(512 * 1024 * 1024 / sizeof(float) / channels());
    capacityLabel->setText(clockText(qMax(qint64(0), capacity - buffer.audio.frames()), rate()));
    QStringList states{"Listo", "Preparado · monitorizando", "● Grabando", "En pausa", "Detenido · toma conservada"};
    stateLabel->setText(deviceBox->count() ? states[int(state)] : "No hay entradas de audio disponibles.");
    meters->setLevels(running ? buffer.peaks[0] : 0, running ? buffer.peaks[1] : 0);
}
