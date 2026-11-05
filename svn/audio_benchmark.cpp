#include "audio.h"
#include "waveform.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QtEndian>
#include <cstdio>
#include <cmath>

// Opt-in benchmark. Deterministic fixtures, warm filesystem cache, no audio
// device and no microphone; fixture creation/MP3 encoding is not timed.
int audioBenchmark() {
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/../build/benchmark-XXXXXX");
    if (!temp.isValid()) return 1;
    for (int seconds : {30, 120}) {
        auto source = AudioData::tone(seconds, 440); QString error;
        for (const QString &extension : {QString("wav"), QString("mp3")}) {
            QString path = temp.filePath(QString::number(seconds) + "." + extension);
            if (!source.write(path, error)) { std::printf("ERROR %s\n", error.toUtf8().constData()); return 1; }
            qint64 decodeUs = 0, peaksUs = 0, pcmUs = 0;
            for (int run = 0; run < 3; ++run) {
                AudioData decoded; QElapsedTimer elapsed; elapsed.start();
                if (!AudioData::read(path, decoded, error, [](int) { return true; })) return 1;
                decodeUs += elapsed.nsecsElapsed() / 1000;
                elapsed.restart(); auto peaks = Waveform::preparePeaks(decoded); peaksUs += elapsed.nsecsElapsed() / 1000;
                elapsed.restart();
                QByteArray pcm; pcm.resize(decoded.samples.size() * 2);
                for (qsizetype i = 0; i < decoded.samples.size(); ++i)
                    qToLittleEndian(quint16(qint16(std::lround(decoded.samples[i] * 32767))), pcm.data() + i * 2);
                pcmUs += elapsed.nsecsElapsed() / 1000;
                if (peaks.maxima.isEmpty() || pcm.isEmpty()) return 1;
            }
            std::printf("BENCH %ds %s decode_ms=%.2f waveform_ms=%.2f ready_ms=%.2f legacy_play_pack_ms=%.2f\n", seconds, extension.toUtf8().constData(), decodeUs / 3000., peaksUs / 3000., (decodeUs + peaksUs) / 3000., pcmUs / 3000.); std::fflush(stdout);
        }
    }
    return 0;
}
