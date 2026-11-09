#include "processpreview.h"
#include "mainwindow.h"
#include "previewplayer.h"
#include <QApplication>
#include <QDialog>
#include <QPushButton>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <limits>

int processPreviewTests() {
    int failures = 0;
    auto check = [&](bool ok, const QString &text) { std::printf("%s process preview %s\n", ok ? "PASS" : "FAIL", text.toUtf8().constData()); std::fflush(stdout); if (!ok) ++failures; };
    auto waitUntil = [](const std::function<bool()> &ready) {
        QElapsedTimer timeout; timeout.start();
        while (!ready() && timeout.elapsed() < 5000) { QEventLoop loop; QTimer::singleShot(10, &loop, &QEventLoop::quit); loop.exec(); }
        return ready();
    };
    const auto source = AudioData::tone(.12, 220, 8000, 2);
    const auto saved = source.samples;
    for (const auto &op : Dsp::operations()) {
        if (op.id == "normalize" || op.parameters.isEmpty()) continue;
        ProcessPreview preview(source, 0, source.frames(), op.id);
        auto values = Dsp::defaults(op.id); if (op.id == "resample") values["rate"] = source.rate;
        preview.setValues(values);
        AudioData heard; int updates = 0, stops = 0;
        preview.playAudio = [&](const AudioData &audio, std::function<void()>) { heard = audio; ++updates; return true; };
        preview.stopAudio = [&] { ++stops; };
        preview.setActive(true);
        bool ok = waitUntil([&] { return updates > 0; });
        const int before = updates; const auto &p = op.parameters.first();
        values[p.key] = values[p.key] == p.minimum ? p.maximum : p.minimum;
        preview.setValues(values);
        ok &= waitUntil([&] { return updates > before; }) && preview.active() && stops == 0
            && heard.frames() > 0 && heard.rate == source.rate && heard.channels == 2;
        check(ok, op.id + " updates its parameters without stopping the preview");
        preview.setBypass(true); const auto dry = heard;
        check(dry.rate == source.rate && dry.channels == 2 && dry.samples == source.samples,
            op.id + " bypass uses the unmodified source in the stable output format");
        preview.setActive(false);
        check(stops == 1 && source.samples == saved, op.id + " explicit Stop leaves the document untouched");
    }
    {
        ProcessPreview preview(source, 0, source.frames(), "volume");
        AudioData heard; int updates = 0, stops = 0; QString status;
        preview.playAudio = [&](const AudioData &audio, std::function<void()>) { heard = audio; ++updates; return true; };
        preview.stopAudio = [&] { ++stops; }; preview.statusChanged = [&](const QString &text) { status = text; };
        preview.setActive(true); waitUntil([&] { return updates > 0; });
        const int before = updates;
        for (int gain = -1; gain >= -20; --gain) preview.setValues({{"gain", double(gain)}});
        bool latest = waitUntil([&] { return updates > before; });
        for (int i = 0; i < heard.samples.size(); ++i) latest &= std::abs(heard.samples[i] - source.samples[i] * .1f) < 1e-6f;
        check(latest && stops == 0, "rapid edits publish only the latest gain");
        const auto valid = heard.samples; const int validUpdates = updates;
        preview.setValues({{"gain", std::numeric_limits<double>::quiet_NaN()}});
        check(waitUntil([&] { return status.contains("fuera de rango"); }) && updates == validUpdates && preview.active()
            && heard.samples == valid && stops == 0, "invalid settings keep the last valid audio playing");
        preview.setValues({{"gain", -6}}); preview.setActive(false);
        const int afterStop = updates;
        QEventLoop loop; QTimer::singleShot(150, &loop, &QEventLoop::quit); loop.exec();
        check(updates == afterStop && !preview.active(), "Stop cancels pending parameter work and prevents stale playback");
    }

    // End-to-end bindings: sliders, spin boxes and choices in actual dialogs.
    AudioData silence; silence.rate = 8000; silence.samples.fill(0, 8000 * 2);
    PreviewPlayer probe; QString error;
    if (!probe.start(silence, quint32(0xffffffffu), error)) {
        std::printf("SKIP process preview native UI: %s\n", error.toUtf8().constData()); return failures;
    }
    probe.stop();
    MainWindow window({}, false); window.outputDevice = quint32(0xffffffffu); window.addDocument(silence);
    auto *document = window.current();
    for (const QString &id : {QString("volume"), QString("chorus"), QString("distortion"), QString("channels")}) {
        int stage = 0; bool bound = false, presetBound = false, meterBound = false, running = false; QElapsedTimer deadline, changed; deadline.start();
        QTimer responder; responder.setInterval(10);
        QObject::connect(&responder, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
            if (dialog->objectName() != "processDialog") { dialog->reject(); return; }
            if (deadline.elapsed() > 5000) { dialog->reject(); return; }
            auto *button = dialog->findChild<QPushButton *>("processPreview");
            auto *statusLabel = dialog->findChild<QLabel *>("processPreviewStatus");
            if (stage == 0) {
                meterBound = window.previewVu && window.previewVu == dialog->findChild<VuMeter *>("processPreviewMeters");
                stage = 1; button->click(); return;
            }
            if (stage == 1 && window.livePreview && window.livePreview->active() && statusLabel->text() == "En bucle") {
                if (id == "distortion" || id == "channels") dialog->findChild<QComboBox *>("mode")->setCurrentIndex(1);
                else { auto *slider = dialog->findChild<QSlider *>(); slider->setValue(slider->value() > 0 ? slider->value() / 2 : 500); }
                bound = statusLabel->text().contains("Actualizando"); changed.start(); stage = 2; return;
            }
            if (stage == 2 && changed.elapsed() > 120 && statusLabel->text() == "En bucle") {
                dialog->findChild<QComboBox *>("processPresets")->setCurrentIndex(2);
                presetBound = statusLabel->text().contains("Actualizando") && button->isChecked(); changed.restart(); stage = 3; return;
            }
            if (stage == 3 && changed.elapsed() > 120 && statusLabel->text() == "En bucle") {
                auto *bypass = dialog->findChild<QPushButton *>("processBypass"); bypass->click();
                running = button->isChecked() && window.livePreview->active() && statusLabel->text().contains("Bypass");
                dialog->reject();
            }
        });
        responder.start(); window.runProcess(id); responder.stop();
        check(bound && presetBound && meterBound && running && document->data.samples == silence.samples && !document->undo.canUndo()
            && window.livePreview && !window.livePreview->active() && !window.previewVu,
            id + " live edits use the dialog meters and Cancel releases preview without committing");
    }
    for (const QString &id : {QString("resample"), QString("insert_silence")}) {
        bool noPreview = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
            noPreview = !dialog->findChild<VuMeter *>("processPreviewMeters") && !window.previewVu
                && !dialog->findChild<QPushButton *>("processPreview") && !dialog->findChild<QPushButton *>("processBypass");
            dialog->reject();
        });
        window.runProcess(id); check(noPreview, id + " omits unnecessary preview controls and meters");
    }
    window.close();
    return failures;
}
