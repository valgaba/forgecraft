#include "playbackstream.h"
#include <QThread>
#include <QTimer>
#include <QtEndian>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

struct PlaybackStream::Impl {
    QThread thread;
    QObject *worker = nullptr;
    QTimer *timer = nullptr;
    AudioData source;
    std::atomic_bool done{false};
    std::atomic<qint64> played{0}, bytes{0};
    QString failure;
    qint64 first = 0, last = 0, next = 0;
    quint64 wraps = 0;
    quint32 previousPosition = 0;
    bool floating = true;
#ifdef Q_OS_WIN
    HWAVEOUT handle = nullptr;
    struct Block { WAVEHDR header{}; QByteArray pcm; bool prepared = false, queued = false; };
    std::array<Block, 4> blocks;
    bool queue(Block &block) {
        if (next >= last) { block.queued = false; return true; }
        if (block.prepared) { waveOutUnprepareHeader(handle, &block.header, sizeof(WAVEHDR)); block.prepared = false; }
        const qint64 count = qMin(qint64(qMax(1, source.rate / 50)), last - next);
        const float *input = source.samples.constData() + next * source.channels;
        block.pcm.resize(count * source.channels * (floating ? 4 : 2));
        if (floating) std::memcpy(block.pcm.data(), input, block.pcm.size());
        else for (qint64 i = 0; i < count * source.channels; ++i) qToLittleEndian(quint16(qint16(std::lround(std::clamp(double(input[i]), -1.0, 1.0) * 32767))), block.pcm.data() + i * 2);
        block.header = {}; block.header.lpData = block.pcm.data(); block.header.dwBufferLength = DWORD(block.pcm.size());
        auto code = waveOutPrepareHeader(handle, &block.header, sizeof(WAVEHDR));
        if (code == MMSYSERR_NOERROR) { block.prepared = true; code = waveOutWrite(handle, &block.header, sizeof(WAVEHDR)); }
        if (code != MMSYSERR_NOERROR) { failure = "Se interrumpió la cola de reproducción (" + QString::number(code) + ")."; done.store(true); timer->stop(); return false; }
        block.queued = true; next += count; return true;
    }
    void pump() {
        if (!handle) return;
        for (auto &block : blocks) if (block.queued && (block.header.dwFlags & WHDR_DONE)) { if (!queue(block)) return; }
        MMTIME now{}; now.wType = TIME_SAMPLES;
        if (waveOutGetPosition(handle, &now, sizeof(now)) == MMSYSERR_NOERROR) {
            const quint32 value = now.wType == TIME_SAMPLES ? now.u.sample : now.wType == TIME_BYTES ? now.u.cb : now.u.ms;
            if (value < previousPosition && previousPosition - value > 0x80000000u) wraps += (1ull << 32);
            previousPosition = value;
            const quint64 absolute = wraps + value;
            const qint64 frames = now.wType == TIME_SAMPLES ? qint64(absolute) : now.wType == TIME_BYTES ? qint64(absolute / (source.channels * (floating ? 4 : 2))) : qint64(absolute * source.rate / 1000);
            played.store(qMin(last - first, frames));
        }
        if (next >= last && std::all_of(blocks.begin(), blocks.end(), [](const Block &block) { return !block.queued; })) { played.store(last - first); timer->stop(); done.store(true); }
    }
#endif
    void stop() {
        timer->stop();
#ifdef Q_OS_WIN
        if (handle) {
            waveOutReset(handle);
            for (auto &block : blocks) {
                if (block.prepared) waveOutUnprepareHeader(handle, &block.header, sizeof(WAVEHDR));
                block = {};
            }
        }
        handle = nullptr;
#endif
        source = {}; bytes.store(0);
    }
};
PlaybackStream::PlaybackStream() : impl(new Impl) {
    impl->worker = new QObject; impl->timer = new QTimer(impl->worker);
    impl->timer->setInterval(5); impl->timer->setTimerType(Qt::PreciseTimer);
    QObject::connect(impl->timer, &QTimer::timeout, impl->worker, [this] {
#ifdef Q_OS_WIN
        try { impl->pump(); } catch (const std::exception &e) { impl->failure = QString::fromUtf8(e.what()); impl->timer->stop(); impl->done.store(true); }
#endif
    });
    impl->worker->moveToThread(&impl->thread);
    QObject::connect(&impl->thread, &QThread::finished, impl->worker, &QObject::deleteLater);
    impl->thread.start(QThread::HighPriority);
}
PlaybackStream::~PlaybackStream() { stop(); impl->thread.quit(); impl->thread.wait(); }
bool PlaybackStream::start(void *handle, const AudioData &audio, qint64 first, qint64 last, bool floating, QString &error) {
    bool ok = false;
    QMetaObject::invokeMethod(impl->worker, [&] {
        impl->stop(); impl->failure.clear(); impl->done.store(false); impl->played.store(0);
#ifdef Q_OS_WIN
        impl->handle = static_cast<HWAVEOUT>(handle); impl->source = audio; impl->first = first; impl->last = last; impl->next = first;
        impl->floating = floating; impl->wraps = 0; impl->previousPosition = 0;
        waveOutPause(impl->handle);
        try {
            ok = true;
            for (auto &block : impl->blocks) if (!impl->queue(block)) { ok = false; break; }
            if (ok) {
                qint64 bytes = 0; for (const auto &block : impl->blocks) bytes += block.pcm.size(); impl->bytes.store(bytes);
                if (waveOutRestart(impl->handle) != MMSYSERR_NOERROR) { ok = false; impl->failure = "No se pudo iniciar la reproducción por bloques."; }
                else impl->timer->start();
            }
        } catch (const std::exception &e) { impl->failure = QString::fromUtf8(e.what()); ok = false; }
        if (!ok) impl->stop();
#else
        Q_UNUSED(handle) Q_UNUSED(audio) Q_UNUSED(first) Q_UNUSED(last) Q_UNUSED(floating)
#endif
        error = impl->failure;
    }, Qt::BlockingQueuedConnection);
    return ok;
}
void PlaybackStream::stop() { QMetaObject::invokeMethod(impl->worker, [this] { impl->stop(); }, Qt::BlockingQueuedConnection); }
bool PlaybackStream::finished() const { return impl->done.load(); }
qint64 PlaybackStream::position() const { return impl->played.load(); }
QString PlaybackStream::error() const { return finished() ? impl->failure : QString(); }
qint64 PlaybackStream::bufferedBytes() const { return impl->bytes.load(); }
