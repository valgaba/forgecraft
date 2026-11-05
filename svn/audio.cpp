#include "audio.h"
#include "mp3.h"
#include <QFile>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

static quint16 u16(const char *p) { return qFromLittleEndian<quint16>(p); }
static quint32 u32(const char *p) { return qFromLittleEndian<quint32>(p); }
static void put16(QByteArray &b, quint16 n) { char p[2]; qToLittleEndian(n, p); b.append(p, 2); }
static void put32(QByteArray &b, quint32 n) { char p[4]; qToLittleEndian(n, p); b.append(p, 4); }

bool AudioData::read(const QString &path, AudioData &out, QString &error, const ReadProgress &progress) {
    error.clear();
    if (path.endsWith(".mp3", Qt::CaseInsensitive)) return Mp3::read(path, out, error, progress);
    auto report = [&](int percent) { if (progress && !progress(percent)) { error = "Carga cancelada."; return false; } return true; };
    if (!report(0)) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 512 * 1024 * 1024) { error = "El límite de esta versión es 512 MB por archivo."; return false; }
    QByteArray bytes;
    const qint64 fileSize = file.size();
    bytes.reserve(fileSize);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) { error = file.errorString(); return false; }
        if (chunk.isEmpty()) break;
        bytes.append(chunk);
        if (bytes.size() > 512 * 1024 * 1024) { error = "El archivo supera el límite de 512 MB."; return false; }
        if (!report(int(qMin(qint64(30), bytes.size() * 30 / qMax(qint64(1), fileSize))))) return false;
    }
    if (bytes.size() < 12 || bytes.first(4) != "RIFF" || bytes.mid(8, 4) != "WAVE") {
        error = "Selecciona un archivo WAV o MP3 válido."; return false;
    }
    int format = 0, channels = 0, bits = 0, rate = 0, align = 0;
    qint64 pcmOffset = -1, pcmSize = 0;
    for (qint64 pos = 12; pos + 8 <= bytes.size();) {
        if (!report(30)) return false;
        const quint32 size = u32(bytes.constData() + pos + 4);
        if (pos + 8 + size > bytes.size()) { error = "El archivo WAV está truncado."; return false; }
        const QByteArray id = bytes.mid(pos, 4);
        const char *p = bytes.constData() + pos + 8;
        if (id == "fmt " && size >= 16) {
            format = u16(p); channels = u16(p + 2); rate = int(u32(p + 4));
            align = u16(p + 12); bits = u16(p + 14);
            if (format == 0xfffe && size >= 40) format = u16(p + 24);
        } else if (id == "data") { pcmOffset = pos + 8; pcmSize = size; }
        pos += 8 + size + (size & 1);
    }
    if ((channels != 1 && channels != 2) || rate < 8000 || rate > 384000 ||
        !((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32)) ||
        align != channels * (bits / 8) || pcmOffset < 0 || pcmSize % align) {
        error = "Formato no compatible. Usa WAV mono/estéreo PCM de 8/16/24/32 bits o float de 32 bits."; return false;
    }
    AudioData result; result.rate = rate; result.channels = channels;
    result.bitDepth = bits;                       // 8, 16, 24 o 32
    result.floatFormat = format == 3;             // 32 en coma flotante
    result.samples.resize(pcmSize / (bits / 8));
    float *destination = result.samples.data();
    const char *pcm = bytes.constData() + pcmOffset;
    for (qsizetype i = 0; i < result.samples.size(); ++i) {
        if ((i & 16383) == 0 && !report(30 + int(i * 69 / qMax(qsizetype(1), result.samples.size())))) return false;
        const char *p = pcm + i * (bits / 8);
        double v = 0;
        if (format == 3) { quint32 raw = u32(p); float f; std::memcpy(&f, &raw, 4); v = std::isfinite(f) ? f : 0; }
        else if (bits == 8) v = (quint8(*p) - 128) / 128.0;
        else if (bits == 16) v = qint16(u16(p)) / 32768.0;
        else if (bits == 24) { qint32 n = quint8(p[0]) | (quint8(p[1]) << 8) | (quint8(p[2]) << 16); if (n & 0x800000) n |= ~0xffffff; v = n / 8388608.0; }
        else v = qint32(u32(p)) / 2147483648.0;
        destination[i] = float(std::clamp(v, -1.0, 1.0));
    }
    if (!report(100)) return false;
    out = std::move(result); return true;
}

bool AudioData::write(const QString &path, QString &error, int mp3Bitrate) const {
    error.clear();
    if (path.endsWith(".mp3", Qt::CaseInsensitive)) return Mp3::write(*this, path, mp3Bitrate, error);
    const int bits = floatFormat ? 32 : (bitDepth == 8 ? 8 : bitDepth == 24 ? 24 : bitDepth == 32 ? 32 : 16);
    const int bytesPerSample = bits / 8;
    const quint16 format = floatFormat ? 3 : 1;
    quint64 dataSize = quint64(samples.size()) * bytesPerSample;
    if (dataSize + 36 + (dataSize & 1) > 0xffffffffu) { error = "Audio demasiado largo para WAV RIFF."; return false; }
    QByteArray b("RIFF"); put32(b, 36 + dataSize + (dataSize & 1)); b += "WAVEfmt "; put32(b, 16);
    put16(b, format); put16(b, channels); put32(b, rate); put32(b, rate * channels * bytesPerSample);
    put16(b, channels * bytesPerSample); put16(b, bits); b += "data"; put32(b, dataSize);
    for (float f : samples) {
        const double v = std::clamp(double(f), -1.0, 1.0);
        if (bits == 8) b.append(char(int(std::clamp(std::lround(v * 128) + 128, 0l, 255l))));
        else if (bits == 16) put16(b, quint16(qint16(std::lround(v * 32767))));
        else if (bits == 24) {
            const qint32 n = qint32(std::lround(v * 8388607));
            char p[3]{char(n & 0xff), char((n >> 8) & 0xff), char((n >> 16) & 0xff)};
            b.append(p, 3);
        } else if (floatFormat) {
            const float sample = float(v);
            char p[4]; std::memcpy(p, &sample, 4); b.append(p, 4);
        } else put32(b, quint32(qint32(std::lround(v * 2147483647.0))));
    }
    if (dataSize & 1) b.append(char(0));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(b) != b.size() || !file.commit()) { error = file.errorString(); return false; }
    return true;
}

AudioData AudioData::tone(double seconds, double frequency, int rate, int channels) {
    AudioData d; d.rate = rate; d.channels = channels; d.samples.resize(qint64(seconds * rate) * channels);
    for (qint64 f = 0; f < d.frames(); ++f)
        for (int c = 0; c < channels; ++c) d.samples[f * channels + c] = float(0.5 * std::sin(6.283185307179586 * frequency * f / rate));
    return d;
}

void AudioData::process(qint64 first, qint64 last, const QString &operation, double gain) {
    first = std::clamp(first, qint64(0), frames()); last = std::clamp(last, first, frames());
    if (last == first) return;
    if (operation == "reverse") {
        for (qint64 a = first, b = last - 1; a < b; ++a, --b)
            for (int c = 0; c < channels; ++c) std::swap(samples[a * channels + c], samples[b * channels + c]);
        return;
    }
    if (operation == "normalize") {
        double peak = 0; for (qint64 i = first * channels; i < last * channels; ++i) peak = std::max(peak, double(std::abs(samples[i])));
        gain = peak > 0 ? 0.999 / peak : 1;
    }
    for (qint64 f = first; f < last; ++f) {
        double multiplier = gain;
        double t = last - first > 1 ? double(f - first) / (last - first - 1) : 1;
        if (operation == "fadein") multiplier = t;
        if (operation == "fadeout") multiplier = 1 - t;
        if (operation == "silence") multiplier = 0;
        for (int c = 0; c < channels; ++c) samples[f * channels + c] = float(std::clamp(samples[f * channels + c] * multiplier, -1.0, 1.0));
    }
}
AudioData AudioData::fragment(qint64 first, qint64 last) const {
    AudioData part;
    part.rate = rate; part.channels = channels; part.bitDepth = bitDepth; part.floatFormat = floatFormat;
    first = std::clamp(first, qint64(0), frames()); last = std::clamp(last, first, frames());
    part.samples = samples.mid(first * channels, (last - first) * channels);
    for (const auto &marker : markers)
        if (marker.frame >= first && marker.frame <= last) part.markers.append({marker.frame - first, marker.name});
    return part;
}
void AudioData::shiftMarkers(qint64 at, qint64 frames) {
    for (auto &marker : markers) if (marker.frame >= at) marker.frame = qMax(qint64(0), marker.frame + frames);
}
void AudioData::removeMarkers(qint64 first, qint64 last) {
    QVector<Marker> kept;
    for (const auto &marker : markers) {
        if (marker.frame < first) kept.append(marker);
        else if (marker.frame > last) kept.append({marker.frame - (last - first), marker.name});
    }
    markers = kept;
}
void AudioData::trimMarkers(qint64 first, qint64 last) {
    QVector<Marker> kept;
    for (const auto &marker : markers)
        if (marker.frame >= first && marker.frame <= last) kept.append({marker.frame - first, marker.name});
    markers = kept;
}
