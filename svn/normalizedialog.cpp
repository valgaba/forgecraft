#include "normalizedialog.h"
#include "vumeter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>
#include <QRadioButton>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QPointer>
#include <QtConcurrent>
#include <cmath>

namespace {
QString dbText(double amplitude) { return amplitude > 0 ? QString::number(20 * std::log10(amplitude), 'f', 2) + " dBFS" : QString::fromUtf8("−Inf. dBFS"); }
QString timeText(qint64 frames, int rate) {
    const qint64 ms = frames * 1000 / rate;
    return QString("%1:%2:%3,%4").arg(ms / 3600000, 2, 10, QChar('0')).arg(ms / 60000 % 60, 2, 10, QChar('0')).arg(ms / 1000 % 60, 2, 10, QChar('0')).arg(ms % 1000, 3, 10, QChar('0'));
}
}

NormalizeDialog::NormalizeDialog(const AudioData &audio, qint64 a, qint64 b, const QString &name, QWidget *parent)
    : QDialog(parent), source(audio), first(a), last(b) {
    if (first == last) { first = 0; last = audio.frames(); }
    setObjectName("processDialog"); setProperty("normalizer", true);
    setWindowTitle("Normalizar — " + name);
    auto *root = new QHBoxLayout(this); root->setContentsMargins(12, 12, 12, 10); root->setSpacing(12);
    auto *content = new QVBoxLayout; content->setAlignment(Qt::AlignTop); root->addLayout(content, 1);
    presets = new QComboBox; presets->setObjectName("normalizePresets"); presets->setAccessibleName("Preset de normalización"); content->addWidget(presets);
    auto *panel = new QGroupBox("Normalización"); panel->setObjectName("normalizePanel"); content->addWidget(panel);
    auto *panelLayout = new QVBoxLayout(panel);
    auto *modeRow = new QHBoxLayout; modeRow->addWidget(new QLabel("Normalizar usando:"));
    peakMode = new QRadioButton("Nivel de pico"); peakMode->setObjectName("normalizePeak");
    rmsMode = new QRadioButton("Potencia RMS media"); rmsMode->setObjectName("normalizeRms");
    modeRow->addWidget(peakMode); modeRow->addWidget(rmsMode); panelLayout->addLayout(modeRow);
    auto *settingsRow = new QHBoxLayout; panelLayout->addLayout(settingsRow);
    auto column = [&](const QString &title, const QString &key, double minimum, double maximum, double initial) {
        auto *widget = new QWidget; auto *layout = new QVBoxLayout(widget); layout->setContentsMargins(4, 0, 4, 0);
        auto *spin = new QDoubleSpinBox; spin->setObjectName(key); spin->setRange(minimum, maximum); spin->setDecimals(2); spin->setSingleStep(.1); spin->setSuffix(" dB"); spin->setValue(initial);
        spin->setStyleSheet("min-width:90px;max-width:115px;");
        auto *percent = new QLabel; percent->setAlignment(Qt::AlignCenter);
        auto *slider = new QSlider(Qt::Vertical); slider->setRange(int(minimum * 10), int(maximum * 10)); slider->setValue(int(initial * 10)); slider->setMinimumHeight(125);
        auto *label = new QLabel(title); label->setAlignment(Qt::AlignCenter); label->setWordWrap(true);
        layout->addWidget(spin); layout->addWidget(percent); layout->addWidget(slider, 1, Qt::AlignHCenter); layout->addWidget(label);
        auto percentText = [=](double value) { percent->setText(QString("(%1 %)").arg(value <= -96 ? 0 : 100 * std::pow(10.0, value / 20.0), 0, 'f', 2)); };
        percentText(initial);
        connect(slider, &QSlider::valueChanged, spin, [spin](int value) { spin->setValue(value / 10.0); });
        connect(spin, &QDoubleSpinBox::valueChanged, slider, [slider, percentText](double value) { const QSignalBlocker block(slider); slider->setValue(int(std::lround(value * 10))); percentText(value); });
        return std::make_pair(widget, spin);
    };
    auto targetColumn = column("Normalizar a\n(−60 a 0 dB)", "target", -60, 0, -.1);
    target = targetColumn.second; settingsRow->addWidget(targetColumn.first);
    scanGroup = new QGroupBox("Ajustes de análisis"); scanGroup->setObjectName("normalizeScanGroup"); settingsRow->addWidget(scanGroup, 1);
    auto *scanLayout = new QHBoxLayout(scanGroup);
    auto ignoreColumn = column("Ignorar por debajo\n(−Inf. a 0 dB)", "ignore", -96, 0, -45);
    ignore = ignoreColumn.second; ignore->setSpecialValueText("−Inf."); scanLayout->addWidget(ignoreColumn.first);
    auto *scanOptions = new QFormLayout; scanLayout->addLayout(scanOptions);
    attack = new QSpinBox; attack->setObjectName("attack"); attack->setRange(1, 500); attack->setValue(200); attack->setSuffix(" ms");
    release = new QSpinBox; release->setObjectName("release"); release->setRange(1, 500); release->setValue(200); release->setSuffix(" ms");
    scanOptions->addRow("Ataque (1 a 500 ms):", attack); scanOptions->addRow("Relajación (1 a 500 ms):", release);
    contour = new QCheckBox("Usar contorno de igual sonoridad"); contour->setChecked(true); contour->setObjectName("normalizeContour");
    contour->setToolTip("Contorno perceptual aproximado: atenúa graves y realza presencia durante el análisis RMS. No mide LUFS ni reproduce un algoritmo propietario."); scanOptions->addRow(contour);
    auto *scanRow = new QHBoxLayout;
    auto *scanButton = new QPushButton("Analizar niveles"); scanButton->setObjectName("normalizeScan"); scanRow->addWidget(scanButton);
    scanRow->addWidget(new QLabel("Si se produce clipping:"));
    clipping = new QComboBox; clipping->setObjectName("clipping"); clipping->addItems({"Compresión dinámica (limitador)", "Reducir ganancia para evitarlo", "Saturar los picos", "Cancelar la operación"}); scanRow->addWidget(clipping, 1); content->addLayout(scanRow);
    auto *levelRow = new QHBoxLayout; auto *readings = new QVBoxLayout;
    peakValue = new QLabel("Pico: —"); peakValue->setObjectName("normalizePeakValue"); rmsValue = new QLabel("RMS: —"); rmsValue->setObjectName("normalizeRmsValue"); readings->addWidget(peakValue); readings->addWidget(rmsValue); levelRow->addLayout(readings);
    reuse = new QCheckBox("Usar el análisis actual (no volver a analizar)"); reuse->setObjectName("normalizeReuseScan"); reuse->setEnabled(false); levelRow->addWidget(reuse, 1); content->addLayout(levelRow);
    selection = new QLabel; selection->setObjectName("processFooter"); content->addWidget(selection);
    auto *channels = new QLabel(source.channels == 2 ? "Canales: ambos (L/R)" : "Canales: mono"); channels->setObjectName("processFooter"); content->addWidget(channels);
    auto *buttons = new QVBoxLayout; root->addLayout(buttons);
    auto makeButton = [&](const QString &text, const QString &object = QString()) { auto *button = new QPushButton(text); button->setObjectName(object); button->setAutoDefault(false); buttons->addWidget(button); return button; };
    auto *ok = makeButton("Aceptar", "processApply"); ok->setDefault(true);
    presets->setFixedHeight(ok->sizeHint().height());
    auto *cancel = makeButton("Cancelar", "processCancel");
    buttons->addSpacing(8);
    auto *save = makeButton("Guardar como…", "normalizeSavePreset"); deletePreset = makeButton("Eliminar preset", "normalizeDeletePreset"); buttons->addSpacing(8);
    preview = makeButton("Previsualizar", "processPreview"); preview->setCheckable(true);
    preview->setToolTip("Preescucha en bucle; los cambios se actualizan sin detener el audio");
    bypass = new QCheckBox("Bypass"); bypass->setObjectName("processBypass"); bypass->setEnabled(false); buttons->addWidget(bypass);
    previewStatus = new QLabel; previewStatus->setObjectName("normalizePreviewStatus"); previewStatus->setWordWrap(true); previewStatus->setMaximumWidth(130); buttons->addWidget(previewStatus); buttons->addStretch();
    auto *meterColumn = new QVBoxLayout; root->addLayout(meterColumn);
    auto *meterTitle = new QLabel("Preescucha"); meterTitle->setAlignment(Qt::AlignHCenter); meterColumn->addWidget(meterTitle);
    meters = new VuMeter(nullptr, Qt::Vertical); meters->setObjectName("normalizePreviewMeters"); meterColumn->addWidget(meters, 1);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept); connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    refreshTimer.setSingleShot(true); refreshTimer.setInterval(80);
    connect(&refreshTimer, &QTimer::timeout, this, &NormalizeDialog::rebuildPreview);
    connect(&previewWorker, &QFutureWatcher<PreviewWork>::finished, this, &NormalizeDialog::previewReady);
    connect(scanButton, &QPushButton::clicked, this, [this] { if (scan() && playing) requestPreviewUpdate(); });
    connect(preview, &QPushButton::toggled, this, &NormalizeDialog::listen);
    connect(bypass, &QCheckBox::toggled, this, [this] { if (playing) playCurrent(); });
    connect(reuse, &QCheckBox::clicked, this, [this] { if (playing) requestPreviewUpdate(); });
    connect(target, &QDoubleSpinBox::valueChanged, this, [this] { settingsChanged(false); });
    connect(ignore, &QDoubleSpinBox::valueChanged, this, [this] { settingsChanged(true); });
    connect(attack, &QSpinBox::valueChanged, this, [this] { settingsChanged(true); });
    connect(release, &QSpinBox::valueChanged, this, [this] { settingsChanged(true); });
    connect(contour, &QCheckBox::toggled, this, [this] { settingsChanged(true); });
    connect(clipping, &QComboBox::currentIndexChanged, this, [this] { settingsChanged(false); });
    connect(rmsMode, &QRadioButton::toggled, this, [this](bool rms) { scanGroup->setEnabled(rms); settingsChanged(false); });
    connect(presets, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (loading) return;
        auto v = Dsp::defaults("normalize");
        if (index >= 2 && index <= 5) { v["mode"] = 1; v["target"] = QList<double>{-12, -10, -16, -6}[index - 2]; }
        else if (index == 1) v["target"] = 0;
        else if (index > 5) { const auto map = QSettings().value("normalize/presets/" + presets->currentData().toString()).toMap(); for (auto i = map.cbegin(); i != map.cend(); ++i) v[i.key()] = i.value().toDouble(); }
        if (index) loadValues(v);
        deletePreset->setEnabled(index > 5);
    });
    connect(save, &QPushButton::clicked, this, [this] {
        bool accepted; const QString name = QInputDialog::getText(this, "Guardar preset", "Nombre:", QLineEdit::Normal, {}, &accepted).trimmed();
        if (!accepted || name.isEmpty() || name.contains('/') || name.contains('\\')) return;
        QVariantMap map; const auto v = values(); for (auto i = v.cbegin(); i != v.cend(); ++i) if (!i.key().startsWith("scan_")) map[i.key()] = i.value();
        QSettings settings; settings.setValue("normalize/presets/" + name, map); settings.sync();
        if (settings.status() != QSettings::NoError) { QMessageBox::warning(this, "Guardar preset", "No se pudo guardar el preset en la configuración."); return; }
        refreshPresets(); presets->setCurrentIndex(presets->findData(name));
    });
    connect(deletePreset, &QPushButton::clicked, this, [this] { if (presets->currentIndex() > 5) { QSettings().remove("normalize/presets/" + presets->currentData().toString()); refreshPresets(); } });
    refreshPresets(); loadValues(Dsp::defaults("normalize")); updateSelection();
    setMinimumWidth(850); resize(900, 450);
}

NormalizeDialog::~NormalizeDialog() { endPreview(); previewWorker.waitForFinished(); }
Dsp::Values NormalizeDialog::values() const {
    Dsp::Values v{{"target", target->value()}, {"mode", rmsMode->isChecked() ? 1.0 : 0.0}, {"ignore", ignore->value()}, {"attack", double(attack->value())}, {"release", double(release->value())}, {"contour", contour->isChecked() ? 1.0 : 0.0}, {"clipping", double(clipping->currentIndex())}};
    if (scanned && reuse->isChecked()) { v["scan_cached"] = 1; v["scan_peak"] = levels.peak; v["scan_rms"] = levels.rms; }
    return v;
}
void NormalizeDialog::settingsChanged(bool analysisChanged) {
    if (loading) return;
    if (analysisChanged) { scanned = false; reuse->setChecked(false); reuse->setEnabled(false); peakValue->setText("Pico: —"); rmsValue->setText("RMS: —"); }
    const QSignalBlocker block(presets); presets->setCurrentIndex(0); deletePreset->setEnabled(false);
    if (playing) requestPreviewUpdate();
}
void NormalizeDialog::loadValues(const Dsp::Values &v) {
    loading = true; target->setValue(v.value("target", -.1)); ignore->setValue(v.value("ignore", -45)); attack->setValue(int(v.value("attack", 200))); release->setValue(int(v.value("release", 200))); contour->setChecked(v.value("contour", 1)); clipping->setCurrentIndex(int(v.value("clipping")));
    peakMode->setChecked(!v.value("mode")); rmsMode->setChecked(v.value("mode")); scanGroup->setEnabled(rmsMode->isChecked()); loading = false;
    scanned = false; reuse->setChecked(false); reuse->setEnabled(false); peakValue->setText("Pico: —"); rmsValue->setText("RMS: —");
    if (playing) requestPreviewUpdate();
}
void NormalizeDialog::refreshPresets() {
    const QSignalBlocker block(presets); presets->clear(); presets->addItems({"(Sin título)", "Maximizar nivel de pico", "Normalizar RMS a −12 dB", "Normalizar RMS a −10 dB (voz)", "Normalizar RMS a −16 dB (música)", "Normalizar RMS a −6 dB (muy alto)"});
    QSettings settings; settings.beginGroup("normalize/presets"); for (const auto &key : settings.childKeys()) presets->addItem(key, key); deletePreset->setEnabled(false);
}
void NormalizeDialog::updateSelection() { selection->setText(QString("Selección: %1 → %2 (%3)").arg(timeText(first, source.rate), timeText(last, source.rate), timeText(last - first, source.rate))); }
bool NormalizeDialog::scan() {
    auto canceled = std::make_shared<std::atomic_bool>(false); const auto settings = values(); const AudioData audio = source; const qint64 a = first, b = last;
    QProgressDialog progress("Analizando pico y RMS…", "Cancelar", 0, 0, this); progress.setMinimumDuration(0); progress.setWindowModality(Qt::WindowModal); progress.setAutoClose(false);
    QFutureWatcher<Dsp::NormalizeLevels> watcher; connect(&watcher, &QFutureWatcher<Dsp::NormalizeLevels>::finished, &progress, &QDialog::accept); connect(&progress, &QProgressDialog::canceled, this, [canceled] { canceled->store(true); });
    watcher.setFuture(QtConcurrent::run([audio, a, b, settings, canceled] { return Dsp::scanNormalize(audio, a, b, settings, canceled.get()); }));
    if (!watcher.isFinished()) progress.exec();
    watcher.waitForFinished();
    const auto measured = watcher.result(); if (canceled->load() || measured.canceled) return false;
    levels = measured;
    scanned = true; reuse->setEnabled(true); reuse->setChecked(true); peakValue->setText("Pico: " + dbText(levels.peak)); rmsValue->setText("RMS: " + dbText(levels.rms)); return true;
}
void NormalizeDialog::listen(bool on) {
    if (!on) { endPreview(); return; }
    if ((!scanned || !reuse->isChecked()) && !scan()) { endPreview(); return; }
    auto v = values(); v["scan_cached"] = 1; v["scan_peak"] = levels.peak; v["scan_rms"] = levels.rms;
    Dsp::Result result;
    try { dry = source.fragment(first, qMin(last, first + 15 * qint64(source.rate))); result = Dsp::apply(dry, 0, dry.frames(), "normalize", v); }
    catch (const std::bad_alloc &) { QMessageBox::warning(this, "Previsualizar", "No hay memoria suficiente para la preescucha."); endPreview(); return; }
    if (!result.error.isEmpty()) { QMessageBox::warning(this, "Previsualizar", result.error); endPreview(); return; }
    wet = std::move(result.audio); playing = true; preview->setText("Detener preescucha"); bypass->setEnabled(true); playCurrent();
}
void NormalizeDialog::playCurrent() {
    if (!playing) return;
    const QPointer<NormalizeDialog> self(this);
    if (!previewAudio || !previewAudio(bypass->isChecked() ? dry : wet, [self] { if (self) self->endPreview(); })) endPreview();
    else previewStatus->setText(bypass->isChecked() ? "Bypass · en bucle" : "En bucle");
}
void NormalizeDialog::endPreview() {
    const bool wasPlaying = playing;
    playing = false;
    refreshTimer.stop(); pendingPreview = false; ++revision;
    if (refreshCancel) refreshCancel->store(true);
    if (wasPlaying && stopAudio) stopAudio();
    const QSignalBlocker p(preview), b(bypass); preview->setChecked(false); bypass->setChecked(false); bypass->setEnabled(false);
    preview->setText("Previsualizar"); previewStatus->clear();
    if (meters) meters->reset();
}
void NormalizeDialog::requestPreviewUpdate() {
    if (!playing) return;
    ++revision; pendingPreview = true;
    if (refreshCancel) refreshCancel->store(true);
    previewStatus->setText("Actualizando…"); refreshTimer.start();
}
void NormalizeDialog::rebuildPreview() {
    if (!playing || !pendingPreview || previewWorker.isRunning()) return;
    pendingPreview = false;
    const auto parameters = values(); const AudioData audio = source, fragment = dry;
    const qint64 a = first, b = last; const quint64 version = revision;
    refreshCancel = std::make_shared<std::atomic_bool>(false); const auto cancel = refreshCancel;
    previewWorker.setFuture(QtConcurrent::run([audio, fragment, a, b, version, parameters, cancel] {
        PreviewWork work; work.revision = version;
        try {
            if (parameters.value("scan_cached")) { work.levels.peak = parameters.value("scan_peak"); work.levels.rms = parameters.value("scan_rms"); }
            else work.levels = Dsp::scanNormalize(audio, a, b, parameters, cancel.get());
            if (cancel->load() || work.levels.canceled) { work.canceled = true; return work; }
            auto v = parameters; v["scan_cached"] = 1; v["scan_peak"] = work.levels.peak; v["scan_rms"] = work.levels.rms;
            auto result = Dsp::apply(fragment, 0, fragment.frames(), "normalize", v, cancel.get());
            work.audio = std::move(result.audio); work.error = result.error; work.canceled = result.canceled || cancel->load();
        } catch (const std::bad_alloc &) { work.error = "No hay memoria suficiente para actualizar la preescucha."; }
        return work;
    }));
}
void NormalizeDialog::previewReady() {
    const auto work = previewWorker.result();
    if (playing && work.revision == revision && !work.canceled) {
        if (!work.error.isEmpty()) { previewStatus->setText("Ajustes no válidos"); previewStatus->setToolTip(work.error); }
        else {
            wet = work.audio; levels = work.levels; scanned = true; reuse->setEnabled(true); reuse->setChecked(true);
            peakValue->setText("Pico: " + dbText(levels.peak)); rmsValue->setText("RMS: " + dbText(levels.rms));
            if (!bypass->isChecked()) playCurrent(); else previewStatus->setText("Bypass · en bucle");
        }
    }
    if (playing && pendingPreview && !refreshTimer.isActive()) refreshTimer.start(0);
}
