#include "audio.h"
#include "waveform.h"
#include "mainwindow.h"
#include <QApplication>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QMessageBox>
#include <QFile>
#include <cstdio>

int loadingProgressTests() {
    int failures = 0;
    auto check = [&](bool ok, const char *name) { std::printf("%s loading %s\n", ok ? "PASS" : "FAIL", name); std::fflush(stdout); if (!ok) ++failures; };
    QTemporaryDir temp(QApplication::applicationDirPath() + "/../build/load-test-XXXXXX");
    auto original = AudioData::tone(5, 440); QString error;
    if (!temp.isValid()) { check(false, "test workspace"); return failures; }
    for (const QString &suffix : {QString("wav"), QString("mp3")}) {
        const QString path = temp.filePath("Audio español." + suffix);
        if (!original.write(path, error)) { check(false, "fixture encoding"); continue; }
        AudioData decoded; QVector<int> values;
        bool loaded = AudioData::read(path, decoded, error, [&](int percent) { values.append(percent); return true; });
        bool monotonic = !values.isEmpty() && values.first() == 0 && values.last() == 100;
        for (qsizetype i = 0; i < values.size(); ++i) monotonic &= values[i] >= 0 && values[i] <= 100 && (i == 0 || values[i] >= values[i - 1]);
        check(loaded && monotonic && decoded.frames() == original.frames(), suffix == "wav" ? "WAV reports monotonic 0–100 progress" : "MP3 reports monotonic 0–100 progress");
        AudioData sentinel = AudioData::tone(.01, 1000); const auto untouched = sentinel.samples;
        bool read = AudioData::read(path, sentinel, error, [](int percent) { return percent < 35; });
        check(!read && sentinel.samples == untouched, suffix == "wav" ? "cancel WAV preserves destination" : "cancel MP3 preserves destination");
    }
    QVector<int> peakProgress;
    auto prepared = Waveform::preparePeaks(original, [&](int percent) { peakProgress.append(percent); return true; });
    bool accurate = !prepared.canceled && !peakProgress.isEmpty() && peakProgress.first() == 0 && peakProgress.last() == 100;
    for (qsizetype f = 0; f < original.frames(); ++f) for (int c = 0; c < original.channels; ++c) {
        qsizetype block = (f / 128) * original.channels + c;
        accurate &= original.samples[f * original.channels + c] >= prepared.minima[block] && original.samples[f * original.channels + c] <= prepared.maxima[block];
    }
    check(accurate, "waveform preparation reports progress and retains sample bounds");
    check(Waveform::preparePeaks(original, [](int percent) { return percent < 40; }).canceled, "waveform preparation is cancelable");
    return failures;
}

int loadingUiTests() {
    int failures = 0;
    auto check = [&](bool ok, const char *name) { std::printf("%s loading UI %s\n", ok ? "PASS" : "FAIL", name); std::fflush(stdout); if (!ok) ++failures; };
    QTemporaryDir temp(QApplication::applicationDirPath() + "/../build/load-ui-XXXXXX"); QString error;
    const auto audio = AudioData::tone(20, 440);
    const QString wav = temp.filePath("Carga WAV.wav"), mp3 = temp.filePath("Carga MP3.mp3");
    if (!temp.isValid() || !audio.write(wav, error) || !audio.write(mp3, error)) { check(false, "test fixtures"); return failures; }
    MainWindow window({}, false); window.show(); auto *workspace = window.findChild<QMdiArea *>();
    QTimer observe; observe.setInterval(0);
    bool cancel = false, sawDialog = false, validRange = true, sawError = false;
    QObject::connect(&observe, &QTimer::timeout, &window, [&] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { sawError = true; box->reject(); }
        auto *dialog = window.findChild<QProgressDialog *>("audioLoadProgress");
        if (!dialog) return;
        sawDialog = true; validRange &= dialog->minimum() == 0 && dialog->maximum() == 100 && dialog->value() >= 0 && dialog->value() <= 100;
        if (cancel) dialog->cancel();
    });
    observe.start(); window.openFile(wav); observe.stop();
    auto *firstWindow = workspace->currentSubWindow(); auto *first = static_cast<Document *>(firstWindow->widget());
    check(sawDialog && validRange && !sawError && first->data.frames() == audio.frames(), "WAV displays progress and opens waveform");
    auto before = first->data.samples; int count = workspace->subWindowList().size();
    cancel = true; sawDialog = false; observe.start(); window.openFile(mp3, true); observe.stop();
    check(sawDialog && workspace->subWindowList().size() == count && first->data.samples == before && !sawError, "canceling MP3 import preserves open documents");
    cancel = false; sawDialog = false; observe.start(); window.openFile(mp3, true); observe.stop();
    auto *second = static_cast<Document *>(workspace->currentSubWindow()->widget());
    check(sawDialog && validRange && !sawError && workspace->subWindowList().size() == count + 1 && second->path == mp3 && second->data.frames() == audio.frames(), "MP3 displays progress and opens a new waveform");
    const QString badPath = temp.filePath("Archivo incorrecto.wav");
    QFile bad(badPath); bad.open(QIODevice::WriteOnly); bad.write("not a WAV"); bad.close();
    observe.start(); window.openFile(badPath, true); observe.stop();
    check(sawError && workspace->subWindowList().size() == count + 1 && second->data.frames() == audio.frames(), "failed import closes progress and preserves loaded documents");
    window.close(); return failures;
}
