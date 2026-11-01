#pragma once
#include <QVector>
#include <QString>

struct AudioData {
    int rate = 44100;
    int channels = 2;
    int bitDepth = 16;
    QVector<float> samples;
    qint64 frames() const { return samples.size() / channels; }
    double duration() const { return double(frames()) / rate; }
    static bool read(const QString &path, AudioData &out, QString &error);
    bool write(const QString &path, QString &error, int mp3Bitrate = 192) const;
    static AudioData tone(double seconds, double frequency, int rate = 44100, int channels = 2);
    void process(qint64 first, qint64 last, const QString &operation, double gain = 1.0);
};
