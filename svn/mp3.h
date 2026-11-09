#pragma once
#include "audio.h"

namespace Mp3 {
bool read(const QString &path, AudioData &out, QString &error, const AudioData::ReadProgress &progress = {});
bool write(const AudioData &audio, const QString &path, int bitrate, QString &error, const AudioData::ReadProgress &progress = {});
}
