#include "recorder.h"
#include "mainwindow.h"
#include "theme.h"
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
struct DeviceCounters { int starts = 0, stops = 0, destroyed = 0; quint32 device = 0; bool recording = false; };
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
    bool start(quint32 device, int rate, int channels, bool record, QString &error) override {
        counters->device = device; counters->recording = record;
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
    MainWindow window([&] { return source.get(); }, false); window.show();
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

int inputMonitoringTests() {
    int failures = 0;
    auto check = [&](bool ok, const char *message) { std::printf("%s input monitoring %s\n", ok ? "PASS" : "FAIL", message); std::fflush(stdout); if (!ok) ++failures; };
    auto wait = [](int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); };
    DeviceCounters counters;
    auto source = std::make_unique<SyntheticRecorder>(&counters);
    MainWindow window([&] { return source.get(); }); window.show();
    wait(120);
    check(source->active() && !counters.recording && source->buffer.audio.frames() == 0
        && window.workspace->subWindowList().isEmpty(), "starts without a document and never stores monitored samples");
    const QImage liveInput = window.inputVu->grab().toImage();
    bool visibleLevel = false;
    for (int y = 13; y < liveInput.height(); ++y) for (int x = 1; x < liveInput.width() - 1; ++x)
        visibleLevel |= liveInput.pixelColor(x, y) == Theme::Accent;
    check(visibleLevel, "input meter moves while not recording");
    auto *mute = window.findChild<QToolButton *>("inputMute");
    auto *outputMute = window.findChild<QToolButton *>("outputMute");
    check(mute && outputMute && mute->isCheckable() && outputMute->isCheckable(), "meter icons are independent mute buttons");
    if (!mute || !outputMute) return failures;
    const int startsBeforeMute = counters.starts, stopsBeforeMute = counters.stops;
    mute->click(); wait(60);
    check(mute->isChecked() && window.inputMuted && source->buffer.muted && source->active()
        && source->buffer.peaks[0] == 0 && source->buffer.peaks[1] == 0 && counters.starts == startsBeforeMute && counters.stops == stopsBeforeMute
        && !outputMute->isChecked(), "microphone click silences input without reopening it or muting output");
    outputMute->click();
    check(window.outputMuted && window.inputMuted && outputMute->isChecked(), "both mute controls can be active independently");
    auto hasMuteMark = [](QToolButton *button) {
        const QImage icon = button->icon().pixmap(QSize(20, 20), QIcon::Normal, QIcon::On).toImage();
        int red = 0;
        for (int y = 0; y < icon.height(); ++y) for (int x = 0; x < icon.width(); ++x) {
            const QColor color = icon.pixelColor(x, y); red += color.alpha() > 100 && color.red() > 180 && color.green() < 150;
        }
        return red > 10;
    };
    check(hasMuteMark(mute) && hasMuteMark(outputMute), "muted icons display the red slash");
    if (auto *panel = window.findChild<QFrame *>("commonVuMeters")) panel->grab().save(QApplication::applicationDirPath() + "/../build/mute-controls.png");
    outputMute->click();
    mute->click(); wait(60);
    check(!window.inputMuted && source->buffer.peaks[0] > .1, "second microphone click restores live input");
    window.stopPlayback(); wait(60);
    check(source->active() && source->buffer.audio.frames() == 0, "stopping playback leaves input monitoring running");
    window.addDocument(AudioData{});
    auto *document = window.current();
    window.startRecording(); wait(90);
    check(counters.recording && source->buffer.audio.frames() > 0, "recording switches the same input to take accumulation");
    const auto beforeMute = source->buffer.audio.samples;
    mute->click(); wait(70);
    const auto mutedSamples = source->buffer.audio.samples;
    bool silence = mutedSamples.size() > beforeMute.size();
    for (qsizetype i = beforeMute.size(); i < mutedSamples.size(); ++i) silence &= mutedSamples[i] == 0;
    check(silence && mutedSamples.first(beforeMute.size()) == beforeMute && source->active() && window.recording,
        "input mute records silence while preserving previous samples and capture timing");
    mute->click(); wait(70);
    bool recovered = false;
    for (qsizetype i = mutedSamples.size(); i < source->buffer.audio.samples.size(); ++i) recovered |= std::abs(source->buffer.audio.samples[i]) > .1;
    check(recovered && source->active(), "unmute resumes recording real samples in the same take");
    const qint64 takeFrames = source->buffer.audio.frames();
    window.stopRecording(); wait(60);
    check(source->active() && !counters.recording && source->buffer.audio.frames() == 0
        && document->data.frames() == takeFrames, "stopping recording resumes monitoring without extending the take");
    mute->click(); source->stop(); window.inputDevice = 123; window.startInputMonitor(); wait(30);
    check(source->active() && counters.device == 123 && !counters.recording && source->buffer.muted && source->buffer.peaks[0] == 0, "monitoring reopens the selected input device and preserves mute");
    mute->click();
    document->undo.setClean(); window.close(); wait(20);
    check(!source->active(), "closing the application releases the input device");

    VuMeter scale; scale.resize(380, 28);
    scale.setLevels(std::pow(10.0, -30.0 / 20.0), std::pow(10.0, -6.0 / 20.0));
    wait(250);
    const QImage image = scale.grab().toImage();
    auto barEnd = [&](int y) {
        int last = -1;
        for (int x = 1; x < image.width() - 1; ++x) if (image.pixelColor(x, y) == Theme::Accent) last = x;
        return last;
    };
    check(std::abs(barEnd(3) - 189) <= 3 && std::abs(barEnd(22) - 340) <= 3,
        "dBFS scale maps -30 dB to halfway and -6 dB to 90 percent");
    return failures;
}
