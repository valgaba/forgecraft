#include "recorder.h"
#include "mainwindow.h"
#include <QApplication>
#include <QAction>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QToolButton>
#include <QEventLoop>
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

int recordingScopeTests() {
    int failures = 0; DeviceCounters counters;
    auto check = [&](bool ok, const char *message) { std::printf("%s recording MDI %s\n", ok ? "PASS" : "FAIL", message); std::fflush(stdout); if (!ok) ++failures; };
    auto source = std::make_unique<SyntheticRecorder>(&counters);
    MainWindow window([&] { return source.get(); }); window.show();
    window.addDocument(AudioData{});
    auto wait = [] { QEventLoop loop; QTimer::singleShot(60, &loop, &QEventLoop::quit); loop.exec(); };
    auto *workspace = window.findChild<QMdiArea *>();
    auto *firstWindow = workspace->currentSubWindow();
    auto *first = static_cast<Document *>(firstWindow->widget());
    QTemporaryDir temp(QApplication::applicationDirPath() + "/../build/record-scope-XXXXXX");
    QString error; const auto tone = AudioData::tone(.1, 440);
    if (!temp.isValid() || !tone.write(temp.filePath("second.wav"), error)) { check(false, "fixture creation"); return failures; }
    window.openFile(temp.filePath("second.wav"), true);
    auto *secondWindow = workspace->currentSubWindow();
    auto *second = static_cast<Document *>(secondWindow->widget());
    const auto secondOriginal = second->data.samples;
    auto clickStop = [](Document *document) {
        for (auto *button : document->findChildren<QToolButton *>()) if (button->toolTip() == "Detener") { button->click(); return true; }
        return false;
    };
    first->record(); wait();
    check(source->active() && source->buffer.audio.frames() > 0, "capture starts in first window");
    workspace->setActiveSubWindow(secondWindow);
    const qint64 beforeForeignStop = source->buffer.audio.frames();
    check(clickStop(second), "second window Stop button exists");
    check(source->active() && first->data.frames() == 0 && second->data.samples == secondOriginal, "Stop in another window leaves recording and both documents untouched");
    wait();
    check(source->active() && source->buffer.audio.frames() > beforeForeignStop, "capture keeps accumulating after foreign Stop");
    const qint64 firstTake = source->buffer.audio.frames();
    // Focus remains in the second window: the owning button must still stop first.
    clickStop(first);
    check(!source->active() && first->data.frames() == firstTake && firstTake > 0 && second->data.samples == secondOriginal, "own Stop commits the take to its owner regardless of focus");
    first->undo.undo(); check(first->data.frames() == 0, "stopped take can be undone"); first->undo.redo();
    second->record(); wait(); workspace->setActiveSubWindow(firstWindow);
    const qint64 secondTakeBefore = source->buffer.audio.frames(); clickStop(first); wait();
    check(source->active() && source->buffer.audio.frames() > secondTakeBefore && second->data.samples == secondOriginal, "isolation also works when second window owns capture");
    const qint64 secondTake = source->buffer.audio.frames(); clickStop(second);
    check(!source->active() && second->data.frames() == tone.frames() + secondTake, "second window Stop appends its own take");
    const qint64 existing = first->data.frames(); first->record(); wait();
    const qint64 beforeToolbarStop = source->buffer.audio.frames(); workspace->setActiveSubWindow(secondWindow);
    bool globalFound = false;
    for (auto *action : window.findChildren<QAction *>()) if (action->text() == "Detener") { globalFound = true; action->trigger(); break; }
    wait();
    check(globalFound && source->active() && source->buffer.audio.frames() > beforeToolbarStop && first->data.frames() == existing, "bottom Stop leaves capture in unfocused window running");
    const qint64 focusedTake = source->buffer.audio.frames(); workspace->setActiveSubWindow(firstWindow);
    for (auto *action : window.findChildren<QAction *>()) if (action->text() == "Detener") { action->trigger(); break; }
    check(!source->active() && first->data.frames() == existing + focusedTake, "bottom Stop commits capture only when its owner is focused");
    first->undo.undo(); first->undo.undo();
    check(first->data.frames() == 0, "empty focused document fixture");
    second->record(); wait(); workspace->setActiveSubWindow(firstWindow);
    const qint64 beforeEmptyPlay = source->buffer.audio.frames(); bool playFound = false;
    for (auto *action : window.findChildren<QAction *>()) if (action->text() == "Reproducir / detener") { playFound = true; action->trigger(); break; }
    wait();
    check(playFound && source->active() && source->buffer.audio.frames() > beforeEmptyPlay && first->data.frames() == 0, "bottom Play on empty focused window leaves other capture running");
    workspace->setActiveSubWindow(secondWindow);
    for (auto *action : window.findChildren<QAction *>()) if (action->text() == "Detener") { action->trigger(); break; }
    check(!source->active(), "bottom Stop follows newly focused recording window");
    for (auto *child : workspace->subWindowList()) static_cast<Document *>(child->widget())->undo.setClean();
    window.close(); return failures;
}
