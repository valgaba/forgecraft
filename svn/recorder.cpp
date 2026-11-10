#include "recorder.h"
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <array>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace { constexpr qint64 maxSamples = SampleBuffer::maxBytes / sizeof(float); }
void CaptureBuffer::reset(int rate, int channels) {
    audio = AudioData{}; audio.rate = rate; audio.channels = channels;
    limitFrames = 0; peaks[0] = peaks[1] = 0;
    previousInput[0] = previousInput[1] = previousOutput[0] = previousOutput[1] = 0;
}
qint64 CaptureBuffer::capacityFrames() const {
    if (audio.channels < 1 || audio.channels > 2) return 0;
    qint64 capacity = maxSamples / audio.channels;
    return limitFrames > 0 ? qMin(capacity, limitFrames) : capacity;
}
bool CaptureBuffer::consume(const char *pcm, qsizetype bytes, bool store) {
    if (audio.channels < 1 || audio.channels > 2 || audio.rate < 8000 || bytes < 0 || bytes % (audio.channels * 2)) return false;
    qint64 frames = bytes / (audio.channels * 2), old = audio.frames();
    qint64 count = store ? qMin(frames, qMax(qint64(0), capacityFrames() - old)) : 0;
    if (store) audio.samples.resize((old + count) * audio.channels);
    float *dst = store ? audio.samples.data() + old * audio.channels : nullptr;
    peaks[0] = peaks[1] = 0; double pole = std::exp(-2 * 3.141592653589793 * 20 / audio.rate);
    for (qint64 f = 0; f < frames; ++f) for (int c = 0; c < audio.channels; ++c) {
        double x = qint16(qFromLittleEndian<quint16>(pcm + (f * audio.channels + c) * 2)) / 32768.0;
        double filtered = x - previousInput[c] + pole * previousOutput[c]; previousInput[c] = x; previousOutput[c] = filtered;
        double y = muted ? 0 : std::clamp(dcAdjust ? filtered : x, -1.0, 1.0);
        peaks[c] = qMax(peaks[c], std::abs(y));
        if (f < count) dst[f * audio.channels + c] = float(y);
    }
    if (audio.channels == 1) peaks[1] = peaks[0];
    return !store || audio.frames() < capacityFrames();
}

Recording::MergeResult Recording::merge(const AudioData &original, const AudioData &take, Mode mode, qint64 position, qint64 selectionEnd) {
    MergeResult result;
    if (take.channels < 1 || take.channels > 2 || take.samples.isEmpty() || take.samples.size() % take.channels || take.rate < 8000) { result.error = "La toma no contiene audio válido."; return result; }
    if (take.samples.size() > maxSamples) { result.error = "La toma supera el límite de 256 GB."; return result; }
    if (mode == NewDocument) { result.audio = take; result.last = take.frames(); return result; }
    if (mode != Insert && mode != ReplaceSelection) { result.error = "Modo de grabación desconocido."; return result; }
    if (original.channels < 1 || original.channels > 2) { result.error = "Formato de destino no válido."; return result; }
    if (original.frames() && (original.rate != take.rate || original.channels != take.channels)) { result.error = "La toma y el documento deben tener la misma frecuencia y canales."; return result; }
    position = std::clamp(position, qint64(0), original.frames());
    qint64 end = mode == ReplaceSelection ? std::clamp(selectionEnd, position, original.frames()) : position;
    int channels = take.channels;
    if (original.frames() - (end - position) + take.frames() > maxSamples / channels) { result.error = "El resultado supera el límite de 256 GB. Añade la toma en una ventana nueva."; return result; }
    result.audio = original.frames() ? original : take;
    result.audio.samples = SampleBuffer::join({original.samples.first(position * channels), take.samples, original.samples.mid(end * channels)});
    result.first = position; result.last = position + take.frames(); return result;
}

struct Recorder::Native {
#ifdef Q_OS_WIN
    HWAVEIN handle = nullptr;
    struct Block { WAVEHDR header{}; QByteArray bytes; bool prepared = false; };
    std::array<Block, 4> blocks;
    int next = 0;
    QString error;
#endif
};

Recorder::Recorder(QObject *parent) : QObject(parent), native(new Native) {
    buffer.reset(44100, 2); timer.setInterval(15); connect(&timer, &QTimer::timeout, this, &Recorder::poll);
}
Recorder::~Recorder() { stop(); }
QVector<RecordingDevice> Recorder::devices() {
    QVector<RecordingDevice> result;
#ifdef Q_OS_WIN
    UINT count = waveInGetNumDevs();
    if (count) result.append({quint32(WAVE_MAPPER), "Entrada predeterminada de Windows"});
    for (UINT id = 0; id < count; ++id) { WAVEINCAPSW caps{}; if (waveInGetDevCapsW(id, &caps, sizeof(caps)) == MMSYSERR_NOERROR) result.append({id, QString::fromWCharArray(caps.szPname)}); }
#endif
    return result;
}
bool Recorder::active() const {
#ifdef Q_OS_WIN
    return native->handle != nullptr;
#else
    return false;
#endif
}
void Recorder::clear(int rate, int channels) { stop(); buffer.reset(rate, channels); emit updated(); }
bool Recorder::start(quint32 device, int rate, int channels, bool record, QString &error) {
    stop(); error.clear();
    if ((channels != 1 && channels != 2) || rate < 8000 || rate > 192000) { error = "Formato de captura no válido."; return false; }
    if (buffer.audio.frames() && (buffer.audio.rate != rate || buffer.audio.channels != channels)) { error = "Crea una toma nueva antes de cambiar el formato."; return false; }
    buffer.audio.rate = rate; buffer.audio.channels = channels; storing = record;
    if (record && buffer.audio.frames() >= buffer.capacityFrames()) { error = "La toma ha alcanzado el límite de duración o memoria."; return false; }
#ifdef Q_OS_WIN
    WAVEFORMATEX format{}; format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = WORD(channels); format.nSamplesPerSec = DWORD(rate);
    format.wBitsPerSample = 16; format.nBlockAlign = WORD(channels * 2); format.nAvgBytesPerSec = DWORD(rate * channels * 2);
    auto message = [](MMRESULT code) { wchar_t text[MAXERRORLENGTH]{}; waveInGetErrorTextW(code, text, MAXERRORLENGTH); return QString::fromWCharArray(text) + " (" + QString::number(code) + ")"; };
    MMRESULT code = waveInOpen(&native->handle, UINT(device), &format, 0, 0, CALLBACK_NULL);
    if (code != MMSYSERR_NOERROR) { native->handle = nullptr; error = "No se pudo abrir la entrada de audio.\n" + message(code); return false; }
    native->next = 0; native->error.clear();
    for (auto &block : native->blocks) {
        block.header = {};
        try { block.bytes.resize(qMax(1, rate / 20) * channels * 2); }
        catch (const std::bad_alloc &) { error = "No hay memoria para los buffers de captura."; stop(); return false; }
        block.header.lpData = block.bytes.data(); block.header.dwBufferLength = DWORD(block.bytes.size());
        code = waveInPrepareHeader(native->handle, &block.header, sizeof(WAVEHDR));
        if (code == MMSYSERR_NOERROR) { block.prepared = true; code = waveInAddBuffer(native->handle, &block.header, sizeof(WAVEHDR)); }
        if (code != MMSYSERR_NOERROR) { error = "No se pudo preparar la captura.\n" + message(code); stop(); return false; }
    }
    code = waveInStart(native->handle);
    if (code != MMSYSERR_NOERROR) { error = "No se pudo iniciar la captura.\n" + message(code); stop(); return false; }
    timer.start(); return true;
#else
    Q_UNUSED(device) error = "La grabación de esta versión requiere Windows."; return false;
#endif
}
bool Recorder::drain(bool requeue) {
#ifdef Q_OS_WIN
    bool withinLimit = true;
    for (size_t i = 0; i < native->blocks.size(); ++i) {
        auto &block = native->blocks[native->next];
        if (!block.prepared || !(block.header.dwFlags & WHDR_DONE)) break;
        native->next = (native->next + 1) % int(native->blocks.size());
        if (block.header.dwBytesRecorded > block.header.dwBufferLength || block.header.dwBytesRecorded % (buffer.audio.channels * 2)) { block.header.dwBytesRecorded = 0; native->error = "La entrada ha devuelto un bloque PCM no válido. Se conserva la toma capturada."; return false; }
        if (block.header.dwBytesRecorded) {
            bool ok = buffer.consume(block.bytes.constData(), block.header.dwBytesRecorded, storing);
            block.header.dwBytesRecorded = 0; withinLimit &= ok;
        }
        if (requeue && withinLimit && waveInAddBuffer(native->handle, &block.header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) { native->error = "Se interrumpió la cola de entrada de audio. Se conserva la toma capturada."; return false; }
    }
    return withinLimit;
#else
    Q_UNUSED(requeue) return false;
#endif
}
void Recorder::poll() {
    bool keepGoing = false;
    try { keepGoing = drain(true); }
    catch (const std::exception &e) { storing = false; stop(); emit failed(QString::fromUtf8(e.what())); return; }
    emit updated();
    if (!keepGoing) {
        stop();
#ifdef Q_OS_WIN
        if (!native->error.isEmpty()) { emit failed(native->error); return; }
#endif
        emit limitReached();
    }
}
void Recorder::stop() {
    timer.stop();
#ifdef Q_OS_WIN
    if (native->handle) {
        waveInStop(native->handle); waveInReset(native->handle);
        try { drain(false); } catch (const std::exception &) { /* Keep the captured prefix and release the device. */ }
        for (auto &block : native->blocks) if (block.prepared) { waveInUnprepareHeader(native->handle, &block.header, sizeof(WAVEHDR)); block.prepared = false; }
        waveInClose(native->handle); native->handle = nullptr;
    }
#endif
    storing = false;
}
