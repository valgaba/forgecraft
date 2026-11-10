#include "audio.h"
#include "dsp.h"
#include "waveform.h"
#include "audioformats.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QProcess>
#include <QElapsedTimer>
#include <QtEndian>
#include <cstdio>
#include <cmath>
#include <cstring>

int sampleBufferTests() {
    int failures = 0;
    auto check = [&](bool ok, const char *name) { std::printf("%s sample storage %s\n", ok ? "PASS" : "FAIL", name); std::fflush(stdout); if (!ok) ++failures; };
    SampleBuffer original{1, 2, 3, 4}; auto copy = original;
    check(original.constData() == copy.constData(), "copies share samples");
    copy[0] = 9;
    check(original.constData()[0] == 1 && copy.constData()[0] == 9, "writing a copy preserves its source");
    auto slice = original.mid(1, 2); check(slice.constData() == original.constData() + 1, "selection slices share the source without copying");
    slice[0] = 7; check(original.constData()[1] == 2 && slice.constData()[0] == 7, "editing a selection detaches only that selection");
    original.resize(2); original.resize(5);
    check(original == SampleBuffer({1, 2, 0, 0, 0}), "shrinking and extending zeroes the new range");
    check(SampleBuffer::join({slice, original.first(2)}) == SampleBuffer({7, 3, 1, 2}), "joining fragments preserves order");
    SampleBuffer disk; disk.resize(SampleBuffer::memorySamples + 4096); disk[0] = .25f; disk[disk.size() - 1] = -.5f;
    const QString path = disk.backingPath();
    check(disk.diskBacked() && QFileInfo::exists(path), "large samples use a mapped temporary file");
    const qsizetype previousSize = disk.size(); disk.resize(previousSize * 2);
    check(disk.backingPath() == path && disk.constData()[0] == .25f && disk.constData()[previousSize - 1] == -.5f && disk.constData()[disk.size() - 1] == 0,
        "growing a unique disk buffer remaps the same file and preserves its prefix");
    disk[disk.size() - 1] = -.5f;
    auto snapshot = disk; auto selected = disk.mid(disk.size() - 8);
    disk.clear(); check(QFileInfo::exists(path) && selected.constData()[7] == -.5f, "undo and selections retain the backing file after original release");
    selected[7] = .75f; check(!selected.diskBacked() && snapshot.constData()[snapshot.size() - 1] == -.5f, "small edits do not clone the whole large source");
    snapshot.clear(); check(!QFileInfo::exists(path), "last snapshot release removes its temporary file");
    bool rejected = false; try { original.resize(SampleBuffer::maxBytes / sizeof(float) + 1); } catch (const std::exception &) { rejected = true; }
    check(rejected && original.size() == 5, "invalid allocations preserve the existing buffer");
    return failures;
}

int largeAudioTests() {
    int failures = sampleBufferTests();
    auto check = [&](bool ok, const char *name) { std::printf("%s large audio %s\n", ok ? "PASS" : "FAIL", name); std::fflush(stdout); if (!ok) ++failures; };
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/../build/large-audio-XXXXXX");
    // A real seven-hour PCM16 mono WAV, sparse on disk, exceeds the former
    // decoded limit. No microphone or user file is used by this test.
    const qint64 frames = 7ll * 3600 * 8000, pcmSize = frames * 2;
    const QString input = temp.filePath("Siete horas.wav");
    QFile file(input); bool ready = file.open(QIODevice::WriteOnly);
    QByteArray header(44, 0); std::memcpy(header.data(), "RIFF", 4); qToLittleEndian<quint32>(quint32(36 + pcmSize), header.data() + 4);
    std::memcpy(header.data() + 8, "WAVEfmt ", 8); qToLittleEndian<quint32>(16, header.data() + 16);
    qToLittleEndian<quint16>(1, header.data() + 20); qToLittleEndian<quint16>(1, header.data() + 22); qToLittleEndian<quint32>(8000, header.data() + 24);
    qToLittleEndian<quint32>(16000, header.data() + 28); qToLittleEndian<quint16>(2, header.data() + 32); qToLittleEndian<quint16>(16, header.data() + 34);
    std::memcpy(header.data() + 36, "data", 4); qToLittleEndian<quint32>(quint32(pcmSize), header.data() + 40);
    ready &= file.write(header) == 44 && file.resize(44 + pcmSize);
    file.seek(44 + (frames - 1) * 2); char last[2]; qToLittleEndian<quint16>(16384, last); file.write(last, 2); file.close();
    check(ready, "seven-hour WAV fixture");
    AudioData audio; QString error; int progress = 0;
    bool loaded = AudioData::read(input, audio, error, [&](int p) { progress = p; return true; });
    check(loaded && audio.frames() == frames && audio.samples.diskBacked() && progress == 100 && audio.samples.constData()[frames - 1] == .5f, "seven-hour audio decodes beyond 512 MB into disk storage");
    if (!loaded) { std::printf("%s\n", error.toUtf8().constData()); return failures + 1; }
    const auto peaks = Waveform::preparePeaks(audio);
    check(!peaks.canceled && peaks.maxima.last() == .5f && peaks.maxima.size() < audio.samples.size() / 100, "seven-hour waveform cache remains compact and includes the end");
    const auto snapshot = audio;
    auto result = Dsp::apply(audio, frames - 8000, frames, "volume", {{"gain", -6}});
    check(result.error.isEmpty() && result.audio.samples.diskBacked() && result.audio.frames() == frames && snapshot.samples.constData()[frames - 1] == .5f,
        "processing the end of a seven-hour document preserves duration and undo snapshot");
    const QString saved = temp.filePath("Guardado largo.wav");
    check(audio.write(saved, error) && QFileInfo(saved).size() == 44 + pcmSize, "long WAV exports in chunks");
    const QString protectedPath = temp.filePath("Cancelar.wav"); QFile originalFile(protectedPath); originalFile.open(QIODevice::WriteOnly); originalFile.write("original"); originalFile.close();
    bool canceled = !audio.write(protectedPath, error, 192, [](int p) { return p < 10; });
    originalFile.open(QIODevice::ReadOnly); check(canceled && originalFile.readAll() == "original", "canceling a long export preserves the original file"); originalFile.close();
    SampleBuffer huge; huge.resize(qsizetype(7ll * 3600 * 44100 * 2));
    const qsizetype end = huge.size() - 1; huge[end] = -.25f;
    check(huge.diskBacked() && huge.size() * qint64(sizeof(float)) > 4ll * 1024 * 1024 * 1024 && huge.mid(end).constData()[0] == -.25f,
        "64-bit stereo sample offsets work beyond 4 GB for seven-hour sessions");
    AudioData rf64; rf64.rate = 22050; rf64.channels = 2; rf64.bitDepth = 32; rf64.floatFormat = true;
    rf64.samples.resize(7ll * 3600 * rf64.rate * 2); rf64.samples[rf64.samples.size() - 1] = -.25f;
    const QString rf64Path = temp.filePath("Siete horas float RF64.wav");
    std::printf("RUN large audio: exporting and reopening a seven-hour RF64 beyond 4 GB\n"); std::fflush(stdout);
    bool wroteRF64 = rf64.write(rf64Path, error);
    QFile rf64File(rf64Path); rf64File.open(QIODevice::ReadOnly);
    check(wroteRF64 && rf64File.read(4) == "RF64" && rf64File.size() > 4ll * 1024 * 1024 * 1024, "WAV beyond RIFF size exports automatically as RF64"); rf64File.close();
    AudioData reopenedRF64;
    check(wroteRF64 && AudioData::read(rf64Path, reopenedRF64, error) && reopenedRF64.frames() == rf64.frames()
        && reopenedRF64.samples.constData()[reopenedRF64.samples.size() - 1] == -.25f && reopenedRF64.floatFormat,
        "RF64 larger than 4 GB reopens with exact float samples and seven-hour duration");
    if (!error.isEmpty()) std::printf("%s\n", error.toUtf8().constData());
    reopenedRF64.samples.clear(); rf64.samples.clear(); huge.clear(); QFile::remove(rf64Path);
    std::printf("RUN large audio: generating and decoding a real seven-hour MP3\n"); std::fflush(stdout);
    const QString mp3Path = temp.filePath("Siete horas.mp3");
    QProcess encoder; encoder.setProgram(AudioFormats::toolPath("ffmpeg"));
#ifdef Q_OS_WIN
    encoder.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
    encoder.setArguments({"-nostdin", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "anullsrc=r=8000:cl=mono", "-t", "25200", "-c:a", "libmp3lame", "-compression_level", "9", "-b:a", "24k", "-y", mp3Path});
    encoder.start(); bool encoded = encoder.waitForStarted(); QElapsedTimer deadline; deadline.start();
    while (encoded && encoder.state() != QProcess::NotRunning && deadline.elapsed() < 600000) encoder.waitForFinished(1000);
    if (encoder.state() != QProcess::NotRunning) { encoder.kill(); encoder.waitForFinished(); encoded = false; }
    encoded &= encoder.exitStatus() == QProcess::NormalExit && encoder.exitCode() == 0;
    AudioData mp3;
    check(encoded && AudioData::read(mp3Path, mp3, error) && mp3.frames() == frames && mp3.samples.diskBacked() && mp3.rate == 8000 && mp3.channels == 1,
        "real seven-hour MP3 decodes beyond the former 512 MB limit");
    if (!encoded) std::printf("%s\n", encoder.readAllStandardError().constData());
    if (!error.isEmpty()) std::printf("%s\n", error.toUtf8().constData());
    return failures;
}
