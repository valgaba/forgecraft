#include "processpreview.h"
#include <QPointer>
#include <QtConcurrent>
#include <algorithm>

namespace {
// Mantener un formato de salida fijo permite alternar bypass y parámetros de
// canales/frecuencia sin cerrar waveOut. El documento conserva su formato real.
AudioData outputFormat(AudioData audio, int rate, qint64 silentFrames, const std::atomic_bool *cancel, QString &error) {
    if (!audio.frames()) { audio.rate = rate; audio.channels = 2; audio.samples.fill(0, qMax(qint64(1), silentFrames) * 2); return audio; }
    if (audio.rate != rate) {
        auto result = Dsp::apply(audio, 0, audio.frames(), "resample", {{"rate", double(rate)}}, cancel);
        if (!result.error.isEmpty()) { error = result.error; return {}; }
        if (result.canceled) return {};
        audio = std::move(result.audio);
    }
    if (audio.channels == 1) {
        auto result = Dsp::apply(audio, 0, audio.frames(), "channels", {{"mode", 3}}, cancel);
        if (!result.error.isEmpty()) { error = result.error; return {}; }
        if (result.canceled) return {};
        audio = std::move(result.audio);
    }
    return audio;
}
}

ProcessPreview::ProcessPreview(const AudioData &audio, qint64 begin, qint64 end, const QString &id, QObject *parent)
    : QObject(parent), source(audio), first(begin), last(end), operation(id), parameters(Dsp::defaults(id)) {
    first = std::clamp(first, qint64(0), source.frames()); last = std::clamp(last, first, source.frames());
    if (first == last) last = source.frames();
    last = qMin(last, first + 15 * qint64(source.rate));
    refresh.setSingleShot(true);
    connect(&refresh, &QTimer::timeout, this, &ProcessPreview::rebuild);
    connect(&watcher, &QFutureWatcher<Work>::finished, this, &ProcessPreview::ready);
}
ProcessPreview::~ProcessPreview() { setActive(false); watcher.waitForFinished(); }
void ProcessPreview::status(const QString &text) { if (statusChanged) statusChanged(text); }
void ProcessPreview::setValues(const Dsp::Values &values) {
    if (parameters == values) return;
    parameters = values;
    if (enabled) request(80);
}
void ProcessPreview::setActive(bool on) {
    if (enabled == on) return;
    enabled = on;
    if (on) {
        if (activeChanged) activeChanged(true);
        request(0);
    } else {
        refresh.stop(); ++revision; pending = false;
        if (cancel) cancel->store(true);
        const bool wasStarted = outputStarted; outputStarted = false; bypass = false;
        if (wasStarted && stopAudio) stopAudio();
        if (activeChanged) activeChanged(false);
        status({});
    }
}
void ProcessPreview::setBypass(bool on) {
    bypass = on;
    if (enabled && dry.frames()) playCurrent();
}
void ProcessPreview::request(int delay) {
    ++revision; pending = true;
    if (cancel) cancel->store(true);
    status(outputStarted ? "Actualizando…" : "Preparando…"); refresh.start(delay);
}
void ProcessPreview::rebuild() {
    if (!enabled || !pending || watcher.isRunning()) return;
    pending = false; cancel = std::make_shared<std::atomic_bool>(false);
    const auto canceled = cancel; const auto values = parameters; const auto audio = source;
    const auto id = operation; const auto a = first, b = last; const auto version = revision;
    watcher.setFuture(QtConcurrent::run([audio, values, id, a, b, version, canceled] {
        Work work; work.revision = version;
        try {
            if (b <= a) { work.error = "No hay audio para preescuchar en esta posición."; return work; }
            const AudioData fragment = audio.fragment(a, b);
            auto previewValues = values;
            // No reservar minutos de silencio para escuchar un tramo de 15 s.
            if (id == "insert_silence") previewValues["seconds"] = qMin(15.0, values.value("seconds"));
            auto result = Dsp::apply(fragment, 0, fragment.frames(), id, previewValues, canceled.get());
            work.error = result.error; work.canceled = result.canceled || canceled->load();
            if (!work.error.isEmpty() || work.canceled) return work;
            auto processed = result.first == 0 && result.last == result.audio.frames()
                ? std::move(result.audio) : result.audio.fragment(result.first, result.last);
            const int outputRate = std::clamp(audio.rate, 8000, 192000);
            const qint64 silentFrames = qMax(qint64(1), fragment.frames() * outputRate / audio.rate);
            work.dry = outputFormat(fragment, outputRate, silentFrames, canceled.get(), work.error);
            if (canceled->load() || !work.error.isEmpty()) { work.canceled = canceled->load(); return work; }
            work.wet = outputFormat(std::move(processed), outputRate, silentFrames, canceled.get(), work.error);
            work.canceled = canceled->load();
        } catch (const std::exception &e) { work.error = QString::fromUtf8(e.what()); }
        return work;
    }));
}
void ProcessPreview::ready() {
    const auto work = watcher.result();
    if (enabled && work.revision == revision && !work.canceled) {
        if (!work.error.isEmpty()) status(work.error);
        else {
            dry = work.dry; wet = work.wet;
            if (!bypass || !outputStarted) playCurrent(); else status("Bypass · en bucle");
        }
    }
    if (enabled && pending && !refresh.isActive()) refresh.start(0);
}
void ProcessPreview::playCurrent() {
    const QPointer<ProcessPreview> self(this);
    const AudioData &audio = bypass ? dry : wet;
    if (!audio.frames()) return;
    if (!playAudio || !playAudio(audio, [self] { if (self) self->setActive(false); })) { setActive(false); return; }
    outputStarted = true;
    status(bypass ? "Bypass · en bucle" : "En bucle");
}
