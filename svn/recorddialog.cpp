#include "recorddialog.h"
#include "mainwindow.h"
#include "theme.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QPainter>
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
    rateBox = new QComboBox; rateBox->setObjectName("recordRate"); for (int rate : {8000,16000,22050,32000,44100,48000,96000,192000}) rateBox->addItem(QString::number(rate) + " Hz", rate);
    if (rateBox->findData(targetRate) < 0) rateBox->addItem(QString::number(targetRate) + " Hz", targetRate);
    rateBox->setCurrentIndex(rateBox->findData(targetRate));
    channelsBox = new QComboBox; channelsBox->setObjectName("recordChannels"); channelsBox->addItem("Mono", 1); channelsBox->addItem("Estéreo", 2); channelsBox->setCurrentIndex(targetChannels - 1);
    auto *formatRow = new QHBoxLayout; formatRow->addWidget(rateBox); formatRow->addWidget(channelsBox); form->addRow("Formato PCM 16-bit:", formatRow);
    durationBox = new QDoubleSpinBox; durationBox->setObjectName("recordDuration"); durationBox->setDecimals(3); durationBox->setRange(0, 10000); durationBox->setSpecialValueText("Sin límite de tiempo"); durationBox->setSuffix(" s"); form->addRow("Duración máxima:", durationBox);
    meters = new LevelMeter; meters->setMinimumHeight(205); meters->setFixedWidth(68); body->addWidget(meters);
    auto *transport = new QHBoxLayout; transport->setSpacing(2); left->addLayout(transport);
    recordButton = new QPushButton("Grabar"); recordButton->setObjectName("recordStart");
    QPixmap dot(14,14); dot.fill(Qt::transparent); { QPainter p(&dot); p.setBrush(Theme::RecordDot); p.setPen(Theme::RecordDot.darker(150)); p.drawEllipse(2,2,9,9); } recordButton->setIcon(QIcon(dot)); recordButton->setShortcut(QKeySequence("Alt+R")); transport->addWidget(recordButton);
    pauseButton = new QPushButton("Pausa"); pauseButton->setObjectName("recordPause"); pauseButton->setIcon(Theme::icon("pause")); transport->addWidget(pauseButton);
    stopButton = new QPushButton("Detener"); stopButton->setObjectName("recordStop"); stopButton->setIcon(Theme::icon("stop")); transport->addWidget(stopButton);
    auto *timing = new QFormLayout; left->addLayout(timing); timeLabel = new QLabel; timeLabel->setObjectName("recordTime"); capacityLabel = new QLabel; stateLabel = new QLabel; stateLabel->setObjectName("recordState"); timing->addRow("Tiempo grabado:", timeLabel); timing->addRow("Tiempo disponible¹:", capacityLabel); timing->addRow("Estado:", stateLabel);
    auto *note = new QLabel("¹ Según el límite de 512 MB por toma en memoria.\nCada toma se añade al documento al pulsar «Detener»."); note->setWordWrap(true); left->addWidget(note);
    auto *close = new QPushButton("Cerrar"); close->setObjectName("recordClose"); right->addWidget(close); right->addStretch();
    for (auto *button : findChildren<QPushButton *>()) button->setAutoDefault(false);
    connect(newButton, &QPushButton::clicked, this, &RecordDialog::newTake);
    connect(recordButton, &QPushButton::clicked, this, &RecordDialog::startRecord);
    connect(pauseButton, &QPushButton::clicked, this, &RecordDialog::pauseRecord);
    connect(stopButton, &QPushButton::clicked, this, &RecordDialog::stopRecord);
    connect(close, &QPushButton::clicked, this, &RecordDialog::reject);
    connect(deviceBox, &QComboBox::currentIndexChanged, this, [this] { if (switching) return; if (state != Capturing && state != Paused) startMonitoring(); else updateUi(); });
    connect(durationBox, &QDoubleSpinBox::valueChanged, this, [this] { updateUi(); });
    auto formatChanged = [this] {
        if (switching) return;
        if (!recorder->buffer.audio.frames()) recorder->clear(rate(), channels());
        if (state != Capturing && state != Paused) startMonitoring(); else updateUi();
    };
    connect(rateBox, &QComboBox::currentIndexChanged, this, formatChanged); connect(channelsBox, &QComboBox::currentIndexChanged, this, formatChanged);
    connect(recorder, &Recorder::updated, this, &RecordDialog::updateUi);
    connect(recorder, &Recorder::limitReached, this, [this] { state = Stopped; updateUi(); stateLabel->setText("Límite alcanzado. Puedes usar la toma."); });
    connect(recorder, &Recorder::failed, this, [this](const QString &error) { state = Stopped; updateUi(); QMessageBox::warning(this, "Grabación", error); });
    recorder->clear(rate(), channels());
    // El ajuste DC y la monitorización van siempre activos: no hay que elegirlos.
    recorder->buffer.dcAdjust = true;
    startMonitoring();
}
int RecordDialog::rate() const { return rateBox->currentData().toInt(); }
int RecordDialog::channels() const { return channelsBox->currentData().toInt(); }
bool RecordDialog::openInput(bool record, bool report) {
    if (deviceBox->currentIndex() < 0) return false;
    recorder->buffer.dcAdjust = true;
    recorder->buffer.limitFrames = durationBox->value() > 0 ? qint64(std::llround(durationBox->value() * rate())) : 0;
    QString error;
    if (!recorder->start(deviceBox->currentData().toUInt(), rate(), channels(), record, error)) {
        state = Idle;
        if (report) QMessageBox::warning(this, "Grabación", error);
        updateUi();
        return false;
    }
    return true;
}

// La monitorización va siempre activa: la entrada se abre al abrir el diálogo y
// cada vez que cambia el dispositivo o el formato.
void RecordDialog::startMonitoring() {
    if (state == Capturing || !deviceBox->count()) { updateUi(); return; }
    if (openInput(false, false)) state = Prepared;
    updateUi();
}
void RecordDialog::startRecord() { if (state == Capturing) return; if (openInput(true)) { state = Capturing; updateUi(); } }
void RecordDialog::pauseRecord() {
    if (state == Paused) { startRecord(); return; }
    if (state != Capturing) return;
    recorder->stop(); state = Paused;
    if (deviceBox->count()) openInput(false, false);   // sigue mostrando niveles sin grabar
    updateUi();
}
void RecordDialog::stopCapture() { recorder->stop(); state = recorder->buffer.audio.frames() ? Stopped : Idle; }

// Entrega la toma al documento enfocado y deja el contador a cero para la
// siguiente; si el documento ya tenía audio, se añade al final.
bool RecordDialog::deliverTake() {
    if (!take().frames() || !applyTake) return false;
    if (!applyTake(take())) return false;
    recorder->clear(rate(), channels());
    state = Idle;
    return true;
}
void RecordDialog::stopRecord() {
    stopCapture();
    deliverTake();
    if (isVisible()) startMonitoring();
    updateUi();
}
void RecordDialog::newTake() {
    stopCapture();
    if (take().frames() && QMessageBox::question(this, "Nueva toma", "¿Descartar la toma actual y empezar otra?", QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) {
        if (isVisible()) startMonitoring();
        return;
    }
    recorder->clear(rate(), channels()); state = Idle;
    startMonitoring();
}
void RecordDialog::reject() {
    // Al cerrar no se pierde nada: lo grabado hasta ahora se añade al documento.
    stopCapture();
    deliverTake();
    QDialog::reject();
}
void RecordDialog::updateUi() {
    const bool recording = state == Capturing, paused = state == Paused;
    const bool hasAudio = recorder->buffer.audio.frames() > 0, running = recorder->active();
    const bool idle = !recording && !paused;
    deviceBox->setEnabled(idle);
    // Con audio en el documento destino el formato queda fijado al suyo.
    rateBox->setEnabled(idle && !hasAudio && targetFrames == 0); channelsBox->setEnabled(rateBox->isEnabled());
    durationBox->setEnabled(idle);
    recordButton->setEnabled(!recording && deviceBox->count()); recordButton->setText(recording ? "Grabando…" : hasAudio ? "Continuar" : "Grabar");
    pauseButton->setEnabled(recording || paused); pauseButton->setText(paused ? "Continuar" : "Pausa");
    stopButton->setEnabled(recording || paused);
    newButton->setEnabled(!recording && !paused);
    attributes->setText(QString("Atributos: %1 Hz; 16-bit; %2").arg(rate()).arg(channels() == 2 ? "Estéreo" : "Mono"));
    const auto &buffer = recorder->buffer;
    timeLabel->setText(clockText(buffer.audio.frames(), buffer.audio.rate));
    qint64 capacity = durationBox->value() > 0 ? qMin(qint64(durationBox->value() * rate()), qint64(512 * 1024 * 1024 / sizeof(float) / channels())) : qint64(512 * 1024 * 1024 / sizeof(float) / channels());
    capacityLabel->setText(clockText(qMax(qint64(0), capacity - buffer.audio.frames()), rate()));
    QString status;
    if (!deviceBox->count()) status = "No hay entradas de audio disponibles.";
    else if (recording) status = "● Grabando";
    else if (paused) status = "En pausa";
    else if (hasAudio) status = "Detenido · toma conservada";
    else if (running) status = "Escuchando la entrada";
    else status = "Listo";
    stateLabel->setText(status);
    meters->setLevels(running ? buffer.peaks[0] : 0, running ? buffer.peaks[1] : 0);
}
