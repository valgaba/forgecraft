#pragma once
#include <QVector>
#include <QString>
#include <functional>

// Marca de posición dentro de un documento: se dibuja en la regla y viaja con
// el audio cuando se copia o se arrastra un fragmento.
struct Marker {
    qint64 frame = 0;
    QString name;
};

struct AudioData {
    int rate = 44100;
    int channels = 2;
    int bitDepth = 16;
    bool floatFormat = false;   // 32 bits en coma flotante (formato 3 de WAV)
    QVector<float> samples;
    QVector<Marker> markers;
    qint64 frames() const { return samples.size() / channels; }
    double duration() const { return double(frames()) / rate; }
    // Porcentaje 0..100; devolver false cancela sin modificar out.
    using ReadProgress = std::function<bool(int)>;
    static bool read(const QString &path, AudioData &out, QString &error, const ReadProgress &progress = {});
    bool write(const QString &path, QString &error, int mp3Bitrate = 192, const ReadProgress &progress = {}) const;
    static AudioData tone(double seconds, double frequency, int rate = 44100, int channels = 2);
    void process(qint64 first, qint64 last, const QString &operation, double gain = 1.0);
    // Tramo del documento con las marcas que caen dentro, ya referidas a su inicio.
    AudioData fragment(qint64 first, qint64 last) const;
    // Mantiene las marcas coherentes al insertar o quitar audio.
    void shiftMarkers(qint64 at, qint64 frames);
    void removeMarkers(qint64 first, qint64 last);
    void trimMarkers(qint64 first, qint64 last);
};
