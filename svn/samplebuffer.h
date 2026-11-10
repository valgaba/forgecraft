#pragma once
#include <QVector>
#include <QString>
#include <memory>
#include <initializer_list>

// Copias y recortes comparten almacenamiento. Al editar se crea una versión
// independiente; los bloques grandes viven en un temporal mapeado, paginado
// por Windows, en lugar de reservar varios GB de RAM por cada deshacer.
class SampleBuffer {
public:
    static constexpr qint64 maxBytes = 256ll * 1024 * 1024 * 1024;
    static constexpr qsizetype memorySamples = 8 * 1024 * 1024 / sizeof(float);
    SampleBuffer() = default;
    SampleBuffer(qsizetype count, float value = 0) { fill(value, count); }
    SampleBuffer(std::initializer_list<float> values);
    SampleBuffer(const QVector<float> &values);
    qsizetype size() const { return length; }
    bool isEmpty() const { return length == 0; }
    bool diskBacked() const;
    QString backingPath() const;
    const float *constData() const;
    const float *data() const { return constData(); }
    float *data();
    const float &operator[](qsizetype i) const { return constData()[i]; }
    float &operator[](qsizetype i) { return data()[i]; }
    const float &first() const { return (*this)[0]; }
    float &first() { return (*this)[0]; }
    const float &last() const { return (*this)[length - 1]; }
    float &last() { return (*this)[length - 1]; }
    const float *begin() const { return constData(); }
    const float *end() const { return length ? constData() + length : constData(); }
    const float *cbegin() const { return begin(); }
    const float *cend() const { return end(); }
    float *begin() { return data(); }
    float *end() { return length ? data() + length : data(); }
    void resize(qsizetype count);
    void reserve(qsizetype count);
    void clear() { *this = SampleBuffer(); }
    void fill(float value, qsizetype count = -1);
    SampleBuffer mid(qsizetype first, qsizetype count = -1) const;
    SampleBuffer first(qsizetype count) const { return mid(0, count); }
    void remove(qsizetype first, qsizetype count);
    static SampleBuffer join(std::initializer_list<SampleBuffer> parts);
    friend SampleBuffer operator+(const SampleBuffer &a, const SampleBuffer &b) { return join({a, b}); }
    friend bool operator==(const SampleBuffer &a, const SampleBuffer &b);
    friend bool operator!=(const SampleBuffer &a, const SampleBuffer &b) { return !(a == b); }
private:
    struct Store;
    std::shared_ptr<Store> store;
    qsizetype offset = 0, length = 0;
    void makeWritable(qsizetype capacity);
};
