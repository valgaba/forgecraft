#include "audioformats.h"
#include "mainwindow.h"
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialog>
#include <QComboBox>
#include <QTimer>
#include <QElapsedTimer>
#include <QAction>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cstdio>
#include <cmath>

int audioFormatTests() {
    int failures = 0;
    auto check = [&](bool ok, const QString &name) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.toUtf8().constData()); std::fflush(stdout); if (!ok) ++failures; };
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/../build/formats-test-XXXXXX"); QString error;
    check(temp.isValid(), "format test workspace: " + temp.errorString());
    const auto source = AudioData::tone(1.0, 440);
    check(!AudioFormats::toolPath("ffmpeg").isEmpty() && !AudioFormats::toolPath("ffprobe").isEmpty(), "bundled FFmpeg tools available");
    for (const auto &format : AudioFormats::formats()) {
        if (format.id == "wav" || format.id == "mp3") continue; // Native codecs are covered by the existing tests.
        const QString path = temp.filePath("Audio español ñ." + format.suffix);
        AudioExportOptions options; options.bits = 16;
        int progress = -1; bool monotonic = true;
        bool written = AudioFormats::write(source, path, options, error, [&](int p) { monotonic &= p >= progress; progress = p; return true; });
        check(written, format.title + " real encode");
        if (!written) { std::printf("%s\n", error.toUtf8().constData()); continue; }
        check(monotonic && progress == 100, format.title + " export progress");
        AudioData decoded;
        bool read = format.id == "raw" ? AudioFormats::read(path, decoded, error) : AudioData::read(path, decoded, error);
        check(read, format.title + " decode");
        if (!read) { std::printf("%s\n", error.toUtf8().constData()); continue; }
        const int expectedRate = format.id == "amr" ? 8000 : format.id == "opus" || format.id == "webm" || format.id == "ac3" ? 48000 : 44100;
        check(decoded.rate == expectedRate && decoded.channels == (format.id == "amr" ? 1 : 2) && std::abs(decoded.duration() - 1) < .16, format.title + " duration, rate and channels");
        double energy = 0; for (float sample : decoded.samples) energy += sample * sample;
        check(energy / qMax(qsizetype(1), decoded.samples.size()) > .03, format.title + " non-silent signal");
        if (!format.lossy) {
            double difference = 0;
            if (decoded.samples.size() != source.samples.size()) difference = 1;
            else for (qsizetype i = 0; i < source.samples.size(); ++i) difference = qMax(difference, double(std::abs(decoded.samples[i] - source.samples[i])));
            check(difference < .0001, format.title + " lossless precision");
        }
        QFile bytes(path); bytes.open(QIODevice::ReadOnly);
        const auto signature = bytes.read(12);
        check(format.id == "rf64" ? signature.startsWith("RF64") : !signature.startsWith("RIFF"), format.title + " correct container, not disguised WAV");
    }
    const QString protectedPath = temp.filePath("protected.flac");
    for (const QString suffix : {"aiff", "au", "caf", "w64", "rf64", "wv", "flac"}) for (bool floating : {false, true}) {
        if (floating && suffix == "flac") continue;
        const QString path = temp.filePath("depth." + suffix);
        AudioExportOptions options; options.bits = floating ? 32 : 24; options.floating = floating;
        AudioData decoded;
        bool ok = AudioFormats::write(source, path, options, error) && AudioData::read(path, decoded, error);
        double difference = 1;
        if (ok && decoded.samples.size() == source.samples.size()) {
            difference = 0; for (qsizetype i = 0; i < source.samples.size(); ++i) difference = qMax(difference, double(std::abs(decoded.samples[i] - source.samples[i])));
        }
        check(ok && difference < .000001 && decoded.bitDepth == options.bits && decoded.floatFormat == floating, suffix + (floating ? " float32" : " PCM24") + " export precision and metadata");
        if (!ok) std::printf("%s\n", error.toUtf8().constData());
    }
    auto reset = [&] { QFile file(protectedPath); file.open(QIODevice::WriteOnly); file.write("original destination"); };
    auto preserved = [&] { QFile file(protectedPath); file.open(QIODevice::ReadOnly); return file.readAll() == "original destination"; };
    for (int cancelAt : {0, 20, 95, 100}) {
        reset(); check(!AudioFormats::write(source, protectedPath, {}, error, [=](int p) { return p < cancelAt; }) && preserved(), QString("export cancellation at %1 preserves destination").arg(cancelAt));
    }
    AudioData untouched = source;
    reset(); check(!AudioData::read(protectedPath, untouched, error) && untouched.samples == source.samples, "invalid compressed input preserves destination");
    check(!source.write(temp.filePath("unknown.xyz"), error) && !QFileInfo::exists(temp.filePath("unknown.xyz")), "unknown output format rejected without fake WAV");
    const QString raw = temp.filePath("mono.pcm"); AudioExportOptions options; options.rawEncoding = "s24be"; options.rate = 22050; options.channels = 1;
    RawAudioOptions rawOptions; rawOptions.encoding = "s24be"; rawOptions.rate = 22050; rawOptions.channels = 1;
    AudioData back;
    check(AudioFormats::write(source, raw, options, error) && AudioFormats::read(raw, back, error, {}, rawOptions) && back.rate == 22050 && back.channels == 1 && back.frames() == 22050, "RAW endian/rate/channel options round trip");
    const auto *flac = AudioFormats::forPath("test.FLAC");
    check(flac && AudioFormats::forFilter(AudioFormats::filter(*flac)) == flac && AudioFormats::pathForFormat("test.wav", *flac) == "test.flac", "format filters and suffix conversion");
    check(!AudioFormats::canOpen("project.aup3") && !AudioFormats::canOpen("notes.mid") && AudioFormats::canOpen("audio.opus"), "audio-only open and drag filters");
    return failures;
}

int audioFormatUiTests() {
    int failures = 0;
    auto check = [&](bool ok, const char *name) { std::printf("%s audio formats UI %s\n", ok ? "PASS" : "FAIL", name); std::fflush(stdout); if (!ok) ++failures; };
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/../build/formats-ui-XXXXXX"); QString error;
    const auto source = AudioData::tone(.5, 440);
    const QString flac = temp.filePath("Fuente española.flac"), opus = temp.filePath("Fuente.opus");
    if (!source.write(flac, error) || !source.write(opus, error)) { check(false, "fixtures"); return failures; }
    MainWindow window({}, false); window.show();
    QTimer responder; responder.setInterval(5); QElapsedTimer deadline; deadline.start();
    bool rejectOptions = false, dialogFailure = false, filtersValid = false, exportOptionsSeen = false;
    const QString savedPath = temp.filePath("Exportación 24 bits.flac");
    QObject::connect(&responder, &QTimer::timeout, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
        if (qobject_cast<QMessageBox *>(dialog) || deadline.elapsed() > 10000) { dialogFailure = true; dialog->reject(); return; }
        if (dialog->property("formatTestHandled").toBool()) return;
        dialog->setProperty("formatTestHandled", true);
        if (auto *file = qobject_cast<QFileDialog *>(dialog)) {
            filtersValid = file->nameFilters() == AudioFormats::saveFilters() && !file->testOption(QFileDialog::DontUseNativeDialog);
            file->selectNameFilter(AudioFormats::filter(*AudioFormats::forPath(savedPath)));
            file->selectFile(savedPath); QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
        } else if (dialog->objectName() == "audioExportDialog") {
            exportOptionsSeen = true;
            if (rejectOptions) dialog->reject();
            else { for (auto *box : dialog->findChildren<QComboBox *>()) { const int index = box->findData(24); if (index >= 0) box->setCurrentIndex(index); } dialog->accept(); }
        } else if (dialog->objectName() == "rawAudioDialog") dialog->accept();
    });
    responder.start(); window.openFile(flac); responder.stop();
    auto *document = window.current();
    check(document && document->path == flac && document->data.frames() == source.frames(), "FLAC import creates waveform");
    if (!document) return failures;
    deadline.restart(); responder.start(); bool saved = window.save(document, true); responder.stop();
    AudioData decoded;
    check(saved && !dialogFailure && filtersValid && exportOptionsSeen && document->path == savedPath && AudioData::read(savedPath, decoded, error) && decoded.bitDepth == 24, "save-as filters, export options, Unicode name and reopen");
    QFile file(savedPath); file.open(QIODevice::ReadOnly); const auto original = file.readAll(); file.close();
    document->undo.push(new QUndoCommand("unsaved edit")); rejectOptions = true; deadline.restart(); responder.start(); saved = window.save(document); responder.stop();
    file.open(QIODevice::ReadOnly); check(!saved && file.readAll() == original && !document->undo.isClean(), "cancel export options preserves file and dirty state"); file.close();
    document->undo.setClean();
    const int before = window.workspace->subWindowList().size();
    QMimeData mime; mime.setUrls({QUrl::fromLocalFile(flac), QUrl::fromLocalFile(opus)});
    QDragEnterEvent enter(QPoint(10, 30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(document->wave, &enter);
    QDropEvent drop(QPointF(10, 30), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(document->wave, &drop);
    QApplication::processEvents();
    check(enter.isAccepted() && drop.isAccepted() && window.workspace->subWindowList().size() == before + 2, "FLAC and Opus drop open separate MDI windows");
    const QString raw = temp.filePath("Entrada.raw"); source.write(raw, error);
    deadline.restart(); responder.start(); window.openFile(raw, true); responder.stop();
    check(!dialogFailure && window.current()->path == raw && window.current()->data.frames() == source.frames(), "RAW import prompts for encoding and creates waveform");
    window.close(); return failures;
}
