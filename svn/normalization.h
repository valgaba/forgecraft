#pragma once
#include "dsp.h"

namespace Dsp {
struct NormalizeLevels {
    double peak = 0, rms = 0;
    qint64 includedFrames = 0;
    bool canceled = false;
};
NormalizeLevels scanNormalize(const AudioData &audio, qint64 first, qint64 last,
                              const Values &values, const std::atomic_bool *cancel = nullptr);
QString normalizeSamples(QVector<float> &samples, int channels, int rate, const Values &values,
                         const Values &cached, const std::atomic_bool *cancel);
}
