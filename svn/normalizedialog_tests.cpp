#include "normalizedialog.h"
#include "mainwindow.h"
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QGroupBox>
#include <QInputDialog>
#include <QSettings>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QDateTime>
#include <QTemporaryDir>
#include <QTimer>
#include <QLabel>
#include <cstdio>

int normalizerDialogTests() {
    int failures = 0;
    auto check = [&](bool ok, const char *label) { std::printf("%s normalizer UI %s\n", ok ? "PASS" : "FAIL", label); std::fflush(stdout); if (!ok) ++failures; };
    auto waitUntil = [](const std::function<bool()> &ready) {
        QElapsedTimer timeout; timeout.start();
        while (!ready() && timeout.elapsed() < 3000) { QEventLoop loop; QTimer::singleShot(10, &loop, &QEventLoop::quit); loop.exec(); }
        return ready();
    };
    QTemporaryDir settingsDirectory(QApplication::applicationDirPath() + "/../build/normalize-settings-XXXXXX");
    const auto previousFormat = QSettings::defaultFormat();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    const auto original = AudioData::tone(1, 440, 8000, 2);
    NormalizeDialog dialog(original, 0, original.frames(), "Prueba"); dialog.show();
    auto *presets = dialog.findChild<QComboBox *>("normalizePresets");
    auto *target = dialog.findChild<QDoubleSpinBox *>("target");
    auto *ignore = dialog.findChild<QDoubleSpinBox *>("ignore");
    auto *reuse = dialog.findChild<QCheckBox *>("normalizeReuseScan");
    auto *preview = dialog.findChild<QPushButton *>("processPreview");
    auto *bypass = dialog.findChild<QCheckBox *>("processBypass");
    auto *scan = dialog.findChild<QPushButton *>("normalizeScan");
    check(presets->count() >= 6 && !dialog.findChild<QGroupBox *>("normalizeScanGroup")->isEnabled()
        && dialog.meters->sizeHint().height() > dialog.meters->sizeHint().width(), "classic controls, presets and vertical stereo preview meter exist");
    presets->setCurrentIndex(3);
    check(dialog.findChild<QRadioButton *>("normalizeRms")->isChecked() && target->value() == -10
        && dialog.findChild<QGroupBox *>("normalizeScanGroup")->isEnabled(), "speech preset selects RMS and its target");
    presets->setCurrentIndex(5);
    dialog.grab().save(QApplication::applicationDirPath() + "/../build/normalize-dialog-rms.png");
    dialog.findChild<QCheckBox *>("normalizeContour")->setChecked(false); ignore->setValue(-96); target->setValue(-12);
    scan->click();
    check(reuse->isEnabled() && reuse->isChecked() && dialog.values().contains("scan_peak")
        && !dialog.findChild<QLabel *>("normalizeRmsValue")->text().contains("—"), "level analysis populates peak/RMS and enables scan reuse");
    target->setValue(-13);
    check(reuse->isChecked() && dialog.values().contains("scan_rms"), "changing the target preserves a valid analysis");
    ignore->setValue(-95);
    check(!reuse->isEnabled() && !dialog.values().contains("scan_rms"), "changing scan settings invalidates cached levels");
    ignore->setValue(-96); target->setValue(-12);
    AudioData heard, processed; std::function<void()> completed; int stops = 0, updates = 0;
    dialog.previewAudio = [&](const AudioData &audio, std::function<void()> finished) { heard = audio; completed = std::move(finished); ++updates; return true; };
    dialog.stopAudio = [&] { ++stops; };
    preview->click(); processed = heard;
    check(preview->isChecked() && bypass->isEnabled() && heard.frames() == original.frames()
        && heard.samples != original.samples, "preview processes audio without changing the source");
    bypass->setChecked(true);
    check(heard.samples == original.samples, "bypass previews the unmodified selection");
    bypass->setChecked(false);
    check(heard.samples == processed.samples, "leaving bypass restores the processed preview");
    const int beforeUpdate = updates;
    target->setValue(-20); target->setValue(-18);
    const bool changedLive = waitUntil([&] { return updates > beforeUpdate; });
    check(changedLive && preview->isChecked() && bypass->isEnabled() && stops == 0 && heard.samples != processed.samples,
        "target edits update the running preview without invoking Stop");
    const auto expectedLive = Dsp::apply(original, 0, original.frames(), "normalize", {{"mode", 1}, {"target", -18}, {"ignore", -96}, {"contour", 0}});
    check(heard.samples == expectedLive.audio.samples, "rapid edits publish the newest parameters");
    const int beforeRescan = updates; ignore->setValue(-80);
    check(waitUntil([&] { return updates > beforeRescan; }) && preview->isChecked() && stops == 0,
        "scan-parameter edits recompute in the background while preview stays active");
    const int beforePreset = updates; presets->setCurrentIndex(4);
    check(waitUntil([&] { return updates > beforePreset; }) && preview->isChecked() && stops == 0,
        "changing presets keeps preview running");
    preview->click();
    check(!preview->isChecked() && stops == 1, "explicit preview toggle stops playback");
    preview->click();
    if (completed) completed();
    check(!preview->isChecked() && !bypass->isEnabled() && stops == 2, "device termination resets preview controls");
    // Personal presets persist through QSettings and can be deleted.
    const QString presetName = "test-normalizer-" + QString::number(QDateTime::currentMSecsSinceEpoch());
    QTimer::singleShot(0, &dialog, [&] { if (auto *input = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) { input->setTextValue(presetName); input->accept(); } });
    dialog.findChild<QPushButton *>("normalizeSavePreset")->click();
    check(presets->findData(presetName) > 5 && QSettings().contains("normalize/presets/" + presetName), "custom preset can be saved");
    dialog.findChild<QPushButton *>("normalizeDeletePreset")->click();
    check(presets->findData(presetName) < 0 && !QSettings().contains("normalize/presets/" + presetName), "custom preset can be deleted");
    dialog.reject();
    MainWindow window({}, false); window.addDocument(original); auto *document = window.current();
    document->undo.push(new QUndoCommand("Previous edit"));
    QTimer::singleShot(0, &window, [&] {
        if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            modal->accept();
        }
    });
    window.runProcess("normalize");
    check(document->undo.canUndo() && document->undo.index() == 2 && document->data.samples != original.samples,
        "normalization always adds one undo step and preserves previous history");
    document->undo.undo();
    check(document->data.samples == original.samples && document->undo.index() == 1,
        "undo restores the original audio while keeping previous edits");
    const auto beforeCancel = document->data.samples;
    QTimer::singleShot(0, &window, [&] { if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) modal->reject(); });
    window.runProcess("normalize");
    check(document->data.samples == beforeCancel && !window.previewVu, "cancel preserves audio and releases preview meters");
    document->undo.setClean(); window.close();
    QSettings::setDefaultFormat(previousFormat);
    return failures;
}
