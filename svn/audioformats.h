#pragma once
#include "audio.h"
#include <QStringList>

struct AudioExportOptions {
    int rate = 0, channels = 0, bits = 0, bitrate = 192, compression = 5;
    bool floating = false;
    QString rawEncoding = "s16le";
};
struct RawAudioOptions { int rate = 44100, channels = 2; QString encoding = "s16le"; };

namespace AudioFormats {
struct Format { QString id, title, suffix, encoder, muxer; QStringList extensions; bool lossy = false; };
const QVector<Format> &formats();
const Format *forPath(const QString &path);
const Format *forFilter(const QString &filter);
QString filter(const Format &format);
QStringList saveFilters();
QString openFilters();
QString pathForFormat(const QString &path, const Format &format);
bool canOpen(const QString &path);
bool isRaw(const QString &path);
QString toolPath(const QString &name);
bool read(const QString &path, AudioData &out, QString &error, const AudioData::ReadProgress &progress = {}, const RawAudioOptions &raw = {});
bool write(const AudioData &audio, const QString &path, const AudioExportOptions &options, QString &error, const AudioData::ReadProgress &progress = {});
}
