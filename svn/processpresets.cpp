#include "processpresets.h"
#include <QSettings>
#include <QVariantMap>
#include <cmath>

namespace {
QString group(const QString &id) { return "processPresets/" + id; }
QString key(const QString &id, const QString &name) { return group(id) + "/" + QString::fromLatin1(name.toUtf8().toHex()); }
bool valid(const QString &id, const Dsp::Values &input, Dsp::Values &output) {
    for (const auto &op : Dsp::operations()) if (op.id == id && !op.parameters.isEmpty()) {
        output = Dsp::defaults(id);
        for (const auto &p : op.parameters) {
            const double v = input.value(p.key, p.value);
            if (!std::isfinite(v) || v < p.minimum || v > p.maximum || (!p.choices.isEmpty() && std::floor(v) != v)) return false;
            output[p.key] = v;
        }
        return true;
    }
    return false;
}
}

QVector<ProcessPresets::Preset> ProcessPresets::factory(const QString &id) {
    QVector<Preset> presets;
    const auto base = Dsp::defaults(id);
    if (base.isEmpty()) return presets;
    auto add = [&](const QString &name, const Dsp::Values &overrides = {}) {
        auto v = base; for (auto i = overrides.cbegin(); i != overrides.cend(); ++i) v[i.key()] = i.value();
        presets.append({name, v, false});
    };
    add("Predeterminado");
    if (id == "auto_trim") {
        add("Voz: conservar respiraciones", {{"threshold", -50}, {"padding", 100}});
        add("Recorte preciso", {{"threshold", -60}, {"padding", 5}});
        add("Recortar ruido de fondo", {{"threshold", -35}, {"padding", 80}});
    } else if (id == "channels") {
        add("Mono: mezclar L + R", {{"mode", 0}}); add("Mono: izquierdo", {{"mode", 1}});
        add("Mono: derecho", {{"mode", 2}}); add("Estéreo: duplicar mono", {{"mode", 3}});
    } else if (id == "bit8") {
        add("8 bits con dither", {{"dither", 1}}); add("8 bits sin dither", {{"dither", 0}});
    } else if (id == "dc") {
        add("Eliminar componente continua", {{"remove", 1}, {"offset", 0}});
        add("Desplazamiento +1 %", {{"remove", 0}, {"offset", 1}});
        add("Desplazamiento −1 %", {{"remove", 0}, {"offset", -1}});
    } else if (id == "eq.graphic") {
        add("Voz clara", {{"32", -6}, {"64", -4}, {"125", -2}, {"250", -2}, {"1000", 1}, {"2000", 3}, {"4000", 2}});
        add("Refuerzo de graves", {{"32", 4}, {"64", 4}, {"125", 2}});
        add("Brillo", {{"4000", 2}, {"8000", 4}, {"16000", 3}});
        add("Cálido", {{"125", 2}, {"250", 1}, {"4000", -2}, {"8000", -2}, {"16000", -3}});
    } else if (id == "eq.parametric") {
        add("Voz: paso alto a 80 Hz", {{"type", 2}, {"frequency", 80}});
        add("Reducir zumbido de 50 Hz", {{"frequency", 50}, {"gain", -18}, {"q", 8}});
        add("Reducir zumbido de 60 Hz", {{"frequency", 60}, {"gain", -18}, {"q", 8}});
        add("Presencia de voz", {{"frequency", 3000}, {"gain", 3}, {"q", 1}});
        add("Aire", {{"type", 4}, {"frequency", 8000}, {"gain", 3}});
    } else if (id == "fade.envelope" || id == "envelope") {
        add("Entrada progresiva", {{"start", -96}, {"middle", -6}, {"end", 0}});
        add("Salida progresiva", {{"start", 0}, {"middle", -6}, {"end", -96}});
        add("Bajar el centro", {{"start", 0}, {"middle", -12}, {"end", 0}});
    } else if (id == "insert_silence") {
        add("Pausa de 250 ms", {{"seconds", .25}}); add("Pausa de 500 ms", {{"seconds", .5}});
        add("Pausa de 2 segundos", {{"seconds", 2}}); add("Pausa de 5 segundos", {{"seconds", 5}});
    } else if (id == "normalize") {
        add("Maximizar pico", {{"target", 0}}); add("RMS: voz −10 dB", {{"mode", 1}, {"target", -10}});
        add("RMS: música −16 dB", {{"mode", 1}, {"target", -16}});
    } else if (id == "pan") {
        add("Centrado", {{"pan", 0}, {"width", 100}}); add("Mono", {{"pan", 0}, {"width", 0}});
        add("Estéreo amplio", {{"pan", 0}, {"width", 150}}); add("Izquierda", {{"pan", -100}}); add("Derecha", {{"pan", 100}});
    } else if (id == "resample") {
        add("CD: 44.100 Hz", {{"rate", 44100}}); add("Vídeo: 48.000 Hz", {{"rate", 48000}});
        add("Alta resolución: 96.000 Hz", {{"rate", 96000}}); add("22.050 Hz", {{"rate", 22050}});
    } else if (id == "smooth") {
        add("Suavizado leve", {{"mode", 0}, {"amount", 20}}); add("Suavizado medio", {{"mode", 0}, {"amount", 50}});
        add("Realce sutil", {{"mode", 1}, {"amount", 15}});
    } else if (id == "time") {
        add("Duración original", {{"duration", 100}}); add("Duración al 90 %", {{"duration", 90}});
        add("Duración al 110 %", {{"duration", 110}}); add("Duración al 125 %", {{"duration", 125}});
    } else if (id == "volume") {
        add("Unidad: 0 dB", {{"gain", 0}}); add("Reducir 3 dB", {{"gain", -3}}); add("Reducir 6 dB", {{"gain", -6}});
        add("Aumentar 3 dB", {{"gain", 3}}); add("Aumentar 6 dB", {{"gain", 6}});
    } else if (id == "am") {
        add("Trémolo suave", {{"rate", 4}, {"depth", 25}}); add("Trémolo rápido", {{"rate", 8}, {"depth", 60}});
        add("Modulación intensa", {{"rate", 40}, {"depth", 90}});
    } else if (id == "chorus") {
        add("Sutil", {{"rate", .6}, {"delay", 18}, {"depth", 2}, {"mix", 20}});
        add("Estéreo ancho", {{"rate", .8}, {"delay", 22}, {"depth", 6}, {"mix", 45}});
        add("Lento y profundo", {{"rate", .25}, {"delay", 25}, {"depth", 8}, {"mix", 40}});
    } else if (id == "delay") {
        add("Slapback", {{"delay", 90}, {"feedback", 10}, {"mix", 25}, {"tail", .5}});
        add("Eco corto", {{"delay", 180}, {"feedback", 25}, {"mix", 30}, {"tail", 1}});
        add("Voz amplia", {{"delay", 300}, {"feedback", 30}, {"mix", 20}, {"tail", 2}});
        add("Eco largo", {{"delay", 500}, {"feedback", 45}, {"mix", 35}, {"tail", 3}});
    } else if (id == "pingpong") {
        add("Ping-pong corto", {{"delay", 150}, {"feedback", 35}, {"mix", 35}, {"tail", 2}});
        add("Ping-pong rítmico", {{"delay", 300}, {"feedback", 45}, {"mix", 45}, {"tail", 3}});
        add("Ambiente largo", {{"delay", 750}, {"feedback", 60}, {"mix", 40}, {"tail", 6}});
    } else if (id == "distortion") {
        add("Calidez", {{"mode", 0}, {"drive", 3}, {"output", -3}, {"mix", 30}});
        add("Saturación suave", {{"mode", 0}, {"drive", 9}, {"output", -6}, {"mix", 60}});
        add("Recorte agresivo", {{"mode", 1}, {"drive", 24}, {"output", -18}, {"mix", 100}});
    } else if (id == "compressor") {
        add("Voz suave", {{"threshold", -18}, {"ratio", 2}, {"attack", 10}, {"release", 120}, {"makeup", 3}});
        add("Voz presente", {{"threshold", -20}, {"ratio", 3}, {"attack", 5}, {"release", 100}, {"makeup", 4}});
        add("Música suave", {{"threshold", -12}, {"ratio", 2}, {"attack", 25}, {"release", 180}, {"makeup", 2}});
        add("Percusión", {{"threshold", -16}, {"ratio", 4}, {"attack", 20}, {"release", 80}, {"makeup", 3}});
    } else if (id == "expander") {
        add("Limpiar pausas", {{"threshold", -40}, {"ratio", 2}, {"attack", 5}, {"release", 150}});
        add("Cierre firme", {{"threshold", -35}, {"ratio", 4}, {"attack", 3}, {"release", 120}});
        add("Expansión suave", {{"threshold", -50}, {"ratio", 1.5}, {"attack", 10}, {"release", 200}});
    } else if (id == "limiter") {
        add("Techo −1 dBFS", {{"threshold", -1}, {"makeup", 0}});
        add("Techo −0,3 dBFS", {{"threshold", -.3}, {"makeup", 0}}); add("Protección −3 dBFS", {{"threshold", -3}, {"makeup", 0}});
    } else if (id == "flanger") {
        add("Clásico", {{"rate", .3}, {"delay", 2}, {"depth", 1.5}, {"feedback", 40}, {"mix", 50}});
        add("Suave", {{"rate", .2}, {"delay", 3}, {"depth", 1}, {"feedback", 20}, {"mix", 25}});
        add("Barrido profundo", {{"rate", .12}, {"delay", 4}, {"depth", 4}, {"feedback", 60}, {"mix", 60}});
    } else if (id == "wah") {
        add("Auto-wah clásico", {{"rate", 1}, {"low", 300}, {"high", 2500}, {"q", 2}, {"mix", 70}});
        add("Barrido lento", {{"rate", .3}, {"low", 200}, {"high", 2000}, {"q", 3}, {"mix", 60}});
        add("Barrido rápido", {{"rate", 3}, {"low", 400}, {"high", 3500}, {"q", 3}, {"mix", 85}});
    } else if (id == "gapper") {
        add("Tartamudeo", {{"mode", 0}, {"period", 125}, {"gap", 50}});
        add("Pulso suave", {{"mode", 0}, {"period", 250}, {"gap", 20}});
        add("Cortes rápidos", {{"mode", 0}, {"period", 60}, {"gap", 45}}); add("Eliminar huecos", {{"mode", 1}, {"period", 200}, {"gap", 35}});
    } else if (id == "gate") {
        add("Voz", {{"threshold", -40}, {"attack", 2}, {"release", 120}, {"floor", -80}});
        add("Ruido leve", {{"threshold", -50}, {"attack", 5}, {"release", 180}, {"floor", -60}});
        add("Puerta firme", {{"threshold", -30}, {"attack", 1}, {"release", 60}, {"floor", -96}});
    } else if (id == "pitch") {
        add("Octava arriba", {{"semitones", 12}}); add("Octava abajo", {{"semitones", -12}});
        add("Quinta arriba", {{"semitones", 7}}); add("Tercera menor", {{"semitones", 3}}); add("Tres semitonos abajo", {{"semitones", -3}});
    } else if (id == "pitch.bend") {
        add("Subida de octava", {{"start", 0}, {"end", 12}}); add("Caída de octava", {{"start", 12}, {"end", 0}});
        add("Bajada de octava", {{"start", 0}, {"end", -12}}); add("Subida sutil", {{"start", 0}, {"end", 2}});
    } else if (id == "reverb") {
        add("Habitación pequeña", {{"decay", .4}, {"predelay", 5}, {"damping", 65}, {"mix", 15}, {"tail", .7}});
        add("Sala media", {{"decay", 1.2}, {"predelay", 15}, {"damping", 50}, {"mix", 22}, {"tail", 2}});
        add("Hall cálido", {{"decay", 2.5}, {"predelay", 30}, {"damping", 75}, {"mix", 30}, {"tail", 3.5}});
        add("Hall brillante", {{"decay", 2.8}, {"predelay", 35}, {"damping", 20}, {"mix", 30}, {"tail", 4}});
        add("Catedral", {{"decay", 6}, {"predelay", 60}, {"damping", 35}, {"mix", 45}, {"tail", 7}});
        add("Voz sutil", {{"decay", .9}, {"predelay", 25}, {"damping", 60}, {"mix", 12}, {"tail", 1.5}});
        add("Metálico", {{"decay", 2}, {"predelay", 3}, {"damping", 5}, {"mix", 35}, {"tail", 3}});
    } else if (id == "vibrato") {
        add("Sutil", {{"rate", 5}, {"depth", .5}}); add("Clásico", {{"rate", 6}, {"depth", 1.5}});
        add("Lento y profundo", {{"rate", 2}, {"depth", 3}});
    }
    return presets;
}
QVector<ProcessPresets::Preset> ProcessPresets::list(const QString &id) {
    auto result = factory(id);
    QSettings settings; settings.beginGroup(group(id));
    for (const auto &k : settings.childKeys()) {
        const auto item = settings.value(k).toMap(); const auto map = item.value("values").toMap();
        Dsp::Values input, values; for (auto i = map.cbegin(); i != map.cend(); ++i) input[i.key()] = i.value().toDouble();
        const QString name = item.value("name").toString();
        if (!name.isEmpty() && valid(id, input, values)) result.append({name, values, true});
    }
    return result;
}
bool ProcessPresets::save(const QString &id, const QString &rawName, const Dsp::Values &input, QString &error) {
    error.clear(); const QString name = rawName.trimmed(); Dsp::Values values;
    if (name.isEmpty()) { error = "Escribe un nombre para el preset."; return false; }
    for (const auto &p : factory(id)) if (p.name == name) { error = "Ese nombre pertenece a un preset incluido. Usa otro nombre."; return false; }
    if (!valid(id, input, values)) { error = "Los parámetros del preset no son válidos."; return false; }
    QVariantMap map; for (auto i = values.cbegin(); i != values.cend(); ++i) map[i.key()] = i.value();
    QSettings settings; settings.setValue(key(id, name), QVariantMap{{"name", name}, {"values", map}}); settings.sync();
    if (settings.status() != QSettings::NoError) { error = "No se pudo guardar el preset en la configuración."; return false; }
    return true;
}
bool ProcessPresets::remove(const QString &id, const QString &name, QString &error) {
    error.clear(); QSettings settings;
    if (!settings.contains(key(id, name))) { error = "Solo se pueden eliminar presets personales."; return false; }
    settings.remove(key(id, name)); settings.sync();
    if (settings.status() != QSettings::NoError) { error = "No se pudo eliminar el preset."; return false; }
    return true;
}
