#pragma once
#include "audio.h"
#include <QMap>
#include <QStringList>
#include <atomic>

namespace Dsp {
using Values = QMap<QString, double>;
struct Parameter {
    QString key, label;
    double value, minimum, maximum;
    int decimals = 1;
    QStringList choices;
};
struct Operation {
    QString id, menu, title, description;
    QVector<Parameter> parameters;
    bool wholeDocument = false;
    bool previewAvailable = true;
};
struct Result {
    AudioData audio;
    qint64 first = 0, last = 0;
    QString error;
    bool canceled = false;
};
const QVector<Operation> &operations();
Values defaults(const QString &id);
Result apply(const AudioData &source, qint64 first, qint64 last, const QString &id,
             const Values &parameters = {}, const std::atomic_bool *cancel = nullptr);
}
