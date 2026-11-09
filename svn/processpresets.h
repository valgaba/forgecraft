#pragma once
#include "dsp.h"

namespace ProcessPresets {
struct Preset { QString name; Dsp::Values values; bool personal = false; };
QVector<Preset> factory(const QString &operation);
QVector<Preset> list(const QString &operation);
bool save(const QString &operation, const QString &name, const Dsp::Values &values, QString &error);
bool remove(const QString &operation, const QString &name, QString &error);
}
