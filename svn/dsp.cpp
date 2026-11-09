#include "dsp.h"
#include "normalization.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr qint64 maxSamples = 512 * 1024 * 1024 / sizeof(float);
double db(double value) { return std::pow(10.0, value / 20.0); }
bool canceled(const std::atomic_bool *flag) { return flag && flag->load(); }
Dsp::Parameter number(QString key, QString label, double value, double lo, double hi, int decimals = 1) { return {key, label, value, lo, hi, decimals, {}}; }
Dsp::Parameter choice(QString key, QString label, QStringList choices, int selected = 0) { return {key, label, double(selected), 0, double(choices.size() - 1), 0, choices}; }
float sample(const QVector<float> &data, int channels, double position, int channel) {
    if (data.isEmpty()) return 0;
    position = std::clamp(position, 0.0, double(data.size() / channels - 1));
    qint64 a = qint64(position), b = qMin(a + 1, data.size() / channels - 1);
    double t = position - a;
    return float(data[a * channels + channel] * (1 - t) + data[b * channels + channel] * t);
}

QVector<float> resample(const QVector<float> &input, int channels, qint64 frames, const std::atomic_bool *cancel) {
    const qint64 n = input.size() / channels;
    if (!n || frames <= 0 || frames > maxSamples / channels) return {};
    if (frames == n) return input;
    QVector<float> output(frames * channels);
    double step = double(n) / frames, cutoff = qMin(1.0, 1.0 / step);
    const float *src = input.constData(); float *dst = output.data();
    // Windowed-sinc interpolation with anti-alias filtering when downsampling.
    int radius = int(std::ceil(16 / cutoff));
    for (qint64 f = 0; f < frames; ++f) {
        if ((f & 1023) == 0 && canceled(cancel)) return {};
        double pos = f * step, total[2] = {0, 0}, weight = 0;
        qint64 center = qint64(pos);
        for (int k = -radius + 1; k <= radius; ++k) {
            double x = center + k - pos, z = pi * x * cutoff;
            double w = (std::abs(z) < 1e-12 ? 1 : std::sin(z) / z) * (0.5 + 0.5 * std::cos(pi * x / radius));
            qint64 index = std::clamp(center + k, qint64(0), n - 1);
            weight += w;
            for (int c = 0; c < channels; ++c) total[c] += src[index * channels + c] * w;
        }
        for (int c = 0; c < channels; ++c) dst[f * channels + c] = float(total[c] / weight);
    }
    return output;
}

// Waveform-similarity overlap/add: a shared alignment for both stereo channels
// preserves their relationship while changing duration independently of pitch.
QVector<float> stretch(const QVector<float> &input, int channels, int rate, qint64 frames, const std::atomic_bool *cancel) {
    qint64 n = input.size() / channels;
    if (!n || frames <= 0 || frames > maxSamples / channels - 8192) return {};
    if (n == frames) return input;
    if (n < 64) return resample(input, channels, frames, cancel);
    int window = int(qMin(qint64(rate * .04), n)); window -= window % 2;
    int hop = window / 2, seek = qMin(rate / 200, window / 4);
    QVector<float> output((frames + window) * channels, 0), weights(frames + window, 0);
    const float *src = input.constData(); float *dst = output.data(); float *ws = weights.data();
    qint64 previous = 0;
    for (qint64 out = 0; out < frames; out += hop) {
        if (canceled(cancel)) return {};
        qint64 expected = std::clamp(qint64(std::llround(double(out) * n / frames)), qint64(0), n - window), best = expected;
        if (out) {
            double bestScore = -std::numeric_limits<double>::infinity();
            qint64 lo = qMax(qint64(0), expected - seek), hi = qMin(n - window, expected + seek);
            auto score = [&](qint64 position) {
                double xy = 0, xx = 1e-12, yy = 1e-12;
                for (int i = 0; i < hop; i += 4) for (int c = 0; c < channels; ++c) {
                    double x = src[(previous + hop + i) * channels + c], y = src[(position + i) * channels + c];
                    xy += x * y; xx += x * x; yy += y * y;
                }
                return xy / std::sqrt(xx * yy);
            };
            for (qint64 pos = lo; pos <= hi; pos += 8) { double s = score(pos); if (s > bestScore) { bestScore = s; best = pos; } }
            qint64 coarse = best;
            for (qint64 pos = qMax(lo, coarse - 7); pos <= qMin(hi, coarse + 7); ++pos) { double s = score(pos); if (s > bestScore) { bestScore = s; best = pos; } }
        }
        for (int i = 0; i < window; ++i) {
            float w = float(.5 - .5 * std::cos(2 * pi * (i + .5) / window));
            ws[out + i] += w;
            for (int c = 0; c < channels; ++c) dst[(out + i) * channels + c] += src[(best + i) * channels + c] * w;
        }
        previous = best;
    }
    for (qint64 f = 0; f < frames; ++f) for (int c = 0; c < channels; ++c) dst[f * channels + c] /= qMax(ws[f], 1e-12f);
    output.resize(frames * channels); return output;
}

void filter(QVector<float> &samples, int channels, int rate, int type, double frequency, double gain, double q, const std::atomic_bool *cancel) {
    // RBJ biquad coefficient equations, Audio EQ Cookbook (W3C).
    frequency = std::clamp(frequency, 5.0, rate * .475);
    double w = 2 * pi * frequency / rate, cs = std::cos(w), sn = std::sin(w), alpha = sn / (2 * q), A = std::pow(10, gain / 40);
    double b0, b1, b2, a0, a1, a2;
    if (type == 1) { b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = b0; a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha; }
    else if (type == 2) { b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = b0; a0 = 1 + alpha; a1 = -2 * cs; a2 = 1 - alpha; }
    else if (type == 3 || type == 4) {
        double s = 2 * std::sqrt(A) * alpha;
        if (type == 3) { b0 = A * ((A + 1) - (A - 1) * cs + s); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - s); a0 = (A + 1) + (A - 1) * cs + s; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - s; }
        else { b0 = A * ((A + 1) + (A - 1) * cs + s); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - s); a0 = (A + 1) - (A - 1) * cs + s; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - s; }
    } else { b0 = 1 + alpha * A; b1 = -2 * cs; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cs; a2 = 1 - alpha / A; }
    double z1[2] = {0, 0}, z2[2] = {0, 0}; float *data = samples.data();
    for (qint64 f = 0; f < samples.size() / channels; ++f) {
        if ((f & 4095) == 0 && canceled(cancel)) return;
        for (int c = 0; c < channels; ++c) {
            double x = data[f * channels + c], y = b0 / a0 * x + z1[c];
            z1[c] = b1 / a0 * x - a1 / a0 * y + z2[c]; z2[c] = b2 / a0 * x - a2 / a0 * y;
            data[f * channels + c] = float(y);
        }
    }
}
}

const QVector<Dsp::Operation> &Dsp::operations() {
    static const QVector<Operation> catalog = [] {
        QVector<Operation> v;
        auto add = [&](QString id, QString menu, QString title, QString description, QVector<Parameter> parameters = {}, bool whole = false, bool preview = true) { v.append({id, menu, title, description, parameters, whole, preview}); };
        add("auto_trim", "Proceso", "Auto Trim/Crop…", "Recorta el silencio inicial y final de la selección, conservando un margen.", {number("threshold", "Umbral (dBFS)", -45, -96, -6), number("padding", "Margen (ms)", 10, 0, 1000)});
        add("channels", "Proceso", "Channel Converter…", "Convierte los canales de todo el documento.", {choice("mode", "Conversión", {"Mono: mezclar L + R", "Mono: canal izquierdo", "Mono: canal derecho", "Estéreo: duplicar mono"})}, true);
        add("bit8", "Proceso", "Convert to 8-Bit…", "Cuantiza todo el documento y configura el guardado WAV PCM de 8 bits.", {choice("dither", "Dither TPDF", {"Desactivado", "Activado"}, 1)}, true);
        add("dc", "Proceso", "DC Offset…", "Elimina la componente continua por canal y permite añadir un desplazamiento.", {choice("remove", "Eliminar media", {"No", "Sí"}, 1), number("offset", "Desplazamiento (%)", 0, -100, 100)});
        QVector<Parameter> bands; for (int hz : {32,64,125,250,500,1000,2000,4000,8000,16000}) bands.append(number(QString::number(hz), QString::number(hz) + " Hz (dB)", 0, -24, 24));
        add("eq.graphic", "Proceso/EQ", "Graphic EQ…", "Ecualizador gráfico de diez bandas. Omite las bandas por encima de Nyquist.", bands);
        add("eq.parametric", "Proceso/EQ", "Parametric EQ…", "Filtro de segundo orden con frecuencia, ganancia y Q ajustables.", {choice("type", "Filtro", {"Campana", "Paso bajo", "Paso alto", "Estantería baja", "Estantería alta"}), number("frequency", "Frecuencia (Hz)", 1000, 20, 20000), number("gain", "Ganancia (dB)", 0, -24, 24), number("q", "Q", .707, .1, 10, 3)});
        add("fade.in", "Proceso/Fade", "In", "Fundido de entrada lineal.");
        add("fade.out", "Proceso/Fade", "Out", "Fundido de salida lineal.");
        QVector<Parameter> envelope = {number("start", "Nivel inicial (dB)", -60, -96, 12), number("middle", "Nivel intermedio (dB)", 0, -96, 12), number("end", "Nivel final (dB)", -60, -96, 12), number("point", "Posición intermedia (%)", 50, 1, 99)};
        add("fade.envelope", "Proceso/Fade", "Graphic…", "Envolvente de tres puntos con interpolación lineal de amplitud.", envelope);
        add("insert_silence", "Proceso", "Insert Silence…", "Inserta silencio en el cursor o al inicio de la selección.", {number("seconds", "Duración (s)", 1, .001, 600, 3)}, false, false);
        add("invert", "Proceso/Invert/Flip", "Invert polarity", "Invierte la polaridad de las muestras seleccionadas.");
        add("swap", "Proceso/Invert/Flip", "Flip channels", "Intercambia los canales izquierdo y derecho de la selección estéreo.");
        add("mute", "Proceso", "Mute", "Silencia la selección.");
        add("normalize", "Proceso", "Normalize…", "Normalización por pico o RMS con análisis y control de clipping.", {
            number("target", "Nivel objetivo (dBFS)", -.1, -60, 0),
            choice("mode", "Normalizar usando", {"Pico", "Potencia RMS media"}),
            number("ignore", "Ignorar por debajo (dBFS)", -45, -96, 0),
            number("attack", "Ataque de análisis (ms)", 200, 1, 500, 0),
            number("release", "Relajación de análisis (ms)", 200, 1, 500, 0),
            choice("contour", "Contorno perceptual aproximado", {"No", "Sí"}, 1),
            choice("clipping", "Si hay clipping", {"Compresión dinámica (limitador)", "Reducir ganancia", "Saturar", "Cancelar"})});
        add("pan", "Proceso", "Pan/Expand…", "Balance y anchura estéreo mediante matriz mid/side. Requiere dos canales.", {number("pan", "Pan: izquierda −100 / derecha +100", 0, -100, 100), number("width", "Anchura estéreo (%)", 100, 0, 200)});
        add("resample", "Proceso", "Resample…", "Cambia la frecuencia de todo el documento con filtro antialias. Conserva la duración.", {number("rate", "Frecuencia de destino (Hz)", 48000, 8000, 192000, 0)}, true, false);
        add("reverse", "Proceso", "Reverse", "Invierte en el tiempo, conservando juntos los canales estéreo.");
        add("smooth", "Proceso", "Smooth/Enhance…", "Suaviza variaciones rápidas o realza el detalle de alta frecuencia.", {choice("mode", "Modo", {"Suavizar", "Realzar"}), number("amount", "Intensidad (%)", 50, 0, 100)});
        add("time", "Proceso", "Time Compress/Expand…", "Cambia la duración manteniendo el tono mediante alineamiento de ondas y solapamiento.", {number("duration", "Duración respecto al original (%)", 120, 50, 200)});
        add("volume", "Proceso", "Volume…", "Ajusta el volumen de la selección.", {number("gain", "Ganancia (dB)", 0, -60, 24)});
        add("am", "Efectos", "Amplitude Modulation…", "Modulación de amplitud sinusoidal (trémolo).", {number("rate", "Frecuencia (Hz)", 5, .1, 100), number("depth", "Profundidad (%)", 70, 0, 100)});
        add("chorus", "Efectos", "Chorus…", "Retardo modulado con desfase entre canales.", {number("rate", "Velocidad (Hz)", .8, .1, 10), number("delay", "Retardo base (ms)", 20, 1, 60), number("depth", "Profundidad (ms)", 5, 0, 15), number("mix", "Mezcla húmeda (%)", 35, 0, 100)});
        QVector<Parameter> delays = {number("delay", "Retardo (ms)", 250, 1, 2000), number("feedback", "Realimentación (%)", 35, 0, 90), number("mix", "Mezcla húmeda (%)", 35, 0, 100), number("tail", "Cola añadida (s)", 1, 0, 10, 2)};
        add("delay", "Efectos/Delay/Echo", "Simple Delay…", "Retardo con realimentación y cola opcional.", delays);
        add("pingpong", "Efectos/Delay/Echo", "Ping-Pong Echo…", "Eco alternado entre los canales. Requiere estéreo.", delays);
        add("distortion", "Efectos", "Distortion…", "Saturación suave o recorte duro con ganancia de entrada y salida.", {choice("mode", "Modo", {"Saturación suave", "Recorte duro"}), number("drive", "Ganancia de entrada (dB)", 12, 0, 36), number("output", "Ganancia de salida (dB)", -6, -36, 0), number("mix", "Mezcla húmeda (%)", 100, 0, 100)});
        QVector<Parameter> dynamics = {number("threshold", "Umbral (dBFS)", -18, -80, 0), number("ratio", "Relación", 4, 1, 20), number("attack", "Ataque (ms)", 5, .1, 200), number("release", "Relajación (ms)", 100, 1, 2000), number("makeup", "Ganancia de salida (dB)", 0, -24, 24)};
        add("compressor", "Efectos/Dynamics", "Compressor…", "Compresor con detector de pico enlazado para ambos canales.", dynamics);
        add("expander", "Efectos/Dynamics", "Expander…", "Expansor descendente por debajo del umbral.", dynamics);
        add("limiter", "Efectos/Dynamics", "Limiter…", "Limitador con techo duro en el umbral seleccionado.", dynamics);
        add("envelope", "Efectos", "Envelope…", "Envolvente de volumen de tres puntos.", envelope);
        add("flanger", "Efectos/Flange/Wah-Wah", "Flange…", "Retardo corto modulado con realimentación.", {number("rate", "Velocidad (Hz)", .3, .05, 10, 2), number("delay", "Retardo base (ms)", 2, .1, 10), number("depth", "Profundidad (ms)", 1.5, 0, 8), number("feedback", "Realimentación (%)", 40, -90, 90), number("mix", "Mezcla húmeda (%)", 50, 0, 100)});
        add("wah", "Efectos/Flange/Wah-Wah", "Wah-Wah…", "Filtro resonante con barrido sinusoidal de frecuencia.", {number("rate", "Velocidad (Hz)", 1, .1, 10), number("low", "Frecuencia mínima (Hz)", 300, 20, 3000), number("high", "Frecuencia máxima (Hz)", 2500, 100, 16000), number("q", "Resonancia Q", 2, .5, 8), number("mix", "Mezcla húmeda (%)", 70, 0, 100)});
        add("gapper", "Efectos", "Gapper/Snipper…", "Introduce huecos periódicos o elimina esos fragmentos. Los bordes tienen fundido corto.", {choice("mode", "Modo", {"Gapper: silenciar huecos", "Snipper: eliminar huecos"}), number("period", "Periodo (ms)", 200, 10, 2000), number("gap", "Hueco por periodo (%)", 40, 1, 95)});
        add("gate", "Efectos", "Noise Gate…", "Puerta con ataque y relajación. No elimina ruido que coincide con una señal fuerte.", {number("threshold", "Umbral (dBFS)", -40, -96, -3), number("attack", "Ataque (ms)", 2, .1, 100), number("release", "Relajación (ms)", 80, 1, 1000), number("floor", "Nivel cerrado (dB)", -80, -96, -6)});
        add("pitch", "Efectos/Pitch", "Shift…", "Cambia el tono conservando la duración, mediante remuestreo y solapamiento.", {number("semitones", "Semitonos", 3, -12, 12, 2)});
        add("pitch.bend", "Efectos/Pitch", "Bend…", "Deslizamiento de tono con dos ventanas granulares superpuestas. Conserva la duración.", {number("start", "Semitonos iniciales", 0, -12, 12), number("end", "Semitonos finales", 5, -12, 12)});
        add("reverb", "Efectos", "Reverb…", "Reverberación algorítmica con filtros peine y difusores, con cola opcional.", {number("decay", "Decaimiento RT60 (s)", 1.5, .1, 8, 2), number("predelay", "Pre-retardo (ms)", 20, 0, 200), number("damping", "Amortiguación (%)", 50, 0, 95), number("mix", "Mezcla húmeda (%)", 25, 0, 100), number("tail", "Cola añadida (s)", 1.5, 0, 10, 2)});
        add("vibrato", "Efectos", "Vibrato…", "Modula el tono con una lectura fraccional sinusoidal.", {number("rate", "Velocidad (Hz)", 5, .1, 20), number("depth", "Profundidad temporal (ms)", 2, 0, 5)});
        return v;
    }();
    return catalog;
}

Dsp::Values Dsp::defaults(const QString &id) {
    Values values; for (const auto &op : operations()) if (op.id == id) for (const auto &p : op.parameters) values[p.key] = p.value;
    return values;
}

Dsp::Result Dsp::apply(const AudioData &source, qint64 first, qint64 last, const QString &id, const Values &parameters, const std::atomic_bool *cancel) {
    Result result;
    auto fail = [&](QString message) { result.error = message; result.canceled = canceled(cancel); return result; };
    const Operation *operation = nullptr; for (const auto &op : operations()) if (op.id == id) { operation = &op; break; }
    if (!operation) return fail("Proceso desconocido: " + id);
    if (source.channels < 1 || source.channels > 2 || source.rate < 8000 || source.rate > 384000 || source.samples.size() % source.channels) return fail("Formato de audio no válido.");
    Values values = defaults(id);
    for (const auto &p : operation->parameters) {
        double value = parameters.value(p.key, p.value);
        if (!std::isfinite(value) || value < p.minimum || value > p.maximum || (!p.choices.isEmpty() && std::floor(value) != value)) return fail("Parámetro fuera de rango: " + p.label);
        values[p.key] = value;
    }
    auto v = [&](const char *key) { return values.value(QString::fromLatin1(key)); };
    first = std::clamp(first, qint64(0), source.frames()); last = std::clamp(last, first, source.frames());
    if (operation->wholeDocument) { first = 0; last = source.frames(); }
    if (first == last && id != "insert_silence") { first = 0; last = source.frames(); }
    if (first == last && id != "insert_silence") return fail("El documento no contiene audio.");
    qint64 n = last - first; int channels = source.channels, rate = source.rate;
    QVector<float> input = source.samples.mid(first * channels, n * channels), output = input;
    result.audio = source; result.first = first;
    auto allocate = [&](qint64 frames) { return frames >= 0 && frames <= (maxSamples - (source.frames() - n) * channels) / channels; };
    if (id == "normalize") {
        const QString error = normalizeSamples(output, channels, rate, values, parameters, cancel);
        if (!error.isEmpty()) return fail(error);
    } else if (id == "insert_silence") {
        qint64 count = qMax(qint64(1), qint64(std::llround(v("seconds") * rate)));
        if (!allocate(count + n)) return fail("El resultado supera el límite de 512 MB.");
        output = QVector<float>(count * channels, 0); last = first;
    } else if (id == "channels") {
        int mode = int(v("mode")), target = mode == 3 ? 2 : 1;
        if (source.frames() > maxSamples / target) return fail("El resultado supera el límite de 512 MB.");
        output.resize(n * target);
        for (qint64 f = 0; f < n; ++f) {
            if ((f & 4095) == 0 && canceled(cancel)) return fail({});
            float left = input[f * channels], right = input[f * channels + channels - 1];
            if (target == 2) { output[f * 2] = left; output[f * 2 + 1] = right; }
            else output[f] = mode == 1 ? left : mode == 2 ? right : (left + right) * .5f;
        }
        channels = target; result.audio.channels = target;
    } else if (id == "resample") {
        qint64 count = qMax(qint64(1), qint64(std::llround(double(n) * v("rate") / rate)));
        if (!allocate(count)) return fail("El resultado supera el límite de 512 MB.");
        output = resample(input, channels, count, cancel); result.audio.rate = int(v("rate"));
    } else if (id == "time" || id == "pitch") {
        double factor = id == "time" ? v("duration") / 100 : std::pow(2, v("semitones") / 12);
        qint64 count = id == "time" ? qMax(qint64(1), qint64(std::llround(n * factor))) : n;
        if (!allocate(count) || !allocate(qint64(n / factor))) return fail("El resultado supera el límite de 512 MB.");
        if (id == "pitch") output = resample(input, channels, qMax(qint64(1), qint64(std::llround(n / factor))), cancel);
        output = stretch(output, channels, rate, count, cancel);
    } else if (id == "auto_trim") {
        qint64 a = 0, b = n; double threshold = db(v("threshold"));
        auto audible = [&](qint64 f) { for (int c = 0; c < channels; ++c) if (std::abs(input[f * channels + c]) > threshold) return true; return false; };
        while (a < n && !audible(a)) { if ((a & 4095) == 0 && canceled(cancel)) return fail({}); ++a; }
        while (b > a && !audible(b - 1)) { if ((b & 4095) == 0 && canceled(cancel)) return fail({}); --b; }
        qint64 padding = qint64(v("padding") * rate / 1000);
        if (a == n) output.clear();
        else { a = qMax(qint64(0), a - padding); b = qMin(n, b + padding); output = input.mid(a * channels, (b - a) * channels); }
    } else if (id == "eq.parametric" || id == "eq.graphic") {
        if (id == "eq.parametric") filter(output, channels, rate, int(v("type")), v("frequency"), v("gain"), v("q"), cancel);
        else for (int hz : {32,64,125,250,500,1000,2000,4000,8000,16000}) if (hz < rate * .475 && values.value(QString::number(hz)) != 0) filter(output, channels, rate, 0, hz, values.value(QString::number(hz)), 1.4, cancel);
    } else if (id == "delay" || id == "pingpong" || id == "chorus" || id == "flanger") {
        if (id == "pingpong" && channels != 2) return fail("Ping-Pong Echo requiere audio estéreo.");
        qint64 count = n + qint64(v("tail") * rate); if (!allocate(count)) return fail("El resultado supera el límite de 512 MB.");
        output.fill(0, count * channels);
        double base = v("delay") * rate / 1000, depth = v("depth") * rate / 1000;
        int length = int(std::ceil(base + depth)) + 4; QVector<float> line(length * channels, 0);
        double wet = v("mix") / 100, feedback = v("feedback") / 100;
        float *dst = output.data(), *ring = line.data();
        for (qint64 f = 0; f < count; ++f) {
            if ((f & 4095) == 0 && canceled(cancel)) return fail({});
            float taps[2] = {0, 0};
            for (int c = 0; c < channels; ++c) {
                double delay = std::max(1.0, base + depth * std::sin(2 * pi * v("rate") * f / rate + c * pi / 2));
                double pos = std::fmod(double(f % length) - delay + length * 2, length); int a = int(pos), b = (a + 1) % length;
                taps[c] = float(ring[a * channels + c] * (1 - (pos - a)) + ring[b * channels + c] * (pos - a));
            }
            for (int c = 0; c < channels; ++c) {
                float dry = f < n ? input[f * channels + c] : 0;
                ring[(f % length) * channels + c] = dry + float(feedback * taps[id == "pingpong" ? 1 - c : c]);
                dst[f * channels + c] = float(dry * (1 - wet) + taps[c] * wet);
            }
        }
    } else if (id == "reverb") {
        qint64 count = n + qint64(v("tail") * rate); if (!allocate(count)) return fail("El resultado supera el límite de 512 MB.");
        output.fill(0, count * channels); float *dst = output.data();
        for (int c = 0; c < channels; ++c) {
            QVector<float> combs[4], allpasses[2]; double feedback[4], damped[4] = {0,0,0,0};
            double times[] = {.0297,.0371,.0411,.0437};
            for (int k = 0; k < 4; ++k) { combs[k].fill(0, qMax(1, int(rate * (times[k] + c * .0007)))); feedback[k] = std::pow(.001, double(combs[k].size()) / (rate * v("decay"))); }
            allpasses[0].fill(0, qMax(1, int(rate * .005))); allpasses[1].fill(0, qMax(1, int(rate * .0017)));
            qint64 predelay = qint64(v("predelay") * rate / 1000); double wet = v("mix") / 100, damping = v("damping") / 100;
            for (qint64 f = 0; f < count; ++f) {
                if ((f & 4095) == 0 && canceled(cancel)) return fail({});
                double dry = f < n ? input[f * channels + c] : 0, excitation = f >= predelay && f - predelay < n ? input[(f - predelay) * channels + c] : 0, sum = 0;
                for (int k = 0; k < 4; ++k) { int index = int(f % combs[k].size()); double y = combs[k][index]; damped[k] = y * (1 - damping) + damped[k] * damping; combs[k][index] = float(excitation + damped[k] * feedback[k]); sum += y * .25; }
                for (auto &line : allpasses) { int index = int(f % line.size()); double y = line[index] - .5 * sum; line[index] = float(sum + .5 * y); sum = y; }
                dst[f * channels + c] = float(dry * (1 - wet) + sum * wet);
            }
        }
    } else {
        if ((id == "swap" || id == "pan") && channels != 2) return fail("Este proceso requiere audio estéreo. Usa Channel Converter primero.");
        double means[2] = {0,0};
        if (id == "dc" && v("remove")) {
            for (qint64 f = 0; f < n; ++f) { if ((f & 4095) == 0 && canceled(cancel)) return fail({}); for (int c = 0; c < channels; ++c) means[c] += input[f * channels + c] / double(n); }
        }
        float *dst = output.data(); const float *src = input.constData();
        double envelope = 0, gateGain = 0, ic1[2] = {0,0}, ic2[2] = {0,0}; quint32 random = 0x31415926;
        qint64 writeFrame = 0;
        for (qint64 f = 0; f < n; ++f) {
            if ((f & 4095) == 0 && canceled(cancel)) return fail({});
            double t = n > 1 ? double(f) / (n - 1) : 1, gain = 1;
            if (id == "volume") gain = db(v("gain"));
            if (id == "mute") gain = 0;
            if (id == "invert") gain = -1;
            if (id == "fade.in") gain = t;
            if (id == "fade.out") gain = 1 - t;
            if (id == "fade.envelope" || id == "envelope") { double point = v("point") / 100; gain = t < point ? db(v("start")) + (db(v("middle")) - db(v("start"))) * t / point : db(v("middle")) + (db(v("end")) - db(v("middle"))) * (t - point) / (1 - point); }
            if (id == "am") gain = 1 - v("depth") / 100 * (.5 - .5 * std::sin(2 * pi * v("rate") * f / rate));
            if (id == "compressor" || id == "expander" || id == "limiter" || id == "gate") {
                double detector = 0; for (int c = 0; c < channels; ++c) detector = qMax(detector, double(std::abs(src[f * channels + c])));
                double attack = std::exp(-1 / (rate * v("attack") / 1000)), release = std::exp(-1 / (rate * v("release") / 1000));
                double coefficient = detector > envelope ? attack : release; envelope = coefficient * envelope + (1 - coefficient) * detector;
                if (id == "gate") { double target = envelope >= db(v("threshold")) ? 1 : db(v("floor")); coefficient = target > gateGain ? attack : release; gateGain = coefficient * gateGain + (1 - coefficient) * target; gain = gateGain; }
                else { double level = 20 * std::log10(qMax(envelope, 1e-12)), difference = level - v("threshold"), reduction = 0;
                    if (id == "expander" && difference < 0) reduction = difference * (v("ratio") - 1);
                    if (id != "expander" && difference > 0) reduction = -difference * (1 - 1 / (id == "limiter" ? 1000 : v("ratio")));
                    gain = db(reduction + v("makeup"));
                }
            }
            bool keep = true;
            if (id == "gapper") {
                qint64 period = qMax(qint64(1), qint64(v("period") * rate / 1000)), kept = qMax(qint64(1), qint64(period * (1 - v("gap") / 100))), pos = f % period;
                keep = pos < kept;
                double edge = qMax(1.0, qMin(rate * .002, kept / 4.0));
                gain = keep ? std::clamp(qMin(double(pos) / edge, double(kept - 1 - pos) / edge), 0.0, 1.0) : 0;
                if (!keep && v("mode") == 1) continue;
            }
            for (int c = 0; c < channels; ++c) {
                double x = src[f * channels + c], y = x * gain;
                if (id == "reverse") y = src[(n - 1 - f) * channels + c];
                if (id == "swap") y = src[f * channels + 1 - c];
                if (id == "dc") y = x - means[c] + v("offset") / 100;
                if (id == "bit8") { double noise = 0; if (v("dither")) { random = random * 1664525u + 1013904223u; double a = double(random) / 4294967296.; random = random * 1664525u + 1013904223u; noise = (a - double(random) / 4294967296.) / 128; } y = std::clamp(std::round((x + noise) * 128), -128.0, 127.0) / 128; }
                if (id == "pan") { double mid = (src[f * 2] + src[f * 2 + 1]) * .5, side = (src[f * 2] - src[f * 2 + 1]) * .5 * v("width") / 100, pan = v("pan") / 100; y = (mid + (c ? -side : side)) * (c ? qMin(1.0, 1 + pan) : qMin(1.0, 1 - pan)); }
                if (id == "smooth") { double smoothed = (.25 * sample(input, channels, f - 1, c) + .5 * x + .25 * sample(input, channels, f + 1, c)), amount = v("amount") / 100; y = v("mode") ? x + amount * (x - smoothed) : x + amount * (smoothed - x); }
                if (id == "distortion") { double driven = x * db(v("drive")); double distorted = v("mode") ? std::clamp(driven, -1.0, 1.0) : std::tanh(driven); double mix = v("mix") / 100; y = x * (1 - mix) + distorted * db(v("output")) * mix; }
                if (id == "vibrato") y = sample(input, channels, f + v("depth") * rate / 1000 * std::sin(2 * pi * v("rate") * f / rate), c);
                if (id == "pitch.bend") { double factor = std::pow(2, (v("start") + t * (v("end") - v("start"))) / 12), grain = rate * .05, phase = std::fmod(f / grain, 1.0), phase2 = std::fmod(phase + .5, 1.0), w = .5 - .5 * std::cos(2 * pi * phase); y = w * sample(input, channels, f + (factor - 1) * (phase - .5) * grain, c) + (1 - w) * sample(input, channels, f + (factor - 1) * (phase2 - .5) * grain, c); }
                if (id == "wah") { double low = qMin(v("low"), v("high")), high = qMax(v("low"), v("high")), frequency = low * std::pow(high / low, .5 + .5 * std::sin(2 * pi * v("rate") * f / rate)); frequency = qMin(frequency, rate * .4); double g = std::tan(pi * frequency / rate), k = 1 / v("q"), a1 = 1 / (1 + g * (g + k)), v1 = a1 * (ic1[c] + g * (x - ic2[c])), v2 = ic2[c] + g * v1; ic1[c] = 2 * v1 - ic1[c]; ic2[c] = 2 * v2 - ic2[c]; double mix = v("mix") / 100; y = x * (1 - mix) + v1 * k * mix; }
                if (id == "limiter") y = std::clamp(y, -db(v("threshold")), db(v("threshold")));
                dst[writeFrame * channels + c] = float(y);
            }
            ++writeFrame;
        }
        if (id == "gapper") output.resize(writeFrame * channels);
        if (id == "bit8") { result.audio.bitDepth = 8; result.audio.floatFormat = false; }
    }
    if (canceled(cancel)) { result.canceled = true; return result; }
    if (output.isEmpty() && n && (id == "time" || id == "pitch" || id == "resample")) return fail("No se pudo generar el audio procesado.");
    if (output.size() + (source.frames() - (last - first)) * source.channels > maxSamples) return fail("El resultado supera el límite de 512 MB.");
    for (float &value : output) value = std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0;
    result.audio.samples = operation->wholeDocument ? output : source.samples.first(first * source.channels) + output + source.samples.mid(last * source.channels);
    result.last = first + output.size() / channels;
    return result;
}
