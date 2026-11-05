#include "mp3.h"
#include "third_party/mp3/include/mpg123.h"
#include "third_party/mp3/include/lame.h"
#include <QCoreApplication>
#include <QFile>
#include <QLibrary>
#include <QSaveFile>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
// Resolve the separate, replaceable codec DLLs from the application directory.
// No dependency on the user's PATH or another audio application's installation.
#define DECLARE_API(name) decltype(&::name) name = nullptr;
#define LOAD_API(name) name = reinterpret_cast<decltype(name)>(library.resolve(#name)); if (!name) { error = "Biblioteca MP3 incompatible: " #name; return false; }
struct Decoder {
    QLibrary library{QCoreApplication::applicationDirPath() + "/libmpg123-0.dll"};
    DECLARE_API(mpg123_init)
    DECLARE_API(mpg123_exit)
    DECLARE_API(mpg123_new)
    DECLARE_API(mpg123_delete)
    DECLARE_API(mpg123_format_none)
    DECLARE_API(mpg123_format2)
    DECLARE_API(mpg123_open_feed)
    DECLARE_API(mpg123_feed)
    DECLARE_API(mpg123_read)
    DECLARE_API(mpg123_getformat)
    DECLARE_API(mpg123_strerror)
    bool load(QString &error) {
        if (!library.load()) { error = "No se pudo cargar libmpg123-0.dll. Reinstala las bibliotecas MP3 junto a ForgeCraft.\n" + library.errorString(); return false; }
        LOAD_API(mpg123_init) LOAD_API(mpg123_exit) LOAD_API(mpg123_new) LOAD_API(mpg123_delete)
        LOAD_API(mpg123_format_none) LOAD_API(mpg123_format2) LOAD_API(mpg123_open_feed)
        LOAD_API(mpg123_feed) LOAD_API(mpg123_read) LOAD_API(mpg123_getformat) LOAD_API(mpg123_strerror)
        return true;
    }
};
struct Encoder {
    QLibrary library{QCoreApplication::applicationDirPath() + "/libmp3lame-0.dll"};
    DECLARE_API(lame_init)
    DECLARE_API(lame_close)
    DECLARE_API(lame_set_in_samplerate)
    DECLARE_API(lame_set_out_samplerate)
    DECLARE_API(lame_set_num_channels)
    DECLARE_API(lame_set_num_samples)
    DECLARE_API(lame_set_brate)
    DECLARE_API(lame_set_quality)
    DECLARE_API(lame_set_bWriteVbrTag)
    DECLARE_API(lame_set_write_id3tag_automatic)
    DECLARE_API(lame_init_params)
    DECLARE_API(lame_encode_buffer_interleaved_ieee_float)
    DECLARE_API(lame_encode_buffer_ieee_float)
    DECLARE_API(lame_encode_flush)
    DECLARE_API(lame_get_lametag_frame)
    bool load(QString &error) {
        if (!library.load()) { error = "No se pudo cargar libmp3lame-0.dll. Reinstala las bibliotecas MP3 junto a ForgeCraft.\n" + library.errorString(); return false; }
        LOAD_API(lame_init) LOAD_API(lame_close) LOAD_API(lame_set_in_samplerate) LOAD_API(lame_set_out_samplerate)
        LOAD_API(lame_set_num_channels) LOAD_API(lame_set_num_samples) LOAD_API(lame_set_brate) LOAD_API(lame_set_quality)
        LOAD_API(lame_set_bWriteVbrTag) LOAD_API(lame_set_write_id3tag_automatic) LOAD_API(lame_init_params)
        LOAD_API(lame_encode_buffer_interleaved_ieee_float) LOAD_API(lame_encode_buffer_ieee_float)
        LOAD_API(lame_encode_flush) LOAD_API(lame_get_lametag_frame)
        return true;
    }
};
#undef DECLARE_API
#undef LOAD_API
}

bool Mp3::read(const QString &path, AudioData &out, QString &error, const AudioData::ReadProgress &progress) {
    error.clear();
    auto report = [&](int percent) { if (progress && !progress(percent)) { error = "Carga cancelada."; return false; } return true; };
    if (!report(0)) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 512 * 1024 * 1024) { error = "El límite de apertura es 512 MB por archivo."; return false; }
    const qint64 fileSize = file.size();
    Decoder api; if (!api.load(error)) return false;
    if (api.mpg123_init() != MPG123_OK) { error = "No se pudo iniciar el decodificador MP3."; return false; }
    auto shutdown = qScopeGuard([&] { api.mpg123_exit(); });
    int code = 0; auto *handle = api.mpg123_new(nullptr, &code);
    if (!handle) { error = "No se pudo crear el decodificador MP3."; return false; }
    auto cleanup = qScopeGuard([&] { api.mpg123_delete(handle); });
    if (api.mpg123_format_none(handle) != MPG123_OK ||
        api.mpg123_format2(handle, 0, MPG123_MONO | MPG123_STEREO, MPG123_ENC_FLOAT_32) != MPG123_OK ||
        api.mpg123_open_feed(handle) != MPG123_OK) {
        error = QString::fromUtf8(api.mpg123_strerror(handle)); return false;
    }
    AudioData result; bool haveFormat = false, eof = false;
    float decoded[16384];
    for (;;) {
        if (!report(int(qMin(qint64(99), file.pos() * 99 / qMax(qint64(1), fileSize))))) return false;
        size_t done = 0;
        int status = api.mpg123_read(handle, decoded, sizeof(decoded), &done);
        if (status == MPG123_NEW_FORMAT) {
            long rate = 0; int channels = 0, encoding = 0;
            if (api.mpg123_getformat(handle, &rate, &channels, &encoding) != MPG123_OK ||
                (channels != 1 && channels != 2) || encoding != MPG123_ENC_FLOAT_32 ||
                (haveFormat && (result.rate != rate || result.channels != channels))) {
                error = "El MP3 tiene un formato no compatible o cambia de frecuencia/canales."; return false;
            }
            result.rate = int(rate); result.channels = channels; haveFormat = true;
        }
        if (done) {
            if (!haveFormat || done % (sizeof(float) * result.channels)) { error = "Datos MP3 no válidos."; return false; }
            if ((quint64(result.samples.size()) * sizeof(float) + done) > 512 * 1024 * 1024) {
                error = "El audio MP3 decodificado supera el límite de 512 MB en memoria."; return false;
            }
            qsizetype oldSize = result.samples.size(), count = qsizetype(done / sizeof(float));
            result.samples.resize(oldSize + count);
            float *destination = result.samples.data() + oldSize;
            for (qsizetype i = 0; i < count; ++i) destination[i] = std::isfinite(decoded[i]) ? std::clamp(decoded[i], -1.0f, 1.0f) : 0;
        }
        if (status == MPG123_DONE) break;
        if (status == MPG123_NEED_MORE) {
            if (eof) break;
            QByteArray input = file.read(65536);
            if (input.isEmpty()) {
                if (file.error() != QFileDevice::NoError) { error = file.errorString(); return false; }
                eof = true;
            } else if (api.mpg123_feed(handle, reinterpret_cast<const unsigned char *>(input.constData()), size_t(input.size())) != MPG123_OK) {
                error = QString::fromUtf8(api.mpg123_strerror(handle)); return false;
            }
        } else if (status != MPG123_OK && status != MPG123_NEW_FORMAT) {
            error = QString::fromUtf8(api.mpg123_strerror(handle)); return false;
        }
    }
    if (!haveFormat || result.samples.isEmpty()) { error = "El archivo no contiene audio MP3 válido."; return false; }
    if (!report(100)) return false;
    out = std::move(result); return true;
}

bool Mp3::write(const AudioData &audio, const QString &path, int bitrate, QString &error) {
    error.clear();
    if (!audio.frames() || (audio.channels != 1 && audio.channels != 2)) { error = "No hay audio para guardar en MP3."; return false; }
    if (bitrate != 128 && bitrate != 192 && bitrate != 256 && bitrate != 320) { error = "Selecciona 128, 192, 256 o 320 kbps."; return false; }
    Encoder api; if (!api.load(error)) return false;
    auto *handle = api.lame_init();
    if (!handle) { error = "No se pudo iniciar el codificador MP3."; return false; }
    auto cleanup = qScopeGuard([&] { api.lame_close(handle); });
    const int outRate = audio.rate == 32000 || audio.rate == 44100 || audio.rate == 48000 ? audio.rate : 44100;
    if (api.lame_set_in_samplerate(handle, audio.rate) < 0 || api.lame_set_out_samplerate(handle, outRate) < 0 ||
        api.lame_set_num_channels(handle, audio.channels) < 0 || api.lame_set_num_samples(handle, static_cast<unsigned long>(audio.frames())) < 0 ||
        api.lame_set_brate(handle, bitrate) < 0 || api.lame_set_quality(handle, 2) < 0 || api.lame_set_bWriteVbrTag(handle, 1) < 0) {
        error = "No se pudo configurar el codificador MP3."; return false;
    }
    // Keep the first frame at offset zero so we can update the gapless LAME tag.
    api.lame_set_write_id3tag_automatic(handle, 0);
    if (api.lame_init_params(handle) < 0) { error = "Frecuencia o canales no compatibles con el codificador MP3."; return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { error = file.errorString(); return false; }
    unsigned char buffer[32768];
    auto writeChunk = [&](int count) {
        if (count < 0) { error = "Error de codificación MP3 (" + QString::number(count) + ")."; return false; }
        if (file.write(reinterpret_cast<const char *>(buffer), count) != count) { error = file.errorString(); return false; }
        return true;
    };
    for (qint64 frame = 0; frame < audio.frames(); frame += 8192) {
        int count = int(qMin(qint64(8192), audio.frames() - frame));
        const float *pcm = audio.samples.constData() + frame * audio.channels;
        int written = audio.channels == 2 ? api.lame_encode_buffer_interleaved_ieee_float(handle, pcm, count, buffer, sizeof(buffer))
                                         : api.lame_encode_buffer_ieee_float(handle, pcm, pcm, count, buffer, sizeof(buffer));
        if (!writeChunk(written)) return false;
    }
    if (!writeChunk(api.lame_encode_flush(handle, buffer, sizeof(buffer)))) return false;
    size_t tagSize = api.lame_get_lametag_frame(handle, buffer, sizeof(buffer));
    if (tagSize > sizeof(buffer) || tagSize == 0 || qint64(tagSize) > file.pos() || !file.seek(0)) {
        error = "No se pudo finalizar la cabecera de duración MP3."; return false;
    }
    if (!writeChunk(int(tagSize))) return false;
    if (!file.commit()) { error = file.errorString(); return false; }
    return true;
}
