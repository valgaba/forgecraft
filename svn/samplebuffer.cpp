#include "samplebuffer.h"
#include <QTemporaryFile>
#include <QDir>
#include <QStorageInfo>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <io.h>
#endif

namespace {
[[noreturn]] void storageError(const QString &message) { throw std::runtime_error(("Almacenamiento de audio: " + message).toUtf8().constData()); }
void validate(qsizetype count) { if (count < 0 || quint64(count) > quint64(SampleBuffer::maxBytes / sizeof(float))) storageError("el audio supera la capacidad de 256 GB por versión."); }
void copySamples(float *to, const float *from, qsizetype count) {
    constexpr qsizetype block = 1024 * 1024 / sizeof(float);
    for (qsizetype at = 0; at < count; at += block) std::memcpy(to + at, from + at, qMin(block, count - at) * sizeof(float));
}
}
struct SampleBuffer::Store {
    QVector<float> memory;
    std::unique_ptr<QTemporaryFile> file;
    float *mapped = nullptr;
    qsizetype capacity = 0, used = 0;
    ~Store() { if (mapped) file->unmap(reinterpret_cast<uchar *>(mapped)); }
    const float *pointer() const { return mapped ? mapped : memory.constData(); }
    float *pointer() { return mapped ? mapped : memory.data(); }
    void allocate(qsizetype count) {
        validate(count);
        if (!file && count <= SampleBuffer::memorySamples) { memory.resize(count); capacity = count; return; }
        if (!file) {
            file = std::make_unique<QTemporaryFile>(QDir::tempPath() + "/forgecraft-audio-XXXXXX.f32");
            if (!file->open()) storageError(file->errorString());
#ifdef Q_OS_WIN
            DWORD returned = 0;
            DeviceIoControl(reinterpret_cast<HANDLE>(_get_osfhandle(int(file->handle()))), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
#endif
        }
        const qint64 extra = qint64(count) * sizeof(float) - file->size();
        QStorageInfo storage(QDir::tempPath());
        if (storage.isValid() && storage.bytesAvailable() >= 0 && extra > storage.bytesAvailable() - 64ll * 1024 * 1024) storageError("no hay espacio libre suficiente en la carpeta temporal.");
        const bool hadMap = mapped != nullptr;
        const qint64 oldBytes = qint64(capacity) * sizeof(float);
        if (mapped) { if (!file->unmap(reinterpret_cast<uchar *>(mapped))) storageError("no se pudo liberar el mapa temporal."); mapped = nullptr; }
        if (!file->resize(qint64(count) * sizeof(float))) {
            const QString message = file->errorString();
            if (hadMap) mapped = reinterpret_cast<float *>(file->map(0, oldBytes));
            storageError(message);
        }
        auto *address = file->map(0, qint64(count) * sizeof(float));
        if (!address) {
            const QString message = file->errorString();
            if (hadMap) mapped = reinterpret_cast<float *>(file->map(0, oldBytes));
            storageError(message);
        }
        mapped = reinterpret_cast<float *>(address);
        if (!memory.isEmpty()) { copySamples(mapped, memory.constData(), memory.size()); memory.clear(); memory.squeeze(); }
        capacity = count;
    }
};
SampleBuffer::SampleBuffer(std::initializer_list<float> values) : SampleBuffer(QVector<float>(values)) {}
SampleBuffer::SampleBuffer(const QVector<float> &values) {
    if (values.isEmpty()) return;
    resize(values.size()); copySamples(data(), values.constData(), length);
}
bool SampleBuffer::diskBacked() const { return store && bool(store->file); }
QString SampleBuffer::backingPath() const { return diskBacked() ? store->file->fileName() : QString(); }
const float *SampleBuffer::constData() const { return store ? store->pointer() + offset : nullptr; }
float *SampleBuffer::data() { makeWritable(length); return store ? store->pointer() : nullptr; }
void SampleBuffer::makeWritable(qsizetype capacity) {
    validate(capacity);
    if (!store && !capacity) return;
    if (!store || !store.unique() || offset || length != store->used) {
        auto next = std::make_shared<Store>(); next->allocate(qMax(length, capacity));
        if (length) copySamples(next->pointer(), constData(), length);
        next->used = length; store = std::move(next); offset = 0;
    } else if (capacity > store->capacity) {
        const qsizetype grown = qMax(capacity, qMin(qsizetype(maxBytes / sizeof(float)), store->capacity + qMax(qsizetype(16384), store->capacity / 2)));
        store->allocate(grown);
    }
}
void SampleBuffer::resize(qsizetype count) {
    validate(count); if (count == length) return;
    if (!count) { clear(); return; }
    if (count < length) { length = count; return; }
    const qsizetype old = length;
    makeWritable(count);
    // Un archivo recién extendido ya contiene ceros; no tocar esas páginas.
    if (!store->file) std::fill(store->pointer() + old, store->pointer() + count, 0);
    else if (old < store->used) std::fill(store->pointer() + old, store->pointer() + qMin(count, store->used), 0);
    length = count; store->used = count;
}
void SampleBuffer::reserve(qsizetype count) { if (count > length) makeWritable(count); }
void SampleBuffer::fill(float value, qsizetype count) {
    const bool fresh = !length;
    if (count >= 0) resize(count);
    if (!length) return;
    if (fresh && value == 0) return;
    std::fill(data(), data() + length, value);
}
SampleBuffer SampleBuffer::mid(qsizetype first, qsizetype count) const {
    first = qBound(qsizetype(0), first, length);
    count = count < 0 ? length - first : qMin(count, length - first);
    if (!count) return {};
    SampleBuffer result; result.store = store; result.offset = offset + first; result.length = count; return result;
}
void SampleBuffer::remove(qsizetype first, qsizetype count) {
    first = qBound(qsizetype(0), first, length); count = qBound(qsizetype(0), count, length - first);
    *this = join({mid(0, first), mid(first + count)});
}
SampleBuffer SampleBuffer::join(std::initializer_list<SampleBuffer> parts) {
    qsizetype total = 0; for (const auto &part : parts) { validate(total + part.size()); total += part.size(); }
    SampleBuffer result; result.resize(total); float *to = result.data(); qsizetype at = 0;
    for (const auto &part : parts) { if (part.size()) copySamples(to + at, part.constData(), part.size()); at += part.size(); }
    return result;
}
bool operator==(const SampleBuffer &a, const SampleBuffer &b) {
    if (a.size() != b.size()) return false;
    if (!a.size() || a.constData() == b.constData()) return true;
    return std::equal(a.begin(), a.end(), b.begin());
}
