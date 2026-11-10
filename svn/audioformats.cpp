#include "audioformats.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr qint64 maxBytes = SampleBuffer::maxBytes;
QStringList rawArgs(const RawAudioOptions &raw) { return {"-f", raw.encoding, "-sample_rate", QString::number(raw.rate), "-ch_layout", raw.channels == 1 ? "mono" : "stereo"}; }
bool report(const AudioData::ReadProgress &progress, int value, QString &error) {
    if (progress && !progress(value)) { error = "Operación cancelada."; return false; } return true;
}
bool run(const QString &tool, const QStringList &arguments, QByteArray &output, QString &error,
         const AudioData::ReadProgress &progress, int begin, int end, double duration = 0, const QString &sizePath = {}) {
    const QString executable = AudioFormats::toolPath(tool);
    if (executable.isEmpty()) { error = "Falta " + tool + ". Ejecuta setup-codecs.ps1 o utiliza el paquete completo de ForgeCraft."; return false; }
    QProcess process; process.setProgram(executable); process.setArguments(arguments);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= 0x08000000; });
#endif
    process.start();
    if (!process.waitForStarted(5000)) { error = "No se pudo iniciar " + tool + ": " + process.errorString(); return false; }
    QByteArray messages; int percent = begin; QElapsedTimer stalled; stalled.start();
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(40);
        const auto bytes = process.readAllStandardOutput(); if (!bytes.isEmpty()) { output += bytes; stalled.restart(); }
        const auto stderrBytes = process.readAllStandardError(); if (!stderrBytes.isEmpty()) { messages += stderrBytes; stalled.restart(); }
        if (output.size() > 2 * 1024 * 1024) output = output.right(1024 * 1024);
        if (messages.size() > 128 * 1024) messages = messages.right(64 * 1024);
        if (duration > 0) {
            const int at = output.lastIndexOf("out_time_us=");
            if (at >= 0) { const auto line = output.mid(at + 12).split('\n').first(); percent = qBound(begin, begin + int(line.toDouble() / (duration * 1000000) * (end - begin)), end - 1); }
        }
        if ((!sizePath.isEmpty() && QFileInfo(sizePath).size() > maxBytes) || stalled.elapsed() > 120000) {
            process.kill(); process.waitForFinished(); error = stalled.elapsed() > 120000 ? "El códec no responde." : "El audio decodificado supera el límite de 256 GB."; return false;
        }
        if (!report(progress, percent, error)) { process.kill(); process.waitForFinished(); return false; }
    }
    output += process.readAllStandardOutput(); messages += process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) { error = QString::fromUtf8(messages).trimmed().right(2000); if (error.isEmpty()) error = "El códec no pudo completar la operación."; return false; }
    return report(progress, end, error);
}
QString pcmCodec(const AudioData &audio, const AudioExportOptions &options, bool bigEndian) {
    const int bits = options.bits ? options.bits : audio.bitDepth;
    if (options.bits ? options.floating : audio.floatFormat) return bigEndian ? "pcm_f32be" : "pcm_f32le";
    if (bits == 8) return bigEndian ? "pcm_s8" : "pcm_u8";
    return QString("pcm_s%1%2").arg(bits == 24 ? 24 : bits == 32 ? 32 : 16).arg(bigEndian ? "be" : "le");
}
}

const QVector<AudioFormats::Format> &AudioFormats::formats() {
    static const QVector<Format> catalog = {
        {"wav", "WAV (Microsoft)", "wav", {}, "wav", {"wav", "wave"}},
        {"mp3", "Audio MP3", "mp3", "libmp3lame", "mp3", {"mp3"}, true},
        {"ogg", "Ogg Vorbis", "ogg", "libvorbis", "ogg", {"ogg", "oga"}, true},
        {"opus", "Opus", "opus", "libopus", "opus", {"opus"}, true},
        {"flac", "FLAC", "flac", "flac", "flac", {"flac"}},
        {"wv", "WavPack", "wv", "wavpack", "wv", {"wv"}},
        {"m4a", "M4A (AAC)", "m4a", "aac", "ipod", {"m4a", "m4b"}, true},
        {"aac", "AAC (ADTS)", "aac", "aac", "adts", {"aac"}, true},
        {"ac3", "AC3", "ac3", "ac3", "ac3", {"ac3"}, true},
        {"amr", "AMR (banda estrecha)", "amr", "libopencore_amrnb", "amr", {"amr"}, true},
        {"wma", "WMA (versión 2)", "wma", "wmav2", "asf", {"wma"}, true},
        {"mp2", "MP2 (MPEG Layer II)", "mp2", "mp2", "mp2", {"mp2"}, true},
        {"aiff", "AIFF / AIFC sin comprimir", "aiff", {}, "aiff", {"aiff", "aif", "aifc"}},
        {"au", "AU / SND (Sun)", "au", {}, "au", {"au", "snd"}},
        {"caf", "CAF sin comprimir", "caf", {}, "caf", {"caf"}},
        {"w64", "Wave64 sin comprimir", "w64", {}, "w64", {"w64"}},
        {"rf64", "RF64 sin comprimir", "rf64", {}, "wav", {"rf64"}},
        {"raw", "Audio RAW / PCM sin cabecera", "raw", {}, {}, {"raw", "pcm"}},
        {"mka", "Matroska Audio (FLAC)", "mka", "flac", "matroska", {"mka"}},
        {"webm", "WebM Audio (Opus)", "webm", "libopus", "webm", {"webm"}, true}
    }; return catalog;
}
const AudioFormats::Format *AudioFormats::forPath(const QString &path) {
    const auto suffix = QFileInfo(path).suffix().toLower();
    for (const auto &format : formats()) if (format.extensions.contains(suffix)) return &format;
    return nullptr;
}
QString AudioFormats::filter(const Format &format) {
    QStringList patterns; for (const auto &extension : format.extensions) patterns << "*." + extension;
    return format.title + " (" + patterns.join(' ') + ")";
}
QStringList AudioFormats::saveFilters() { QStringList result; for (const auto &format : formats()) result << filter(format); return result; }
const AudioFormats::Format *AudioFormats::forFilter(const QString &name) { for (const auto &format : formats()) if (filter(format) == name) return &format; return nullptr; }
bool AudioFormats::isRaw(const QString &path) { const auto *f = forPath(path); return f && f->id == "raw"; }
bool AudioFormats::canOpen(const QString &path) {
    return forPath(path) || QStringList{"ape", "mpc", "mp4", "mov", "mkv", "avi", "3gp", "m4v", "aifc", "aif", "aiff", "ogv", "ogg", "flv", "dts", "m4r", "weba", "aif", "aiff", "alac"}.contains(QFileInfo(path).suffix().toLower());
}
QString AudioFormats::openFilters() {
    QStringList patterns; for (const auto &f : formats()) for (const auto &e : f.extensions) patterns << "*." + e;
    patterns << "*.ape" << "*.mpc" << "*.mp4" << "*.mkv" << "*.mov" << "*.avi" << "*.3gp" << "*.dts";
    auto filters = saveFilters(); filters.prepend("Todos los archivos compatibles (" + patterns.join(' ') + ")");
    filters << "Otros archivos compatibles con FFmpeg (*.ape *.mpc *.mp4 *.mkv *.mov *.avi *.3gp *.dts)" << "Todos los archivos (*)";
    return filters.join(";;");
}
QString AudioFormats::pathForFormat(const QString &path, const Format &format) {
    if (const auto *existing = forPath(path)) { Q_UNUSED(existing) const auto suffix = QFileInfo(path).suffix(); return path.chopped(suffix.size()) + format.suffix; }
    return path + "." + format.suffix;
}
QString AudioFormats::toolPath(const QString &name) {
    const QString file = name + ".exe";
    for (const auto &folder : {QCoreApplication::applicationDirPath() + "/codecs", QCoreApplication::applicationDirPath(), QCoreApplication::applicationDirPath() + "/../../svn/third_party/ffmpeg/bin"}) {
        const auto path = QDir(folder).absoluteFilePath(file); if (QFileInfo::exists(path)) return path;
    }
    return {};
}
bool AudioFormats::read(const QString &path, AudioData &out, QString &error, const AudioData::ReadProgress &progress, const RawAudioOptions &raw) {
    error.clear(); if (!report(progress, 0, error)) return false;
    if (isRaw(path) && (raw.rate < 8000 || raw.rate > 384000 || raw.channels < 1 || raw.channels > 2 || !QStringList{"u8", "s8", "s16le", "s24le", "s32le", "f32le", "s16be", "s24be", "s32be", "f32be"}.contains(raw.encoding))) { error = "Parámetros RAW no válidos."; return false; }
    if (!QFileInfo(path).isFile()) { error = "No se encuentra el archivo de audio."; return false; }
    QByteArray probe; QStringList probeArgs{"-v", "error", "-protocol_whitelist", "file,pipe"};
    if (isRaw(path)) probeArgs += rawArgs(raw);
    probeArgs << "-select_streams" << "a:0" << "-show_entries" << "stream=sample_fmt,sample_rate,channels,bits_per_raw_sample,bits_per_sample:format=duration" << "-of" << "json" << path;
    if (!run("ffprobe", probeArgs, probe, error, progress, 0, 4)) return false;
    const auto info = QJsonDocument::fromJson(probe).object(); const auto streams = info["streams"].toArray();
    if (streams.isEmpty()) { error = "El archivo no contiene una pista de audio compatible."; return false; }
    const auto stream = streams.first().toObject();
    const int rate = stream["sample_rate"].toString().toInt(), channels = qMin(2, stream["channels"].toInt());
    const double duration = info["format"].toObject()["duration"].toString().toDouble();
    if (rate < 8000 || rate > 384000 || channels < 1 || (duration > 0 && duration * rate * channels * 4 > maxBytes)) { error = "El formato o el tamaño del audio supera los límites del editor (mono/estéreo, 8–384 kHz, 256 GB)."; return false; }
    QTemporaryDir temp;
    if (!temp.isValid()) { error = "No se pudo crear el archivo temporal de audio: " + temp.errorString(); return false; }
    const QString decoded = temp.filePath("decoded.f32");
    QStringList args{"-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-protocol_whitelist", "file,pipe"};
    if (isRaw(path)) args += rawArgs(raw);
    args << "-i" << path << "-map" << "0:a:0" << "-vn" << "-sn" << "-dn" << "-ac" << QString::number(channels) << "-ar" << QString::number(rate) << "-c:a" << "pcm_f32le" << "-f" << "f32le" << "-progress" << "pipe:1" << decoded;
    QByteArray output; if (!run("ffmpeg", args, output, error, progress, 4, 90, duration, decoded)) return false;
    QFile file(decoded); if (!file.open(QIODevice::ReadOnly) || file.size() > maxBytes || file.size() % (4 * channels)) { error = "Los datos de audio decodificados no son válidos."; return false; }
    AudioData audio; audio.rate = rate; audio.channels = channels; audio.bitDepth = 32; audio.floatFormat = true;
    audio.samples.resize(file.size() / 4); float *destination = audio.samples.data(); qsizetype at = 0;
    while (!file.atEnd()) {
        const auto bytes = file.read(1024 * 1024); if (bytes.isEmpty() && !file.atEnd()) { error = file.errorString(); return false; }
        for (int i = 0; i + 4 <= bytes.size(); i += 4) { const quint32 bits = qFromLittleEndian<quint32>(bytes.constData() + i); float value; std::memcpy(&value, &bits, 4); destination[at++] = std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0; }
        if (!report(progress, 90 + int(at * 9 / qMax(qsizetype(1), audio.samples.size())), error)) return false;
    }
    if (audio.samples.isEmpty()) { error = "El archivo no contiene audio."; return false; }
    const int bits = qMax(stream["bits_per_sample"].toInt(), stream["bits_per_raw_sample"].toString().toInt());
    if (bits == 8 || bits == 16 || bits == 24) { audio.bitDepth = bits; audio.floatFormat = false; }
    if (bits == 32 && stream["sample_fmt"].toString().startsWith("s32")) audio.floatFormat = false;
    if (!report(progress, 100, error)) return false;
    out = std::move(audio); return true;
}
bool AudioFormats::write(const AudioData &audio, const QString &path, const AudioExportOptions &options, QString &error, const AudioData::ReadProgress &progress) {
    error.clear(); const auto *format = forPath(path);
    if (!format || format->id == "mp3") { error = "Formato de exportación no válido."; return false; }
    if (audio.channels < 1 || audio.channels > 2 || audio.rate < 8000 || audio.frames() <= 0 || !report(progress, 0, error)) { if (error.isEmpty()) error = "No hay audio válido para exportar."; return false; }
    if ((options.rate && (options.rate < 8000 || options.rate > 384000)) || options.channels < 0 || options.channels > 2) { error = "Frecuencia o canales de exportación no válidos."; return false; }
    const QFileInfo target(path); QTemporaryDir temp(target.absolutePath() + "/.forgecraft-export-XXXXXX");
    if (!temp.isValid()) { error = "No se puede escribir en la carpeta de destino: " + temp.errorString(); return false; }
    const QString input = temp.filePath("input.f32"), outputPath = temp.filePath("output." + format->suffix);
    QFile file(input); if (!file.open(QIODevice::WriteOnly)) { error = file.errorString(); return false; }
    const float *samples = audio.samples.constData(); QByteArray block(65536, Qt::Uninitialized);
    for (qsizetype first = 0; first < audio.samples.size(); first += block.size() / 4) {
        const int count = int(qMin(qsizetype(block.size() / 4), audio.samples.size() - first));
        for (int i = 0; i < count; ++i) { float value = std::isfinite(samples[first + i]) ? std::clamp(samples[first + i], -1.0f, 1.0f) : 0; quint32 bits; std::memcpy(&bits, &value, 4); qToLittleEndian(bits, block.data() + i * 4); }
        if (file.write(block.constData(), count * 4) != count * 4) { error = file.errorString(); return false; }
        if (!report(progress, int(first * 15 / audio.samples.size()), error)) return false;
    }
    file.close();
    int rate = options.rate ? options.rate : audio.rate, channels = options.channels ? options.channels : audio.channels;
    QString encoder = format->encoder, muxer = format->muxer;
    if (format->id == "opus" || format->id == "webm" || format->id == "ac3") rate = 48000;
    if (format->id == "amr") { rate = 8000; channels = 1; }
    if (format->id == "wma" || format->id == "mp2" || format->id == "m4a" || format->id == "aac" || format->id == "ogg") if (rate > 48000 || rate < 16000) rate = 44100;
    QStringList args{"-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-f", "f32le", "-ar", QString::number(audio.rate), "-ac", QString::number(audio.channels), "-i", input, "-map", "0:a:0", "-ar", QString::number(rate), "-ac", QString::number(channels)};
    if (encoder.isEmpty()) encoder = pcmCodec(audio, options, format->id == "aiff" || format->id == "au");
    if (format->id == "raw") { muxer = options.rawEncoding; encoder = muxer == "f32le" ? "pcm_f32le" : muxer == "f32be" ? "pcm_f32be" : muxer == "u8" ? "pcm_u8" : "pcm_" + muxer; }
    args << "-c:a" << encoder;
    if (format->lossy) args << "-b:a" << (format->id == "amr" ? "12200" : QString::number(options.bitrate * 1000));
    if (format->id == "flac" || format->id == "mka") args << "-compression_level" << QString::number(qBound(0, options.compression, 12)) << "-sample_fmt" << ((options.bits ? options.bits : audio.bitDepth) <= 16 ? "s16" : "s32");
    if (format->id == "wv") args << "-compression_level" << QString::number(qBound(0, options.compression, 8)) << "-sample_fmt" << (options.floating || (!options.bits && audio.floatFormat) ? "fltp" : (options.bits ? options.bits : audio.bitDepth) <= 16 ? "s16p" : "s32p");
    if (format->id == "wv" && !options.floating && !(options.bits == 0 && audio.floatFormat)) args << "-bits_per_raw_sample" << QString::number(options.bits ? options.bits : audio.bitDepth);
    if (format->id == "rf64" || format->id == "wav") args << "-rf64" << "always";
    args << "-f" << muxer << "-progress" << "pipe:1" << outputPath;
    QByteArray output; if (!run("ffmpeg", args, output, error, progress, 15, 94, audio.duration())) return false;
    QFile encoded(outputPath); if (!encoded.open(QIODevice::ReadOnly)) { error = encoded.errorString(); return false; }
    QSaveFile saved(path); if (!saved.open(QIODevice::WriteOnly)) { error = saved.errorString(); return false; }
    while (!encoded.atEnd()) {
        const auto chunk = encoded.read(1024 * 1024); if (chunk.isEmpty() && !encoded.atEnd()) { error = encoded.errorString(); return false; }
        if (saved.write(chunk) != chunk.size()) { error = saved.errorString(); return false; }
        if (!report(progress, 94 + int(encoded.pos() * 5 / qMax(qint64(1), encoded.size())), error)) return false;
    }
    if (!report(progress, 100, error)) return false;
    if (!saved.commit()) { error = saved.errorString(); return false; }
    return true;
}
