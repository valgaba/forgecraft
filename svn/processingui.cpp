#include "mainwindow.h"
#include "dsp.h"
#include "normalizedialog.h"
#include "previewplayer.h"
#include "processpreview.h"
#include "processpresets.h"
#include <QMenu>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QPointer>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSlider>
#include <QAbstractSpinBox>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QInputDialog>
#include <QLineEdit>
#include <QtConcurrent>
#include <cmath>

namespace {

// Unidad del parámetro, tomada del paréntesis final de su etiqueta:
// «Retardo (ms)» devuelve «ms».
QString unitOf(const QString &label) {
    const int open = label.lastIndexOf(QStringLiteral(" ("));
    if (open < 0 || !label.endsWith(QLatin1Char(')'))) return QString();
    return label.mid(open + 2, label.size() - open - 3);
}

// Los sliders trabajan con 1000 posiciones; los rangos de más de dos décadas
// (frecuencias, duraciones) se reparten en escala logarítmica para que sean
// manejables con el ratón.
bool logarithmicRange(double minimum, double maximum) { return minimum > 0 && maximum / minimum >= 100; }

int toPosition(double value, double minimum, double maximum, bool logarithmic) {
    if (maximum <= minimum) return 0;
    const double ratio = logarithmic ? std::log(value / minimum) / std::log(maximum / minimum) : (value - minimum) / (maximum - minimum);
    return int(std::lround(std::clamp(ratio, 0.0, 1.0) * 1000));
}

double fromPosition(int position, double minimum, double maximum, bool logarithmic) {
    const double ratio = position / 1000.0;
    return logarithmic ? minimum * std::pow(maximum / minimum, ratio) : minimum + (maximum - minimum) * ratio;
}

QString clockText(qint64 frames, int rate) {
    const qint64 ms = frames * 1000 / qMax(1, rate);
    return QString("%1:%2:%3,%4").arg(ms / 3600000, 2, 10, QChar('0')).arg(ms / 60000 % 60, 2, 10, QChar('0'))
        .arg(ms / 1000 % 60, 2, 10, QChar('0')).arg(ms % 1000, 3, 10, QChar('0'));
}

}

void MainWindow::setupProcessingMenus(QMenu *process, QMenu *effects) {
    process->setObjectName("processMenu"); effects->setObjectName("effectsMenu");
    QMap<QString, QMenu *> menus{{"Proceso", process}, {"Efectos", effects}};
    for (const auto &op : Dsp::operations()) {
        if (!menus.contains(op.menu)) {
            QString root = op.menu.section('/', 0, 0), name = op.menu.section('/', 1);
            menus[op.menu] = menus[root]->addMenu(name);
        }
        auto *action = menus[op.menu]->addAction(op.title); action->setObjectName("dsp." + op.id); action->setToolTip(op.description);
        connect(action, &QAction::triggered, this, [this, id = op.id] { runProcess(id); });
    }
}

bool MainWindow::playLivePreview(const AudioData &audio, std::function<void()> finished, QWidget *errorParent) {
    QString error;
    if (livePreview && livePreview->active()) return livePreview->updateAudio(audio, error);
    stopPlayback();
    if (!livePreview) livePreview = std::make_unique<PreviewPlayer>();
    if (!livePreview->setMuted(outputMuted)) { error = "El dispositivo no permite silenciar la preescucha."; return false; }
    livePreview->levels = [this](double left, double right) {
        if (outputMuted) left = right = 0;
        if (outputVu) outputVu->setLevels(left, right);
        if (previewVu) previewVu->setLevels(left, right);
    };
    livePreview->failed = [this, finished](const QString &error) { statusBar()->showMessage(error, 5000); if (finished) finished(); };
    bool started = false;
    try { started = livePreview->start(audio, outputDevice, error); }
    catch (const std::bad_alloc &) { livePreview->stop(); error = "No hay memoria suficiente para la preescucha."; }
    if (!started) QMessageBox::warning(errorParent, "Previsualizar", error);
    return started;
}

// Diálogo de parámetros con el aspecto de los editores clásicos:
// sliders para los valores, columna de botones a la derecha y pie con la
// selección y los canales. Los sliders son los del tema Radit.
void MainWindow::runProcess(const QString &id) {
    auto *doc = current(); if (!doc) return;
    const Dsp::Operation *op = nullptr; for (const auto &candidate : Dsp::operations()) if (candidate.id == id) { op = &candidate; break; }
    if (!op || (!doc->data.frames() && id != "insert_silence")) return;
    stop();
    auto values = Dsp::defaults(id);
    qint64 processFirst = doc->wave->first(), processLast = doc->wave->last();
    if (id == "resample") values["rate"] = doc->data.rate;
    if (id == "normalize") {
        NormalizeDialog dialog(doc->data, doc->wave->first(), doc->wave->last(), doc->name, this);
        previewVu = dialog.meters;
        dialog.previewAudio = [this, &dialog](const AudioData &audio, std::function<void()> finished) {
            return playLivePreview(audio, std::move(finished), &dialog);
        };
        dialog.stopAudio = [this] { stopPlayback(); };
        const int accepted = dialog.exec();
        stopPlayback(); previewVu = nullptr;
        if (accepted != QDialog::Accepted) return;
        values = dialog.values();
        processFirst = dialog.firstFrame(); processLast = dialog.lastFrame();
    } else if (!op->parameters.isEmpty()) {
        QDialog dialog(this);
        dialog.setObjectName("processDialog");
        dialog.setWindowTitle(op->title);
        auto *root = new QVBoxLayout(&dialog);
        root->setContentsMargins(12, 12, 12, 10);
        root->setSpacing(10);
        auto *body = new QHBoxLayout; body->setSpacing(14); root->addLayout(body, 1);
        auto *content = new QVBoxLayout; content->setSpacing(8); content->setAlignment(Qt::AlignTop); body->addLayout(content, 1);
        auto *presetCombo = new QComboBox; presetCombo->setObjectName("processPresets");
        presetCombo->setAccessibleName("Preset del proceso o efecto"); content->addWidget(presetCombo);
        QVector<ProcessPresets::Preset> availablePresets;
        bool loadingPreset = false;

        QMap<QString, QComboBox *> choices;
        QVector<QSlider *> parameterSliders;
        QVector<QDoubleSpinBox *> parameterBoxes;
        QVector<QPair<QString, std::function<double()>>> readers;
        QVector<QPair<QString, std::function<void(double)>>> writers;
        QVector<const Dsp::Parameter *> numeric;
        for (const auto &parameter : op->parameters) if (parameter.choices.isEmpty()) numeric.append(&parameter);

        for (const auto &parameter : op->parameters) {
            if (parameter.choices.isEmpty()) continue;
            auto *row = new QHBoxLayout; row->setSpacing(8);
            auto *label = new QLabel(parameter.label); label->setObjectName("processParameter");
            auto *control = new QComboBox; control->setObjectName(parameter.key);
            control->addItems(parameter.choices); control->setCurrentIndex(int(values[parameter.key]));
            row->addWidget(label); row->addWidget(control, 1); content->addLayout(row);
            choices[parameter.key] = control;
        }

        auto link = [](QSlider *slider, QDoubleSpinBox *box, double minimum, double maximum, bool logarithmic) {
            QObject::connect(slider, &QSlider::valueChanged, box, [=](int position) {
                const QSignalBlocker block(box);
                box->setValue(fromPosition(position, minimum, maximum, logarithmic));
            });
            QObject::connect(box, &QDoubleSpinBox::valueChanged, slider, [=](double value) {
                const QSignalBlocker block(slider);
                slider->setValue(toPosition(value, minimum, maximum, logarithmic));
            });
        };

        if (numeric.size() >= 3) {
            // Muchos parámetros: columnas con el valor arriba, el slider y la
            // etiqueta debajo, como el ecualizador gráfico de la referencia.
            auto *columns = new QHBoxLayout; columns->setSpacing(4);
            for (const auto *parameter : numeric) {
                const bool logarithmic = logarithmicRange(parameter->minimum, parameter->maximum);
                auto *column = new QVBoxLayout; column->setSpacing(4);
                auto *slider = new QSlider(Qt::Vertical); slider->setRange(0, 1000);
                parameterSliders.append(slider);
                slider->setValue(toPosition(values[parameter->key], parameter->minimum, parameter->maximum, logarithmic));
                slider->setFixedHeight(140);
                auto *value = new QLabel(QString::number(values[parameter->key], 'f', parameter->decimals));
                auto exactValue = std::make_shared<double>(values[parameter->key]);
                value->setObjectName("processValue"); value->setAlignment(Qt::AlignCenter); value->setFixedWidth(70);
                QObject::connect(slider, &QSlider::valueChanged, value, [value, parameter, logarithmic, exactValue](int position) {
                    *exactValue = fromPosition(position, parameter->minimum, parameter->maximum, logarithmic);
                    value->setText(QString::number(*exactValue, 'f', parameter->decimals));
                });
                auto *caption = new QLabel(parameter->label); caption->setObjectName("processCaption");
                caption->setWordWrap(true); caption->setAlignment(Qt::AlignHCenter); caption->setFixedWidth(70);
                caption->setToolTip(parameter->label);
                column->addWidget(value, 0, Qt::AlignHCenter);
                column->addWidget(slider, 0, Qt::AlignHCenter);
                column->addWidget(caption);
                columns->addLayout(column);
                readers.append({parameter->key, [exactValue] { return *exactValue; }});
                writers.append({parameter->key, [slider, value, parameter, logarithmic, exactValue](double number) {
                    const QSignalBlocker block(slider); *exactValue = number;
                    slider->setValue(toPosition(number, parameter->minimum, parameter->maximum, logarithmic));
                    value->setText(QString::number(number, 'f', parameter->decimals));
                }});
            }
            content->addLayout(columns);
            content->addStretch(1);
        } else {
            // Uno o dos parámetros: las mismas columnas verticales, conservando
            // la caja numérica para introducir un valor exacto.
            auto *columns = new QHBoxLayout; columns->setSpacing(12);
            for (const auto *parameter : numeric) {
                const bool logarithmic = logarithmicRange(parameter->minimum, parameter->maximum);
                auto *column = new QVBoxLayout; column->setSpacing(4);
                auto *label = new QLabel(parameter->label); label->setObjectName("processCaption"); label->setWordWrap(true); label->setAlignment(Qt::AlignHCenter); label->setFixedWidth(150);
                auto *slider = new QSlider(Qt::Vertical); slider->setRange(0, 1000); slider->setFixedHeight(140);
                parameterSliders.append(slider);
                slider->setValue(toPosition(values[parameter->key], parameter->minimum, parameter->maximum, logarithmic));
                auto *box = new QDoubleSpinBox; box->setObjectName(parameter->key);
                parameterBoxes.append(box);
                box->setDecimals(parameter->decimals); box->setRange(parameter->minimum, parameter->maximum);
                box->setSingleStep(parameter->decimals ? .1 : 1); box->setValue(values[parameter->key]);
                const QString unit = unitOf(parameter->label);
                if (!unit.isEmpty()) box->setSuffix(" " + unit);
                box->setFixedWidth(140);
                column->addWidget(box, 0, Qt::AlignHCenter); column->addWidget(slider, 0, Qt::AlignHCenter); column->addWidget(label, 0, Qt::AlignHCenter);
                columns->addLayout(column, 1);
                readers.append({parameter->key, [box] { return box->value(); }});
                writers.append({parameter->key, [box](double value) { box->setValue(value); }});
                link(slider, box, parameter->minimum, parameter->maximum, logarithmic);
            }
            if (!numeric.isEmpty()) content->addLayout(columns);
            else delete columns;
            content->addStretch(1);
        }

        auto currentValues = [&] {
            Dsp::Values copy = values;
            for (const auto &reader : readers) copy[reader.first] = reader.second();
            for (auto it = choices.cbegin(); it != choices.cend(); ++it) copy[it.key()] = it.value()->currentIndex();
            return copy;
        };

        auto *buttons = new QVBoxLayout; buttons->setSpacing(6); body->addLayout(buttons);
        auto *applyButton = new QPushButton("Aplicar"); applyButton->setObjectName("processApply");
        applyButton->setDefault(true); applyButton->setAutoDefault(true);
        auto *cancelButton = new QPushButton("Cancelar"); cancelButton->setObjectName("processCancel");
        buttons->addWidget(applyButton); buttons->addWidget(cancelButton);
        presetCombo->setFixedHeight(applyButton->sizeHint().height());
        auto *savePreset = new QPushButton("Guardar como…"); savePreset->setObjectName("processSavePreset");
        auto *deletePreset = new QPushButton("Eliminar preset"); deletePreset->setObjectName("processDeletePreset"); deletePreset->setEnabled(false);
        buttons->addSpacing(6); buttons->addWidget(savePreset); buttons->addWidget(deletePreset); buttons->addSpacing(6);
        connect(applyButton, &QPushButton::clicked, &dialog, &QDialog::accept);
        connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

        qint64 previewFirst = doc->wave->first(), previewLast = doc->wave->last();
        if (previewFirst == previewLast) { previewFirst = doc->wave->cursor; previewLast = doc->data.frames(); }
        ProcessPreview previewController(doc->data, previewFirst, previewLast, op->id, &dialog);
        previewController.setValues(currentValues());
        previewController.playAudio = [this, &dialog](const AudioData &audio, std::function<void()> finished) {
            return playLivePreview(audio, std::move(finished), &dialog);
        };
        previewController.stopAudio = [this] { stopPlayback(); };
        previewVu = nullptr;
        if (op->previewAvailable) {
            auto *previewButton = new QPushButton("Previsualizar"); previewButton->setObjectName("processPreview");
            previewButton->setCheckable(true); previewButton->setToolTip("Preescucha en bucle con actualización de parámetros durante la reproducción");
            auto *bypassButton = new QPushButton("Bypass"); bypassButton->setObjectName("processBypass");
            bypassButton->setCheckable(true); bypassButton->setEnabled(false); bypassButton->setToolTip("Escucha el audio original para comparar");
            buttons->addWidget(previewButton); buttons->addWidget(bypassButton);
            auto *previewStatus = new QLabel; previewStatus->setObjectName("processPreviewStatus");
            previewStatus->setWordWrap(true); previewStatus->setMaximumWidth(160); buttons->addWidget(previewStatus);
            previewController.statusChanged = [previewStatus](const QString &text) { previewStatus->setText(text); };
            previewController.activeChanged = [previewButton, bypassButton](bool active) {
                const QSignalBlocker previewBlock(previewButton), bypassBlock(bypassButton);
                previewButton->setChecked(active); previewButton->setText(active ? "Detener preescucha" : "Previsualizar");
                bypassButton->setEnabled(active); if (!active) bypassButton->setChecked(false);
            };
            connect(previewButton, &QPushButton::toggled, &previewController, &ProcessPreview::setActive);
            connect(bypassButton, &QPushButton::toggled, &previewController, &ProcessPreview::setBypass);
            auto *meterColumn = new QVBoxLayout; body->addLayout(meterColumn);
            auto *title = new QLabel("Preescucha"); title->setAlignment(Qt::AlignHCenter); meterColumn->addWidget(title);
            auto *meters = new VuMeter(nullptr, Qt::Vertical); meters->setObjectName("processPreviewMeters");
            meterColumn->addWidget(meters, 1); previewVu = meters;
        }
        buttons->addStretch();
        auto changed = [&] {
            if (loadingPreset) return;
            const QSignalBlocker block(presetCombo); presetCombo->setCurrentIndex(0); deletePreset->setEnabled(false);
            previewController.setValues(currentValues());
        };
        for (auto *slider : parameterSliders) connect(slider, &QSlider::valueChanged, &previewController, changed);
        for (auto *box : parameterBoxes) connect(box, &QDoubleSpinBox::valueChanged, &previewController, changed);
        for (auto *choice : choices) connect(choice, &QComboBox::currentIndexChanged, &previewController, changed);
        auto reloadPresets = [&](const QString &selected) {
            const QSignalBlocker block(presetCombo);
            availablePresets = ProcessPresets::list(id); presetCombo->clear(); presetCombo->addItem("(Sin título)");
            for (const auto &preset : availablePresets) presetCombo->addItem(preset.name);
            const int index = selected.isEmpty() ? 0 : presetCombo->findText(selected);
            presetCombo->setCurrentIndex(qMax(0, index));
            deletePreset->setEnabled(index > 0 && availablePresets[index - 1].personal);
        };
        connect(presetCombo, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
            if (index <= 0 || index > availablePresets.size()) { deletePreset->setEnabled(false); return; }
            const auto preset = availablePresets[index - 1]; loadingPreset = true;
            for (const auto &writer : writers) writer.second(preset.values.value(writer.first));
            for (auto i = choices.cbegin(); i != choices.cend(); ++i) i.value()->setCurrentIndex(int(preset.values.value(i.key())));
            loadingPreset = false; deletePreset->setEnabled(preset.personal);
            previewController.setValues(currentValues());
        });
        connect(savePreset, &QPushButton::clicked, &dialog, [&] {
            bool accepted = false;
            const QString name = QInputDialog::getText(&dialog, "Guardar preset", "Nombre del preset:", QLineEdit::Normal, {}, &accepted).trimmed();
            if (!accepted || name.isEmpty()) return;
            QString error;
            if (!ProcessPresets::save(id, name, currentValues(), error)) { QMessageBox::warning(&dialog, "Guardar preset", error); return; }
            reloadPresets(name);
        });
        connect(deletePreset, &QPushButton::clicked, &dialog, [&] {
            const int index = presetCombo->currentIndex();
            if (index <= 0 || index > availablePresets.size() || !availablePresets[index - 1].personal) return;
            QString error;
            if (!ProcessPresets::remove(id, availablePresets[index - 1].name, error)) { QMessageBox::warning(&dialog, "Eliminar preset", error); return; }
            reloadPresets({});
        });
        reloadPresets({});

        auto *footer = new QVBoxLayout; footer->setSpacing(1);
        auto *selection = new QLabel(QString("Selección: %1 → %2  (%3)").arg(clockText(doc->wave->first(), doc->data.rate),
                                                                              clockText(doc->wave->last(), doc->data.rate),
                                                                              clockText(doc->wave->last() - doc->wave->first(), doc->data.rate)));
        selection->setObjectName("processFooter");
        auto *channels = new QLabel(QString("Canales: %1").arg(doc->data.channels == 2 ? "Estéreo (izquierdo y derecho)" : "Mono"));
        channels->setObjectName("processFooter");
        footer->addWidget(selection); footer->addWidget(channels);
        root->addLayout(footer);

        dialog.setMinimumWidth((numeric.size() >= 3 ? 220 + 74 * int(numeric.size()) : 640) + (op->previewAvailable ? 102 : 0));
        const int accepted = dialog.exec();
        previewController.setActive(false);
        previewVu = nullptr;
        if (accepted != QDialog::Accepted) return;
        values = currentValues();
    }
    auto cancel = std::make_shared<std::atomic_bool>(false);
    QProgressDialog progress("Procesando " + op->title, "Cancelar", 0, 0, this); progress.setObjectName("processingProgress"); progress.setMinimumDuration(0); progress.setWindowModality(Qt::ApplicationModal); progress.setAutoClose(false);
    QFutureWatcher<Dsp::Result> watcher;
    connect(&watcher, &QFutureWatcher<Dsp::Result>::finished, &progress, &QDialog::accept);
    connect(&progress, &QProgressDialog::canceled, this, [cancel] { cancel->store(true); });
    const AudioData before = doc->data; qint64 first = processFirst, last = processLast;
    watcher.setFuture(QtConcurrent::run([before, first, last, id, values, cancel] {
        try { return Dsp::apply(before, first, last, id, values, cancel.get()); }
        catch (const std::bad_alloc &) { Dsp::Result result; result.error = "No hay memoria suficiente para este proceso."; return result; }
    }));
    if (!watcher.isFinished()) progress.exec();
    watcher.waitForFinished();
    auto result = watcher.result();
    if (cancel->load() || result.canceled) return;
    if (!result.error.isEmpty()) { QMessageBox::warning(this, op->title, result.error); return; }
    commit(doc, result.audio, op->title, result.first, result.last);
    statusBar()->showMessage("Aplicado: " + op->title, 4000);
}
