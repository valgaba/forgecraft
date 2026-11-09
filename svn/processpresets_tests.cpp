#include "processpresets.h"
#include "mainwindow.h"
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QInputDialog>
#include <QTimer>
#include <QDialog>
#include <cmath>
#include <cstdio>
#include <limits>

int processPresetTests() {
    int failures = 0;
    auto check = [&](bool ok, const QString &text) { std::printf("%s process presets %s\n", ok ? "PASS" : "FAIL", text.toUtf8().constData()); std::fflush(stdout); if (!ok) ++failures; };
    QTemporaryDir settingsDirectory(QApplication::applicationDirPath() + "/../build/process-presets-XXXXXX");
    const auto previousFormat = QSettings::defaultFormat(); QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    const auto source = AudioData::tone(.05, 220, 8000, 2);
    for (const auto &op : Dsp::operations()) {
        if (op.parameters.isEmpty()) continue;
        const auto presets = ProcessPresets::factory(op.id); bool valid = presets.size() >= 3;
        for (const auto &preset : presets) {
            valid &= !preset.personal && preset.values.size() == op.parameters.size();
            auto result = Dsp::apply(source, 0, source.frames(), op.id, preset.values);
            valid &= result.error.isEmpty() && !result.canceled;
            for (float value : result.audio.samples) valid &= std::isfinite(value) && std::abs(value) <= 1;
        }
        check(valid, op.id + " includes multiple valid factory presets");
    }
    QString error; const QString name = "Mi voz / café";
    check(ProcessPresets::save("volume", name, {{"gain", -9.3}}, error)
        && ProcessPresets::save("eq.graphic", name, {{"1000", 6}}, error), "personal presets support Unicode names and are separated by operation");
    auto lookup = [&](const QString &id, const QString &key) { for (const auto &p : ProcessPresets::list(id)) if (p.personal && p.name == key) return p.values; return Dsp::Values{}; };
    check(lookup("volume", name).value("gain") == -9.3 && lookup("eq.graphic", name).value("1000") == 6,
        "stored values survive reopening the settings store without cross-contamination");
    check(ProcessPresets::save("volume", name, {{"gain", -3}}, error) && lookup("volume", name).value("gain") == -3, "saving an existing personal name replaces its settings");
    check(!ProcessPresets::save("volume", "Predeterminado", {{"gain", 3}}, error)
        && !ProcessPresets::remove("volume", "Predeterminado", error), "factory presets cannot be overwritten or deleted");
    check(!ProcessPresets::save("volume", "Fuera", {{"gain", 90}}, error)
        && !ProcessPresets::save("volume", "NaN", {{"gain", std::numeric_limits<double>::quiet_NaN()}}, error), "invalid presets are rejected");
    check(ProcessPresets::remove("volume", name, error) && lookup("volume", name).isEmpty()
        && !lookup("eq.graphic", name).isEmpty(), "deleting a personal preset preserves other operations");

    MainWindow window({}, false); window.addDocument(source); auto *document = window.current();
    auto saveInDialog = [&](QDialog *dialog, const QString &name) {
        QTimer::singleShot(0, dialog, [name] {
            if (auto *input = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) { input->setTextValue(name); input->accept(); }
        });
        dialog->findChild<QPushButton *>("processSavePreset")->click();
    };
    bool saved = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
        auto *combo = dialog->findChild<QComboBox *>("processPresets");
        combo->setCurrentIndex(combo->findText("Reducir 6 dB"));
        auto *gain = dialog->findChild<QDoubleSpinBox *>("gain");
        saved = gain->value() == -6 && !dialog->findChild<QPushButton *>("processDeletePreset")->isEnabled();
        gain->setValue(-9.3); saved &= combo->currentIndex() == 0;
        saveInDialog(dialog, "Mi volumen");
        saved &= combo->currentText() == "Mi volumen" && dialog->findChild<QPushButton *>("processDeletePreset")->isEnabled();
        dialog->reject();
    });
    window.runProcess("volume"); check(saved, "dialog can load a factory preset and save modified controls");
    bool recovered = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
        auto *combo = dialog->findChild<QComboBox *>("processPresets"); combo->setCurrentIndex(combo->findText("Mi volumen"));
        recovered = dialog->findChild<QDoubleSpinBox *>("gain")->value() == -9.3; dialog->accept();
    });
    window.runProcess("volume");
    bool applied = recovered && document->undo.canUndo(); const double gain = std::pow(10.0, -9.3 / 20);
    for (int i = 0; i < source.samples.size(); ++i) applied &= std::abs(document->data.samples[i] - source.samples[i] * gain) < 1e-6;
    check(applied, "reopened dialog recalls the personal preset and applies its exact gain");
    document->undo.undo(); check(document->data.samples == source.samples, "preset application remains undoable");
    bool exactVertical = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
        auto *combo = dialog->findChild<QComboBox *>("processPresets"); combo->setCurrentIndex(combo->findText("Hall cálido"));
        saveInDialog(dialog, "Mi hall");
        for (const auto &p : ProcessPresets::factory("reverb")) if (p.name == "Hall cálido") exactVertical = lookup("reverb", "Mi hall") == p.values;
        dialog->grab().save(QApplication::applicationDirPath() + "/../build/reverb-presets.png");
        dialog->findChild<QPushButton *>("processDeletePreset")->click();
        exactVertical &= combo->findText("Mi hall") < 0 && lookup("reverb", "Mi hall").isEmpty(); dialog->reject();
    });
    window.runProcess("reverb"); check(exactVertical, "vertical-slider presets preserve exact values and can be deleted in the dialog");
    document->undo.setClean(); window.close(); QSettings::setDefaultFormat(previousFormat);
    return failures;
}
