#include "previewplayer.h"
#include "waveoutputmute.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <QtEndian>
#include <QEventLoop>
#include <cstdio>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#endif

struct PreviewPlayer::Native {
#ifdef Q_OS_WIN
    HWAVEOUT handle = nullptr;
    WaveOutputMute mute;
    struct Block { WAVEHDR header{}; QByteArray bytes; double peaks[2]{}; bool prepared = false; };
    std::array<Block, 4> blocks;
    int next = 0, frames = 0;
    int underruns = 0;
    bool floating = true;
#endif
};
PreviewPlayer::PreviewPlayer(QObject *parent) : QObject(parent), native(new Native) {
    worker = new QObject;
    timer = new QTimer(worker);
    timer->setInterval(5); timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, worker, [this] { pump(); });
    worker->moveToThread(&audioThread);
    connect(&audioThread, &QThread::finished, worker, &QObject::deleteLater);
    audioThread.start(QThread::HighPriority);
}
PreviewPlayer::~PreviewPlayer() { stop(); audioThread.quit(); audioThread.wait(); }
bool PreviewPlayer::active() const { return running.load(); }
bool PreviewPlayer::start(const AudioData &audio, quint32 device, QString &error) {
    bool started = false;
    QMetaObject::invokeMethod(worker, [&] {
        try { started = startOutput(audio, device, error); }
        catch (const std::bad_alloc &) { stopOutput(); error = "No hay memoria suficiente para la preescucha."; }
    }, Qt::BlockingQueuedConnection);
    return started;
}
bool PreviewPlayer::updateAudio(const AudioData &audio, QString &error) {
    bool updated = false;
    QMetaObject::invokeMethod(worker, [&] { updated = updateOutput(audio, error); }, Qt::BlockingQueuedConnection);
    return updated;
}
void PreviewPlayer::stop() {
    QMetaObject::invokeMethod(worker, [this] { stopOutput(); }, Qt::BlockingQueuedConnection);
}
bool PreviewPlayer::setMuted(bool value) {
    bool ok = true;
    QMetaObject::invokeMethod(worker, [&] {
#ifdef Q_OS_WIN
        ok = native->mute.set(native->handle, value);
#endif
        if (ok) muted = value;
    }, Qt::BlockingQueuedConnection);
    return ok;
}
void PreviewPlayer::postLevels(double left, double right) {
    if (muted) left = right = 0;
    const auto token = generation.load();
    QMetaObject::invokeMethod(this, [this, left, right, token] {
        if (token == generation.load() && levels) levels(left, right);
    }, Qt::QueuedConnection);
}
bool PreviewPlayer::startOutput(const AudioData &audio, quint32 device, QString &error) {
    stopOutput(); error.clear();
    if (audio.frames() <= 0 || audio.channels < 1 || audio.channels > 2 || audio.rate < 8000) { error = "La preescucha no contiene audio válido."; return false; }
#ifdef Q_OS_WIN
    current = audio; position = 0; fadeFrames = qMax(1, audio.rate / 50); fadeLeft = 0;
    WAVEFORMATEX format{}; format.nChannels = WORD(audio.channels); format.nSamplesPerSec = DWORD(audio.rate);
    auto open = [&] {
        format.wFormatTag = native->floating ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
        format.wBitsPerSample = native->floating ? 32 : 16;
        format.nBlockAlign = WORD(audio.channels * format.wBitsPerSample / 8); format.nAvgBytesPerSec = audio.rate * format.nBlockAlign;
        return waveOutOpen(&native->handle, UINT(device), &format, 0, 0, CALLBACK_NULL);
    };
    native->floating = true; auto code = open();
    if (code == WAVERR_BADFORMAT) { native->handle = nullptr; native->floating = false; code = open(); }
    if (code != MMSYSERR_NOERROR) { native->handle = nullptr; stopOutput(); error = "No se pudo abrir la salida de preescucha (" + QString::number(code) + ")."; return false; }
    if (!native->mute.set(native->handle, muted)) { stopOutput(); error = "El dispositivo no permite silenciar la preescucha."; return false; }
    native->next = 0; native->underruns = 0; native->frames = qMax(1, audio.rate / 50);
    waveOutPause(native->handle);
    for (int i = 0; i < int(native->blocks.size()); ++i) {
        auto &block = native->blocks[i];
        block.header = {}; block.bytes.resize(native->frames * format.nBlockAlign);
        fill(i); block.header.lpData = block.bytes.data(); block.header.dwBufferLength = DWORD(block.bytes.size());
        code = waveOutPrepareHeader(native->handle, &block.header, sizeof(WAVEHDR));
        if (code == MMSYSERR_NOERROR) { block.prepared = true; code = waveOutWrite(native->handle, &block.header, sizeof(WAVEHDR)); }
        if (code != MMSYSERR_NOERROR) { stopOutput(); error = "No se pudo preparar la preescucha (" + QString::number(code) + ")."; return false; }
    }
    code = waveOutRestart(native->handle);
    if (code != MMSYSERR_NOERROR) { stopOutput(); error = "No se pudo iniciar la preescucha."; return false; }
    running.store(true); timer->start(); return true;
#else
    Q_UNUSED(device) error = "La preescucha requiere Windows."; return false;
#endif
}
bool PreviewPlayer::updateOutput(const AudioData &audio, QString &error) {
    error.clear();
    if (!active() || !audio.frames() || audio.rate != current.rate || audio.channels != current.channels) { error = "Formato de preescucha incompatible."; return false; }
    pending = audio; hasPending = true;
    return true;
}
void PreviewPlayer::fill(int index) {
#ifdef Q_OS_WIN
    if (hasPending) { previous = current; current = pending; pending = {}; hasPending = false; fadeLeft = fadeFrames; }
    auto &block = native->blocks[index]; block.peaks[0] = block.peaks[1] = 0;
    // Leer por punteros const conserva los buffers compartidos. operator[] no
    // const separaba QVector y copiaba la preescucha entera en el hilo de audio.
    const float *currentSamples = current.samples.constData();
    const float *previousSamples = previous.samples.constData();
    const qint64 currentFrames = current.frames(), previousFrames = previous.frames();
    float *floating = reinterpret_cast<float *>(block.bytes.data()); char *pcm = block.bytes.data();
    for (int f = 0; f < native->frames; ++f, ++position) {
        const double mix = fadeLeft ? double(fadeFrames - fadeLeft + 1) / fadeFrames : 1;
        for (int c = 0; c < current.channels; ++c) {
            double value = currentSamples[(position % currentFrames) * current.channels + c];
            if (fadeLeft && previousFrames) value = mix * value + (1 - mix) * previousSamples[(position % previousFrames) * previous.channels + c];
            value = std::clamp(value, -1.0, 1.0);
            block.peaks[c] = std::max(block.peaks[c], std::abs(value));
            if (native->floating) floating[f * current.channels + c] = float(value);
            else qToLittleEndian(quint16(qint16(std::lround(value * 32767))), pcm + (f * current.channels + c) * 2);
        }
        if (fadeLeft) --fadeLeft;
    }
    if (!fadeLeft) previous = {};
    if (current.channels == 1) block.peaks[1] = block.peaks[0];
#else
    Q_UNUSED(index)
#endif
}
void PreviewPlayer::pump() {
#ifdef Q_OS_WIN
    if (!native->handle) return;
    if (std::all_of(native->blocks.begin(), native->blocks.end(), [](const Native::Block &block) { return (block.header.dwFlags & WHDR_DONE) != 0; })) ++native->underruns;
    double peaks[2]{}; bool updated = false;
    for (int i = 0; i < int(native->blocks.size()); ++i) {
        auto &block = native->blocks[native->next];
        if (!(block.header.dwFlags & WHDR_DONE)) break;
        peaks[0] = std::max(peaks[0], block.peaks[0]); peaks[1] = std::max(peaks[1], block.peaks[1]); updated = true;
        fill(native->next);
        const auto code = waveOutWrite(native->handle, &block.header, sizeof(WAVEHDR));
        if (code != MMSYSERR_NOERROR) {
            stopOutput(); const auto token = generation.load();
            QMetaObject::invokeMethod(this, [this, code, token] {
                if (token == generation.load() && failed) failed("Se interrumpió la salida de preescucha (" + QString::number(code) + ").");
            }, Qt::QueuedConnection);
            return;
        }
        native->next = (native->next + 1) % int(native->blocks.size());
    }
    if (updated) postLevels(peaks[0], peaks[1]);
#endif
}
void PreviewPlayer::stopOutput() {
    running.store(false); ++generation;
    timer->stop();
#ifdef Q_OS_WIN
    if (native->handle) {
        waveOutReset(native->handle);
        native->mute.set(native->handle, false);
        for (auto &block : native->blocks) if (block.prepared) { waveOutUnprepareHeader(native->handle, &block.header, sizeof(WAVEHDR)); block.prepared = false; }
        waveOutClose(native->handle); native->handle = nullptr;
    }
#endif
    current = {}; previous = {}; pending = {}; hasPending = false; position = 0; fadeLeft = 0;
    postLevels(0, 0);
}

int previewLoopTests() {
    int failures = 0;
    auto check = [&](bool ok, const char *text) { std::printf("%s preview loop %s\n", ok ? "PASS" : "FAIL", text); std::fflush(stdout); if (!ok) ++failures; };
#ifdef Q_OS_WIN
    if (!waveOutGetNumDevs()) { std::printf("SKIP preview loop: no output device\n"); return 0; }
    PreviewPlayer player; AudioData silence; silence.samples.fill(0, 4410 * 2); QString error;
    if (!player.start(silence, WAVE_MAPPER, error)) { std::printf("SKIP preview loop: %s\n", error.toUtf8().constData()); return 0; }
    auto wait = [] { QEventLoop loop; QTimer::singleShot(350, &loop, &QEventLoop::quit); loop.exec(); };
    struct Snapshot { HWAVEOUT handle = nullptr; qint64 position = 0; int underruns = 0; const float *samples = nullptr; };
    auto snapshot = [&] {
        Snapshot state;
        QMetaObject::invokeMethod(player.worker, [&] { state = {player.native->handle, player.position, player.native->underruns, player.current.samples.constData()}; }, Qt::BlockingQueuedConnection);
        return state;
    };
    int meterUpdates = 0; player.levels = [&](double, double) { ++meterUpdates; };
    wait(); const auto handle = snapshot().handle; const qint64 before = snapshot().position;
    check(player.active() && before > silence.frames() * 2 && meterUpdates > 3, "repeats beyond the fragment and feeds meters continuously");
    DWORD originalVolume = 0, actualVolume = 1;
    waveOutGetVolume(handle, &originalVolume);
    bool muted = player.setMuted(true); wait(); waveOutGetVolume(handle, &actualVolume);
    check(muted && actualVolume == 0 && player.active() && snapshot().handle == handle && snapshot().position > before,
        "mute silences preview without stopping its position or reopening the device");
    bool unmuted = player.setMuted(false); waveOutGetVolume(handle, &actualVolume);
    check(unmuted && actualVolume == originalVolume && player.active() && snapshot().handle == handle, "unmute restores the preview volume on the same device");
    check(player.updateAudio(silence, error), "accepts a new preview buffer while playing"); wait();
    check(player.active() && snapshot().handle == handle && snapshot().position > before, "updates preserve the output device and playback position");
    AudioData incompatible = silence; incompatible.rate = 48000;
    check(!player.updateAudio(incompatible, error) && player.active(), "invalid updates preserve the running preview");
    player.stop(); check(!player.active(), "explicit Stop releases the continuous output");
    AudioData longPreview; longPreview.samples.fill(0, 44100 * 2 * 15);
    player.setMuted(true);
    check(player.start(longPreview, WAVE_MAPPER, error) && snapshot().samples == longPreview.samples.constData(),
        "filling long preview blocks does not detach or copy the shared audio");
    waveOutGetVolume(snapshot().handle, &actualVolume);
    check(actualVolume == 0 && player.active(), "new preview streams inherit mute before queuing audio");
    player.setMuted(false);
    const auto beforeGuiStall = snapshot();
    QThread::msleep(200);  // Simula pintura, sliders o cualquier bloqueo de la interfaz.
    const auto afterGuiStall = snapshot();
    check(player.active() && afterGuiStall.position > beforeGuiStall.position + 4410 && afterGuiStall.underruns == 0,
        "audio advances without underruns while the UI thread is blocked for 200 ms");
    AudioData otherPreview = longPreview; otherPreview.samples.fill(0);
    bool changed = true;
    for (int i = 0; i < 24; ++i) {
        changed &= player.updateAudio(i % 2 ? longPreview : otherPreview, error);
        QThread::msleep(10);
    }
    wait(); const auto afterUpdates = snapshot();
    check(changed && afterUpdates.samples == longPreview.samples.constData() && afterUpdates.handle == beforeGuiStall.handle
        && afterUpdates.underruns == 0, "rapid long-buffer updates retain sharing, the device and an uninterrupted queue");
    player.stop();
    bool stableGain = true;
    QMetaObject::invokeMethod(player.worker, [&] {
        // Prueba de señal sin enviarla al dispositivo: muchos cambios no deben
        // acumular atenuación ni alterar la relación entre L/R.
        AudioData quiet, loud;
        quiet.samples.resize(4096); loud.samples.resize(4096);
        for (int i = 0; i < 4096; i += 2) { quiet.samples[i] = .25f; quiet.samples[i + 1] = -.25f; loud.samples[i] = .5f; loud.samples[i + 1] = -.5f; }
        player.current = quiet; player.position = 0; player.fadeFrames = 882; player.fadeLeft = 0;
        player.native->frames = 882; player.native->floating = true;
        player.native->blocks[0].bytes.resize(882 * 2 * int(sizeof(float)));
        for (int iteration = 0; iteration < 80; ++iteration) {
            player.pending = iteration % 2 ? quiet : loud; player.hasPending = true; player.fill(0);
            const auto *samples = reinterpret_cast<const float *>(player.native->blocks[0].bytes.constData());
            for (int f = 0; f < 882; ++f)
                stableGain &= samples[f * 2] >= .24999f && samples[f * 2] <= .50001f && std::abs(samples[f * 2] + samples[f * 2 + 1]) < 1e-6;
        }
    }, Qt::BlockingQueuedConnection);
    check(stableGain, "repeated crossfades do not accumulate gain loss or stereo distortion");
#endif
    return failures;
}
