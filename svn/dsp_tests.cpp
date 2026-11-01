#include "dsp.h"
#include <QFile>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <cmath>
#include <cstdio>
#include <limits>

int dspTests() {
    int failures = 0;
    auto check = [&](bool ok, const QString &name) { std::printf("%s DSP %s\n", ok ? "PASS" : "FAIL", name.toUtf8().constData()); std::fflush(stdout); if (!ok) ++failures; };
    auto source = AudioData::tone(.12, 220, 8000, 2);
    for (qint64 f = 0; f < source.frames(); ++f) source.samples[f * 2 + 1] = float(.2 * std::sin(2 * 3.141592653589793 * 330 * f / source.rate));
    for (const auto &op : Dsp::operations()) {
        auto result = Dsp::apply(source, 80, 800, op.id);
        bool finite = result.error.isEmpty() && !result.canceled;
        for (float sample : result.audio.samples) finite &= std::isfinite(sample) && std::abs(sample) <= 1;
        check(finite, op.id + " completes with bounded finite audio");
        if (!op.wholeDocument && finite) {
            bool outside = result.audio.samples.first(160) == source.samples.first(160);
            qint64 replacedEnd = op.id == "insert_silence" ? 80 : 800;
            outside &= result.audio.samples.mid(result.last * 2) == source.samples.mid(replacedEnd * 2);
            check(outside, op.id + " preserves audio outside the selection");
        }
    }
    auto dc = source; dc.samples.fill(.2f);
    auto removed = Dsp::apply(dc, 0, dc.frames(), "dc"); double peak = 0; for (float f : removed.audio.samples) peak = qMax(peak, double(std::abs(f)));
    check(peak < 1e-6, "DC removal cancels constant offset");
    auto inverted = Dsp::apply(dc, 0, 10, "invert"); check(inverted.audio.samples[0] == -.2f && inverted.audio.samples[20] == .2f, "polarity inversion and selection boundary");
    auto mono = Dsp::apply(source, 0, 0, "channels", {{"mode", 1}});
    bool left = mono.audio.channels == 1; for (qint64 f = 0; f < source.frames() && left; ++f) left &= mono.audio.samples[f] == source.samples[f * 2]; check(left, "left-channel conversion");
    auto stereo = Dsp::apply(mono.audio, 0, 0, "channels", {{"mode", 3}}); check(stereo.audio.channels == 2 && stereo.audio.samples[40] == stereo.audio.samples[41], "mono duplication to stereo");
    auto swapped = Dsp::apply(source, 0, source.frames(), "swap"); check(swapped.audio.samples[20] == source.samples[21] && swapped.audio.samples[21] == source.samples[20], "stereo channel swap");
    auto insert = Dsp::apply(source, 100, 200, "insert_silence", {{"seconds", .01}}); check(insert.audio.frames() == source.frames() + 80 && insert.first == 100 && insert.last == 180 && insert.audio.samples[200] == 0, "silence inserts without replacing selection");
    AudioData padded; padded.rate = 8000; padded.channels = 1; padded.samples.fill(0, 1000); for (int i = 200; i < 800; ++i) padded.samples[i] = .5;
    auto trim = Dsp::apply(padded, 0, 1000, "auto_trim", {{"padding", 0}}); check(trim.audio.frames() == 600 && trim.audio.samples.first() == .5f, "auto trim removes both silent margins");
    auto gain = Dsp::apply(dc, 0, dc.frames(), "volume", {{"gain", 6}}); check(std::abs(gain.audio.samples[100] - .399052) < 1e-5, "volume gain in dB");
    auto normalized = Dsp::apply(dc, 0, dc.frames(), "normalize", {{"target", -6}}); check(std::abs(normalized.audio.samples[0] - .501187) < 1e-5, "normalize target peak");
    auto sinc = Dsp::apply(source, 0, 0, "resample", {{"rate", 16000}}); check(sinc.audio.rate == 16000 && sinc.audio.frames() == source.frames() * 2 && std::abs(sinc.audio.duration() - source.duration()) < 1e-6, "resample retains duration and doubles frame count");
    auto aliasSource = AudioData::tone(.1, 6000, 48000, 1);
    auto alias = Dsp::apply(aliasSource, 0, 0, "resample", {{"rate", 8000}}); double energy = 0; for (qint64 f = 40; f < alias.audio.frames() - 40; ++f) energy += alias.audio.samples[f] * alias.audio.samples[f]; check(energy / (alias.audio.frames() - 80) < .001, "downsampling rejects frequencies above target Nyquist");
    AudioData impulse; impulse.rate = 8000; impulse.channels = 1; impulse.samples.fill(0, 800); impulse.samples[0] = 1;
    auto delay = Dsp::apply(impulse, 0, 800, "delay", {{"mix", 100}, {"feedback", 0}, {"delay", 10}, {"tail", 0}}); check(delay.audio.samples[0] == 0 && std::abs(delay.audio.samples[80] - 1) < 1e-6, "delay places impulse at requested delay");
    auto reverb = Dsp::apply(impulse, 0, 800, "reverb", {{"tail", .3}}); double tail = 0; for (qint64 i = 800; i < reverb.audio.frames(); ++i) tail += std::abs(reverb.audio.samples[i]); check(reverb.audio.frames() == 3200 && tail > .01, "reverb generates a decaying tail");
    auto limited = Dsp::apply(dc, 0, dc.frames(), "limiter", {{"threshold", -20}}); peak = 0; for (float value : limited.audio.samples) peak = qMax(peak, double(std::abs(value))); check(peak <= .100001, "limiter enforces ceiling");
    auto loud = dc; loud.samples.fill(.8f); auto compressed = Dsp::apply(loud, 0, loud.frames(), "compressor", {{"threshold", -20}, {"ratio", 4}}); check(std::abs(compressed.audio.samples.last()) < .2, "compressor reduces above-threshold level");
    auto quiet = dc; quiet.samples.fill(.05f); auto expanded = Dsp::apply(quiet, 0, quiet.frames(), "expander", {{"threshold", -20}, {"ratio", 4}}); check(std::abs(expanded.audio.samples.last()) < .01, "expander reduces below-threshold level");
    auto stereoImpulse = impulse; stereoImpulse.channels = 2; stereoImpulse.samples.fill(0, 1600); stereoImpulse.samples[0] = 1;
    auto pingpong = Dsp::apply(stereoImpulse, 0, 800, "pingpong", {{"delay", 10}, {"feedback", 50}, {"mix", 100}, {"tail", 0}}); check(std::abs(pingpong.audio.samples[80 * 2] - 1) < 1e-6 && std::abs(pingpong.audio.samples[160 * 2 + 1] - .5) < 1e-6, "ping-pong echoes alternate stereo channels");
    auto snipped = Dsp::apply(impulse, 0, 800, "gapper", {{"mode", 1}, {"period", 10}, {"gap", 50}}); check(snipped.audio.frames() == 400, "snipper removes periodic gaps");
    auto flat = Dsp::apply(dc, 0, dc.frames(), "eq.parametric", {{"gain", 0}}); check(flat.audio.samples == dc.samples, "zero-gain parametric EQ is transparent");
    auto weak = dc; weak.samples.fill(.001f); auto gated = Dsp::apply(weak, 0, weak.frames(), "gate"); check(std::abs(gated.audio.samples.last()) < .000001, "gate attenuates quiet input");
    auto sine = AudioData::tone(1, 220, 8000, 1);
    auto frequency = [](const AudioData &audio) { int crossings = 0; qint64 a = audio.frames() / 4, b = audio.frames() * 3 / 4; for (qint64 f = a + 1; f < b; ++f) if (audio.samples[f - 1] <= 0 && audio.samples[f] > 0) ++crossings; return crossings * double(audio.rate) / (b - a); };
    auto stretched = Dsp::apply(sine, 0, sine.frames(), "time", {{"duration", 150}}); check(stretched.audio.frames() == 12000 && std::abs(frequency(stretched.audio) - 220) < 8, "time expansion preserves pitch");
    auto pitch = Dsp::apply(sine, 0, sine.frames(), "pitch", {{"semitones", 12}}); check(pitch.audio.frames() == 8000 && std::abs(frequency(pitch.audio) - 440) < 15, "octave shift preserves duration and doubles frequency");
    std::atomic_bool cancel{true}; auto stopped = Dsp::apply(source, 0, 800, "chorus", {}, &cancel); check(stopped.canceled, "cancellation returns no committed result");
    check(!Dsp::apply(source, 0, 100, "volume", {{"gain", std::numeric_limits<double>::quiet_NaN()}}).error.isEmpty(), "reject NaN parameters");
    check(!Dsp::apply(source, 0, 100, "unknown").error.isEmpty(), "reject unknown operation");
    auto quantized = Dsp::apply(sine, 0, sine.frames(), "bit8", {{"dither", 0}}); bool grid = quantized.audio.bitDepth == 8; for (float f : quantized.audio.samples) grid &= std::abs(f * 128 - std::round(f * 128)) < 1e-6; check(grid, "8-bit quantization uses 256 amplitude levels");
    QTemporaryDir temp(QCoreApplication::applicationDirPath() + "/../build/dsp-test-XXXXXX"); QString error; auto eight = quantized.audio; eight.samples.resize(3); auto path = temp.filePath("8-bit.wav");
    bool written = eight.write(path, error); AudioData roundTrip; QFile file(path); file.open(QIODevice::ReadOnly); auto bytes = file.readAll();
    check(written && bytes.size() == 48 && quint8(bytes[34]) == 8 && AudioData::read(path, roundTrip, error) && roundTrip.bitDepth == 8 && roundTrip.samples == eight.samples, "8-bit WAV header, odd data padding and sample round trip");
    return failures;
}
