#include "mainwindow.h"
#include "dsp.h"
#include "normalizedialog.h"
#include "previewplayer.h"
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
#include <QFrame>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
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

// Diálogo de parámetros con el aspecto de los editores clásicos: nombre arriba,
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
            QString error;
            if (livePreview && livePreview->active()) return livePreview->updateAudio(audio, error);
            stopPlayback();
            if (!livePreview) livePreview = std::make_unique<PreviewPlayer>();
            livePreview->levels = [this](double left, double right) {
                if (outputVu) outputVu->setLevels(left, right);
                if (previewVu) previewVu->setLevels(left, right);
            };
            livePreview->failed = [this, finished](const QString &error) { statusBar()->showMessage(error, 5000); if (finished) finished(); };
            bool started = false;
            try { started = livePreview->start(audio, outputDevice, error); }
            catch (const std::bad_alloc &) { livePreview->stop(); error = "No hay memoria suficiente para la preescucha."; }
            if (!started) QMessageBox::warning(&dialog, "Previsualizar", error);
            return started;
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
        auto *content = new QVBoxLayout; content->setSpacing(8); body->addLayout(content, 1);

        auto *nameLabel = new QLabel("Nombre:"); nameLabel->setObjectName("processNameLabel");
        auto *nameValue = new QLabel(op->title); nameValue->setObjectName("processName");
        auto *nameRow = new QHBoxLayout; nameRow->setSpacing(8);
        nameRow->addWidget(nameLabel); nameRow->addWidget(nameValue, 1); content->addLayout(nameRow);

        auto *description = new QLabel(op->description); description->setObjectName("processDescription"); description->setWordWrap(true); description->setMaximumWidth(600); content->addWidget(description);
        auto *scope = new QLabel(op->wholeDocument ? "Ámbito: documento completo." : id == "insert_silence" ? "Ámbito: inserción en el cursor / inicio de selección." : doc->wave->first() == doc->wave->last() ? "Ámbito: todo el audio (no hay selección)." : "Ámbito: selección actual."); scope->setObjectName("processScope"); scope->setMaximumWidth(600); content->addWidget(scope);

        auto *separator = new QFrame; separator->setObjectName("processSeparator"); separator->setFixedHeight(1); content->addWidget(separator);

        QMap<QString, QComboBox *> choices;
        QVector<QPair<QString, std::function<double()>>> readers;
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
                slider->setValue(toPosition(values[parameter->key], parameter->minimum, parameter->maximum, logarithmic));
                slider->setFixedHeight(140);
                auto *value = new QLabel(QString::number(values[parameter->key], 'f', parameter->decimals));
                value->setObjectName("processValue"); value->setAlignment(Qt::AlignCenter); value->setFixedWidth(70);
                QObject::connect(slider, &QSlider::valueChanged, value, [value, parameter, logarithmic](int position) {
                    value->setText(QString::number(fromPosition(position, parameter->minimum, parameter->maximum, logarithmic), 'f', parameter->decimals));
                });
                auto *caption = new QLabel(parameter->label); caption->setObjectName("processCaption");
                caption->setWordWrap(true); caption->setAlignment(Qt::AlignHCenter); caption->setFixedWidth(70);
                caption->setToolTip(parameter->label);
                column->addWidget(value, 0, Qt::AlignHCenter);
                column->addWidget(slider, 0, Qt::AlignHCenter);
                column->addWidget(caption);
                columns->addLayout(column);
                readers.append({parameter->key, [slider, parameter, logarithmic] {
                    return fromPosition(slider->value(), parameter->minimum, parameter->maximum, logarithmic);
                }});
            }
            content->addLayout(columns);
            content->addStretch(1);
        } else {
            // Uno o dos parámetros: fila ancha con el slider y su caja de valor.
            for (const auto *parameter : numeric) {
                const bool logarithmic = logarithmicRange(parameter->minimum, parameter->maximum);
                auto *row = new QHBoxLayout; row->setSpacing(10);
                auto *label = new QLabel(parameter->label); label->setObjectName("processParameter"); label->setMinimumWidth(150);
                auto *slider = new QSlider(Qt::Horizontal); slider->setRange(0, 1000);
                slider->setValue(toPosition(values[parameter->key], parameter->minimum, parameter->maximum, logarithmic));
                auto *box = new QDoubleSpinBox; box->setObjectName(parameter->key);
                box->setDecimals(parameter->decimals); box->setRange(parameter->minimum, parameter->maximum);
                box->setSingleStep(parameter->decimals ? .1 : 1); box->setValue(values[parameter->key]);
                const QString unit = unitOf(parameter->label);
                if (!unit.isEmpty()) box->setSuffix(" " + unit);
                box->setFixedWidth(118);
                row->addWidget(label); row->addWidget(slider, 1); row->addWidget(box);
                content->addLayout(row);
                readers.append({parameter->key, [box] { return box->value(); }});
                link(slider, box, parameter->minimum, parameter->maximum, logarithmic);
            }
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
        auto *helpButton = new QPushButton("Ayuda"); helpButton->setObjectName("processHelp");
        auto *previewButton = new QPushButton("Previsualizar"); previewButton->setObjectName("processPreview");
        previewButton->setCheckable(true); previewButton->setToolTip("Reproduce el resultado con los parámetros actuales, sin aplicarlo");
        auto *bypassButton = new QPushButton("Bypass"); bypassButton->setObjectName("processBypass");
        bypassButton->setCheckable(true); bypassButton->setEnabled(false); bypassButton->setToolTip("Escucha el audio original para comparar");
        buttons->addWidget(applyButton); buttons->addWidget(cancelButton); buttons->addWidget(helpButton);
        buttons->addWidget(previewButton); buttons->addWidget(bypassButton); buttons->addStretch();
        connect(applyButton, &QPushButton::clicked, &dialog, &QDialog::accept);
        connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

        // Vista previa: procesa el tramo de escucha y lo reproduce sin aplicarlo.
        // Bypass alterna entre el resultado y el original durante la escucha.
        struct PreviewState {
            QPointer<QPushButton> preview, bypass;
            AudioData wet;
            qint64 first = 0, last = 0;
            bool active = false;
        };
        auto previewState = std::make_shared<PreviewState>();
        previewState->preview = previewButton; previewState->bypass = bypassButton;
        auto endPreview = [this, previewState] {
            stop();
            previewState->active = false;
            if (previewState->preview) previewState->preview->setChecked(false);
            if (previewState->bypass) { previewState->bypass->setChecked(false); previewState->bypass->setEnabled(false); }
        };
        connect(previewButton, &QPushButton::toggled, &dialog, [&, previewState, endPreview](bool on) {
            if (!on) { endPreview(); return; }
            qint64 first = doc->wave->first(), last = doc->wave->last();
            if (first == last) { first = qMin(doc->wave->cursor, doc->data.frames()); last = doc->data.frames(); }
            if (last <= first) { endPreview(); return; }
            const qint64 limit = 15 * qint64(doc->data.rate);   // 15 s de escucha como máximo
            if (last - first > limit) last = first + limit;
            QApplication::setOverrideCursor(Qt::WaitCursor);
            Dsp::Result result;
            try { result = Dsp::apply(doc->data, first, last, op->id, currentValues()); }
            catch (const std::bad_alloc &) { QApplication::restoreOverrideCursor(); QMessageBox::warning(&dialog, op->title, "No hay memoria suficiente para la vista previa."); endPreview(); return; }
            QApplication::restoreOverrideCursor();
            if (!result.error.isEmpty()) { QMessageBox::warning(&dialog, op->title, result.error); endPreview(); return; }
            previewState->wet = result.audio; previewState->first = result.first; previewState->last = result.last;
            previewState->active = true;
            if (previewState->bypass) previewState->bypass->setEnabled(true);
            if (!playPreview(previewState->wet, previewState->first, previewState->last, [previewState, endPreview] { endPreview(); })) endPreview();
        });
        connect(bypassButton, &QPushButton::toggled, &dialog, [this, previewState, doc, endPreview](bool on) {
            if (!previewState->active) return;
            const auto finished = [previewState, endPreview] { endPreview(); };
            const bool started = on ? playPreview(doc->data, previewState->first, qMin(previewState->last, doc->data.frames()), finished)
                                    : playPreview(previewState->wet, previewState->first, previewState->last, finished);
            if (!started) endPreview();
        });
        connect(helpButton, &QPushButton::clicked, &dialog, [this, &dialog, op, scope] {
            QString text = op->description + "\n\n" + scope->text() + "\n\nParámetros:\n";
            for (const auto &parameter : op->parameters) {
                text += "• " + parameter.label + ": ";
                if (!parameter.choices.isEmpty()) text += parameter.choices.join(", ");
                else text += QString("%1 a %2 %3").arg(QString::number(parameter.minimum), QString::number(parameter.maximum), unitOf(parameter.label));
                text += "\n";
            }
            QMessageBox::information(&dialog, op->title, text.trimmed());
        });

        auto *footer = new QVBoxLayout; footer->setSpacing(1);
        auto *selection = new QLabel(QString("Selección: %1 → %2  (%3)").arg(clockText(doc->wave->first(), doc->data.rate),
                                                                              clockText(doc->wave->last(), doc->data.rate),
                                                                              clockText(doc->wave->last() - doc->wave->first(), doc->data.rate)));
        selection->setObjectName("processFooter");
        auto *channels = new QLabel(QString("Canales: %1").arg(doc->data.channels == 2 ? "Estéreo (izquierdo y derecho)" : "Mono"));
        channels->setObjectName("processFooter");
        footer->addWidget(selection); footer->addWidget(channels);
        root->addLayout(footer);

        dialog.setMinimumWidth(numeric.size() >= 3 ? 40 + 74 * int(numeric.size()) : 640);
        const int accepted = dialog.exec();
        endPreview();
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
