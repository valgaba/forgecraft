#include "recorder.h"
#include "recorddialog.h"
#include "mainwindow.h"
#include <QApplication>
#include <QAction>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <cstdio>

namespace {
QByteArray pcmBlock(int frames, int channels, int rate, qint64 offset = 0) {
    QByteArray result(frames * channels * 2, Qt::Uninitialized);
    for (int f = 0; f < frames; ++f) for (int c = 0; c < channels; ++c) {
        qint16 sample = qint16(std::sin(2 * 3.141592653589793 * (c ? 660 : 440) * (f + offset) / rate) * 16000);
        qToLittleEndian(quint16(sample), result.data() + (f * channels + c) * 2);
    }
    return result;
}
struct DeviceCounters { int starts = 0, stops = 0, destroyed = 0; };
// Test-only input: the application always constructs the native Recorder.
class SyntheticRecorder : public Recorder {
    QTimer clock;
    bool open = false;
    DeviceCounters *counters;
    qint64 offset = 0;
public:
    explicit SyntheticRecorder(DeviceCounters *c) : counters(c) {
        clock.setInterval(10);
        connect(&clock, &QTimer::timeout, this, [this] {
            auto pcm = pcmBlock(buffer.audio.rate / 100, buffer.audio.channels, buffer.audio.rate, offset); offset += buffer.audio.rate / 100;
            bool keep = buffer.consume(pcm.constData(), pcm.size(), storing); emit updated();
            if (!keep) { stop(); emit limitReached(); }
        });
    }
    ~SyntheticRecorder() override { stop(); ++counters->destroyed; }
    bool start(quint32, int rate, int channels, bool record, QString &error) override {
        stop(); error.clear(); buffer.audio.rate = rate; buffer.audio.channels = channels; storing = record; open = true; ++counters->starts; clock.start(); return true;
    }
    bool active() const override { return open; }
    void stop() override { clock.stop(); if (open) ++counters->stops; open = false; storing = false; }
};
}

int recordingTests() {
    int failures = 0;
    auto check = [&](bool ok, const char *message) { std::printf("%s recording %s\n", ok ? "PASS" : "FAIL", message); if (!ok) ++failures; };
    CaptureBuffer buffer; buffer.reset(44100, 2);
    QByteArray pcm(8, Qt::Uninitialized); const qint16 samples[] = {-32768,32767,0,-16384}; for (int i = 0; i < 4; ++i) qToLittleEndian(quint16(samples[i]), pcm.data() + i * 2);
    check(buffer.consume(pcm.constData(), pcm.size(), true) && buffer.audio.samples == QVector<float>({-1,32767.f/32768.f,0,-.5f}), "PCM16 conversion and stereo order");
    buffer.reset(44100, 2); buffer.consume(pcm.constData(), pcm.size(), false); check(buffer.audio.frames() == 0 && buffer.peaks[0] == 1, "monitor updates meters without recording");
    buffer.consume(pcm.constData(), pcm.size(), true); buffer.consume(pcm.constData(), pcm.size(), false); buffer.consume(pcm.constData(), pcm.size(), true); check(buffer.audio.frames() == 4, "paused/monitor samples are excluded from take");
    buffer.reset(44100, 2); buffer.limitFrames = 3; buffer.consume(pcm.constData(), pcm.size(), true); bool limited = !buffer.consume(pcm.constData(), pcm.size(), true); check(limited && buffer.audio.frames() == 3, "duration limit truncates at exact frame");
    buffer.consume(pcm.constData(), pcm.size(), true); check(buffer.audio.frames() == 3, "limit prevents further appends");
    buffer.reset(44100, 1); buffer.dcAdjust = true; QByteArray constant(20000, Qt::Uninitialized); for (int i = 0; i < 10000; ++i) qToLittleEndian(quint16(16000), constant.data() + i * 2); buffer.consume(constant.constData(), constant.size(), true); check(std::abs(buffer.audio.samples.last()) < .0001, "DC filter removes steady offset");
    auto base = AudioData::tone(.1, 440, 8000, 2), take = AudioData::tone(.025, 880, 8000, 2);
    auto inserted = Recording::merge(base, take, Recording::Insert, 100, 200);
    check(inserted.error.isEmpty() && inserted.audio.frames() == 1000 && inserted.first == 100 && inserted.last == 300 && inserted.audio.samples.first(200) == base.samples.first(200) && inserted.audio.samples.mid(600) == base.samples.mid(200), "insert preserves both sides and selection bounds");
    auto replaced = Recording::merge(base, take, Recording::ReplaceSelection, 100, 300);
    check(replaced.error.isEmpty() && replaced.audio.frames() == 800 && replaced.audio.samples.mid(600) == base.samples.mid(600), "replace selected region");
    auto different = take; different.rate = 48000;
    check(!Recording::merge(base, different, Recording::Insert, 0, 0).error.isEmpty(), "reject incompatible destination format");
    check(Recording::merge(base, different, Recording::NewDocument, 0, 0).audio.rate == 48000, "new window keeps capture format");
    check(!Recording::merge(base, AudioData{}, Recording::NewDocument, 0, 0).error.isEmpty(), "reject empty take");
    Recorder native; QString error; check(!native.start(0xfffffffeu, 44100, 2, true, error) && !error.isEmpty() && !native.active(), "native invalid-device error releases input");
    std::printf("INFO recording: %lld Windows input entries; no microphone opened by automated tests\n", static_cast<long long>(Recorder::devices().size()));
    return failures;
}

int recordingUiTests() {
    int failures = 0; DeviceCounters counters;
    auto check = [&](bool ok, const char *message) { std::printf("%s recording UI %s\n", ok ? "PASS" : "FAIL", message); std::fflush(stdout); if (!ok) ++failures; };
    MainWindow window([&] { return new SyntheticRecorder(&counters); }); window.show();
    auto *workspace = window.findChild<QMdiArea *>(); QAction *recordAction = nullptr;
    for (auto *action : window.findChildren<QAction *>()) if (action->objectName() == "recordAudio") recordAction = action;
    if (!recordAction) { check(false, "record action exists"); return failures; }
    auto document = [&] { return static_cast<Document *>(workspace->currentSubWindow()->widget()); };
    auto undo = [&](bool redo) { auto *doc = document(); if (redo) doc->undo.redo(); else doc->undo.undo(); };
    QTimer script; script.setInterval(10); QElapsedTimer elapsed;
    int step = 0, wait = 0; qint64 pausedFrames = 0, firstTake = 0;
    bool monitorOk = false, pauseOk = false, deliveredOk = false, appendedOk = false, formatLocked = false, timeout = false;
    QObject::connect(&script, &QTimer::timeout, &window, [&] {
        auto *dialog = qobject_cast<RecordDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
        auto click = [&](const char *name) { if (auto *button = dialog->findChild<QPushButton *>(name)) button->click(); };
        if (elapsed.elapsed() > 5000) { timeout = true; click("recordStop"); dialog->reject(); return; }
        if (step == 0) {
            // La monitorización es automática: al abrir, la entrada ya se escucha sin grabar.
            if (auto *label = dialog->findChild<QLabel *>("recordState")) monitorOk = label->text() == "Escuchando la entrada" && dialog->take().frames() == 0;
            // Con audio en el documento, el formato de grabación queda fijado al suyo.
            if (auto *rate = dialog->findChild<QComboBox *>("recordRate")) if (document()->data.frames()) formatLocked = !rate->isEnabled();
            click("recordStart"); ++step; return;
        }
        if (step == 1 && dialog->take().frames() >= 1323) { dialog->grab().save(QApplication::applicationDirPath() + "/../build/recording-dialog.png"); click("recordPause"); pausedFrames = dialog->take().frames(); ++step; return; }
        if (step == 2 && ++wait > 5) { pauseOk = dialog->take().frames() == pausedFrames; click("recordPause"); ++step; return; }
        if (step == 3 && dialog->take().frames() >= pausedFrames + 882) {
            const qint64 before = document()->data.frames(), takeFrames = dialog->take().frames();
            click("recordStop");
            deliveredOk = document()->data.frames() == before + takeFrames && dialog->take().frames() == 0;
            firstTake = document()->data.frames();
            ++step; return;
        }
        if (step == 4) { click("recordStart"); ++step; return; }
        if (step == 5 && dialog->take().frames() >= 882) {
            const qint64 before = document()->data.frames(), takeFrames = dialog->take().frames();
            click("recordStop");
            appendedOk = document()->data.frames() == before + takeFrames;
            click("recordClose");
        }
    });
    auto run = [&] { step = wait = 0; elapsed.restart(); script.start(); recordAction->trigger(); script.stop(); };
    run();
    auto *doc = document();
    check(!timeout && monitorOk && pauseOk, "automatic monitoring, pause and resume");
    check(deliveredOk && firstTake > 0, "stop delivers the take to the focused document");
    check(appendedOk, "a second take is appended at the end");
    check(doc->undo.count() == 2 && !doc->undo.isClean() && workspace->subWindowList().size() == 1, "each take is an undo step");
    undo(false); check(doc->data.frames() == firstTake, "undo removes the second take");
    undo(false); check(doc->data.frames() == 0, "undo removes the first take");
    undo(true); check(doc->data.frames() == firstTake, "redo restores the take");
    const auto original = doc->data;
    run();
    check(!timeout && formatLocked, "recording format is locked to the document format");
    check(doc->data.frames() > original.frames() && doc->data.samples.first(original.samples.size()) == original.samples, "appending keeps the existing audio untouched");
    check(counters.starts == counters.stops && counters.destroyed == 2, "every input stream closed after dialog exit");
    for (auto *child : workspace->subWindowList()) static_cast<Document *>(child->widget())->undo.setClean();
    window.close(); return failures;
}
