#include "normalization.h"
#include <algorithm>
#include <cmath>

Dsp::NormalizeLevels Dsp::scanNormalize(const AudioData &audio, qint64 first, qint64 last,
                                       const Values &settings, const std::atomic_bool *cancel) {
    NormalizeLevels result;
    const auto values = [&] { auto v = defaults("normalize"); for (auto i = settings.cbegin(); i != settings.cend(); ++i) v[i.key()] = i.value(); return v; }();
    first = std::clamp(first, qint64(0), audio.frames()); last = std::clamp(last, first, audio.frames());
    const double threshold = values["ignore"] <= -96 ? 0 : std::pow(10.0, values["ignore"] / 10.0);
    const double attack = std::exp(-1.0 / (audio.rate * values["attack"] / 1000.0));
    const double release = std::exp(-1.0 / (audio.rate * values["release"] / 1000.0));
    const double highpass = std::exp(-2.0 * 3.141592653589793 * 150 / audio.rate);
    const double lowpass = 1 - std::exp(-2.0 * 3.141592653589793 * 3000 / audio.rate);
    double previous[2]{}, high[2]{}, low[2]{}, envelope = 0, sum = 0;
    const float *samples = audio.samples.constData();
    for (qint64 f = first; f < last; ++f) {
        if ((f & 4095) == 0 && cancel && cancel->load()) { result.canceled = true; return result; }
        double power = 0;
        for (int c = 0; c < audio.channels; ++c) {
            const double x = samples[f * audio.channels + c];
            result.peak = std::max(result.peak, std::abs(x));
            double weighted = x;
            if (values["contour"]) {
                // Contorno perceptual propio: atenúa graves y realza presencia.
                // No es una medición LUFS ni una ponderación IEC certificada.
                high[c] = highpass * (high[c] + x - previous[c]); previous[c] = x;
                low[c] += lowpass * (high[c] - low[c]);
                weighted = high[c] + .5 * (high[c] - low[c]);
            }
            power += weighted * weighted / audio.channels;
        }
        const double coefficient = power > envelope ? attack : release;
        envelope = power + coefficient * (envelope - power);
        if (envelope >= threshold && power > 0) { sum += power; ++result.includedFrames; }
        else if (threshold == 0) { ++result.includedFrames; }
    }
    result.rms = result.includedFrames ? std::sqrt(sum / result.includedFrames) : 0;
    return result;
}

QString Dsp::normalizeSamples(SampleBuffer &samples, int channels, int rate, const Values &values,
                             const Values &cached, const std::atomic_bool *cancel) {
    NormalizeLevels levels;
    if (cached.value("scan_cached")) {
        levels.peak = cached.value("scan_peak"); levels.rms = cached.value("scan_rms");
        if (!std::isfinite(levels.peak) || !std::isfinite(levels.rms) || levels.peak < 0 || levels.rms < 0)
            return "El análisis guardado no es válido.";
    } else if (!values["mode"]) {
        for (qsizetype i = 0; i < samples.size(); ++i) {
            if ((i & 8191) == 0 && cancel && cancel->load()) return {};
            levels.peak = std::max(levels.peak, double(std::abs(samples[i])));
        }
    } else {
        AudioData segment; segment.samples = samples; segment.channels = channels; segment.rate = rate;
        levels = scanNormalize(segment, 0, segment.frames(), values, cancel);
        if (levels.canceled) return {};
    }
    const double measured = values["mode"] ? levels.rms : levels.peak;
    if (measured <= 1e-12) return {};
    double gain = std::pow(10.0, values["target"] / 20.0) / measured;
    const int policy = int(values["clipping"]);
    if (levels.peak * gain > 1 && policy == 1) gain = 1.0 / levels.peak;
    if (levels.peak * gain > 1 && policy == 3) return "La normalización produciría clipping. Reduce el objetivo o cambia la política de clipping.";
    double limiter = 1;
    const double recovery = 1 - std::exp(-1.0 / (.05 * rate));
    for (qint64 frame = 0; frame < samples.size() / channels; ++frame) {
        if ((frame & 4095) == 0 && cancel && cancel->load()) return {};
        double peak = 0;
        for (int c = 0; c < channels; ++c) peak = std::max(peak, std::abs(samples[frame * channels + c] * gain));
        if (policy == 0) {
            limiter += (1 - limiter) * recovery;
            if (peak > 1) limiter = std::min(limiter, 1.0 / peak);
        }
        for (int c = 0; c < channels; ++c)
            samples[frame * channels + c] = float(std::clamp(samples[frame * channels + c] * gain * (policy == 0 ? limiter : 1), -1.0, 1.0));
    }
    return {};
}
