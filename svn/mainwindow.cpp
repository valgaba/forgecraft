#include "mainwindow.h"
#include "audioformats.h"
#include "audiofiledialogs.h"
#include "aboutdialog.h"
#include "newaudiodialog.h"
#include "mixdialog.h"
#include "optionsdialog.h"
#include "recorder.h"
#include "previewplayer.h"
#include "theme.h"
#include <QApplication>
#include <QActionGroup>
#include <QMenu>
#include <QSettings>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QStyle>
#include <QUndoCommand>
#include <QProgressBar>
#include <QProgressDialog>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QtConcurrent>
#include <atomic>
#include <QPainter>
#include <QToolButton>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>

// Mezcla `fragment` sobre `destination` desde `at`, con el volumen de cada parte
// y un desplazamiento opcional. Devuelve el resultado y el tramo modificado.
static AudioData blendFragment(AudioData destination, const AudioData &fragment, qint64 at, double sourceGain, double targetGain, qint64 tune, qint64 &first, qint64 &last) {
    const qint64 begin = at + tune;
    first = qMax(qint64(0), begin);
    last = begin + fragment.frames();
    if (last <= 0) { last = first; return destination; }
    if (last > destination.frames()) destination.samples.resize(last * destination.channels);
    for (qint64 frame = first; frame < last; ++frame) {
        const qint64 source = frame - begin;
        if (source < 0 || source >= fragment.frames()) continue;
        for (int channel = 0; channel < destination.channels; ++channel) {
            const qsizetype index = frame * destination.channels + channel;
            const double mixed = double(destination.samples[index]) * targetGain + double(fragment.samples[source * destination.channels + channel]) * sourceGain;
            destination.samples[index] = float(std::clamp(mixed, -1.0, 1.0));
        }
    }
    return destination;
}
#endif

static QString timeText(qint64 frame, int rate);   // definida más abajo
struct MainWindow::Playback {
#ifdef Q_OS_WIN
    HWAVEOUT handle = nullptr;
    WAVEHDR header{};
#endif
    QByteArray bytes;
    AudioData source;    // Conserva las muestras durante toda la salida waveOut.
    int bytesPerFrame = 0;
    bool floatOutput = false;
    Document *document = nullptr;
    qint64 begin = 0, end = 0;
    qint64 selectionAnchor = 0, selectionCursor = 0;
    bool loop = false;
};

class AudioEdit : public QUndoCommand {
    Document *d;
    AudioData before, after;
    qint64 oldAnchor, oldCursor, newAnchor, newCursor;
    void apply(const AudioData &data, qint64 anchor, qint64 cursor) {
        d->data = data; d->wave->invalidateCache(); d->wave->anchor = anchor; d->wave->cursor = cursor;
        d->wave->start = std::min(d->wave->start, std::max(qint64(0), data.frames() - d->wave->visibleFrames()));
        d->wave->update(); emit d->wave->selectionChanged();
    }
public:
    AudioEdit(Document *doc, const AudioData &next, const QString &label, qint64 a, qint64 b)
        : QUndoCommand(label), d(doc), before(doc->data), after(next), oldAnchor(doc->wave->anchor), oldCursor(doc->wave->cursor), newAnchor(a), newCursor(b) {}
    void undo() override { apply(before, oldAnchor, oldCursor); }
    void redo() override { apply(after, newAnchor, newCursor); }
};

void AudioWindow::closeEvent(QCloseEvent *event) {
    if (canClose && !canClose()) { event->ignore(); return; }
    QMdiSubWindow::closeEvent(event);
}

// La barra de título es propia, así que el título de la subventana se replica
// en la etiqueta del módulo.
bool AudioWindow::event(QEvent *event) {
    const bool handled = QMdiSubWindow::event(event);
    if (event->type() == QEvent::WindowTitleChange)
        if (auto *document = dynamic_cast<Document *>(widget())) document->setTitle(windowTitle());
    return handled;
}

// Subventana a la que pertenece un widget del módulo (encabezado o tirador).
// No sirve QWidget::window(): una subventana MDI es un widget incrustado
// (Qt::SubWindow), así que window() devuelve la ventana principal.
static QMdiSubWindow *moduleWindow(QWidget *widget) {
    for (QWidget *current = widget; current; current = current->parentWidget())
        if (auto *sub = qobject_cast<QMdiSubWindow *>(current)) return sub;
    return nullptr;
}

// Documento (módulo) del área de trabajo que hay bajo un punto global, y su
// forma de onda si el puntero está justo encima del lienzo. Se consulta al árbol
// del área de trabajo, no a la ventana que esté encima en pantalla.
static Document *documentAt(QWidget *root, const QPoint &global, Waveform *&wave) {
    wave = nullptr;
    QWidget *widget = root->childAt(root->mapFromGlobal(global));
    for (; widget; widget = widget->parentWidget()) {
        if (auto *form = dynamic_cast<Waveform *>(widget)) wave = form;
        if (auto *document = dynamic_cast<Document *>(widget)) return document;
        if (widget == root) break;
    }
    return nullptr;
}

ModuleHeader::ModuleHeader(QWidget *parent) : QFrame(parent) {
    setObjectName("framebarra");
    setFixedHeight(26);
}

// Documento al que pertenece una barra de título.
static Document *documentOf(QWidget *header) { return dynamic_cast<Document *>(header->parentWidget()); }

void ModuleHeader::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        auto *document = documentOf(this);
        // Un módulo minimizado no se arrastra: su botón está fijo en el pie.
        if (!(document && document->isMinimized())) {
            if (auto *sub = moduleWindow(this)) {
                dragging = true;
                dragOrigin = event->globalPosition().toPoint();
                windowOrigin = sub->pos();
                sub->raise();
            }
        }
    }
    QFrame::mousePressEvent(event);
}

void ModuleHeader::mouseMoveEvent(QMouseEvent *event) {
    auto *sub = moduleWindow(this);
    if (!dragging || !sub || !(event->buttons() & Qt::LeftButton) || sub->isMaximized()) {
        QFrame::mouseMoveEvent(event);
        return;
    }
    sub->move(windowOrigin + (event->globalPosition().toPoint() - dragOrigin));
}

void ModuleHeader::mouseDoubleClickEvent(QMouseEvent *) {
    dragging = false;
    if (auto *document = documentOf(this)) {
        // En la barra minimizada, el doble clic restaura en vez de maximizar.
        if (document->isMinimized()) { document->setMinimized(false); return; }
    }
    auto *sub = moduleWindow(this);
    if (!sub) return;
    if (sub->isMaximized()) sub->showNormal(); else sub->showMaximized();
}

void ModuleGrip::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setPen(QPen(Theme::BorderSoft, 1));
    for (int offset = 4; offset <= 12; offset += 4) p.drawLine(width() - 1 - offset, height() - 1, width() - 1, height() - 1 - offset);
}

Document::Document(QWidget *parent) : QWidget(parent) {
    setObjectName("moduleFrame");
    setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    header = new ModuleHeader(this);
    auto *titleRow = new QHBoxLayout(header); titleRow->setContentsMargins(8, 0, 4, 0); titleRow->setSpacing(2);
    titleLabel = new QLabel(name, header); titleLabel->setObjectName("PanelTitle"); titleLabel->setToolTip("Arrastra para mover el módulo; doble clic para maximizar");
    minimizeButton = new QToolButton(header); minimizeButton->setObjectName("moduleMinimize");
    minimizeButton->setIcon(Theme::icon("minimize")); minimizeButton->setIconSize(QSize(14, 14)); minimizeButton->setToolTip("Minimizar módulo"); minimizeButton->setAutoRaise(true);
    connect(minimizeButton, &QToolButton::clicked, this, [this] { setMinimized(!minimized); });
    auto *close = new QToolButton(header); close->setObjectName("moduleClose");
    close->setIcon(Theme::icon("close")); close->setIconSize(QSize(14, 14)); close->setToolTip("Cerrar módulo"); close->setAutoRaise(true);
    connect(close, &QToolButton::clicked, this, [this] { if (auto *frame = moduleWindow(this)) frame->close(); });
    titleRow->addWidget(titleLabel, 1); titleRow->addWidget(minimizeButton, 0, Qt::AlignVCenter); titleRow->addWidget(close, 0, Qt::AlignVCenter);
    layout->addWidget(header);
    body = new QWidget(this);
    auto *content = new QVBoxLayout(body); content->setContentsMargins(4, 4, 4, 4); content->setSpacing(3);
    layout->addWidget(body, 1);
    auto *overview = new QFrame; overview->setObjectName("overviewStrip"); overview->setFixedHeight(11); content->addWidget(overview);
    wave = new Waveform(this); wave->audio = &data; content->addWidget(wave, 1); undo.setUndoLimit(20);
    wave->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(wave, &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        if (wave->selectionDragActive()) return;
        // Con el botón derecho sobre un banderín se abre el menú de la marca.
        const int marker = wave->markerAt(point);
        if (marker >= 0) { if (wave->markerMenu) wave->markerMenu(marker, wave->mapToGlobal(point)); return; }
        if (contextMenu) contextMenu(wave->mapToGlobal(point));
    });
    // El desplazamiento queda a la izquierda y las lupas a la derecha del módulo.
    auto *navigation = new QHBoxLayout; navigation->setSpacing(3); content->addLayout(navigation);
    auto button = [this](QHBoxLayout *row, const QString &icon, const QString &tip, auto fn) {
        auto *b = new QToolButton; b->setIcon(Theme::icon(icon)); b->setIconSize(QSize(18, 18)); b->setFixedSize(30, 28); b->setToolTip(tip); row->addWidget(b); connect(b, &QToolButton::clicked, this, fn);
    };
    scroll = new QScrollBar(Qt::Horizontal); navigation->addWidget(scroll, 1);
    connect(scroll, &QScrollBar::valueChanged, this, [this](int value) { wave->start = qint64(value) * qMax(qint64(0), data.frames() - wave->visibleFrames()) / 100000; wave->update(); });
    button(navigation, "zoom-in", "Acercar", [this] { wave->zoomBy(2); emit wave->selectionChanged(); });
    button(navigation, "zoom-out", "Alejar", [this] { wave->zoomBy(.5); emit wave->selectionChanged(); });
    auto *row = new QHBoxLayout; row->setSpacing(3); content->addLayout(row);
    auto *recordControl = new QToolButton; recordControl->setFixedSize(30, 28); recordControl->setToolTip("Grabar / detener");
    { QPixmap dot(16,16); dot.fill(Qt::transparent); { QPainter p(&dot); p.setPen(Theme::RecordDot.darker(150)); p.setBrush(Theme::RecordDot); p.drawEllipse(3,3,10,10); } recordIcon = QIcon(dot); }
    { QPixmap dot(16,16); dot.fill(Qt::transparent); { QPainter p(&dot); p.setPen(Theme::RecordDot.darker(150)); p.setBrush(Theme::RecordDot.darker(210)); p.drawEllipse(3,3,10,10); } recordIconDark = QIcon(dot); }
    recordControl->setIcon(recordIcon); recordButton = recordControl; row->addWidget(recordControl);
    connect(recordControl, &QToolButton::clicked, this, [this] { if (record) record(); });
    button(row, "start", "Ir al inicio", [this] { if (beginning) beginning(); });
    button(row, "end", "Ir al final", [this] { if (ending) ending(); });
    button(row, "stop", "Detener", [this] { if (stop) stop(); });
    button(row, "play", "Reproducir", [this] { if (play) play(); });
    followButton = new QToolButton;
    followButton->setObjectName("followPlayback");
    followButton->setAccessibleName("Seguir reproducción");
    followButton->setIcon(Theme::icon("follow-playhead"));
    followButton->setIconSize(QSize(18, 18)); followButton->setFixedSize(30, 28);
    followButton->setCheckable(true); followButton->setChecked(wave->followPlayback);
    followButton->setToolTip("Seguir reproducción: fijar la línea roja en el centro y desplazar la forma de onda");
    row->addWidget(followButton);
    connect(followButton, &QToolButton::toggled, this, [this](bool follow) {
        wave->followPlayback = follow;
        if (follow && wave->playhead >= 0) wave->setPlayhead(wave->playhead);
        emit wave->selectionChanged();
    });
    row->addStretch();
    position = new QLabel("00:00:00.000"); selectionEnd = new QLabel("00:00:00.000"); selectionLength = new QLabel("00:00:00.000");
    position->setToolTip("Inicio de selección"); selectionEnd->setToolTip("Final de selección"); selectionLength->setToolTip("Duración de selección");
    for (auto *field : {position, selectionEnd, selectionLength}) { field->setObjectName("timeField"); field->setMinimumWidth(92); field->setAlignment(Qt::AlignCenter); row->addWidget(field); }
    grip = new ModuleGrip(this); grip->raise();
    wave->dragOut = [this](qint64 first, qint64 last, const QPoint &global) {
        if (copySelectionToDocument) copySelectionToDocument(first, last, global);
    };
}

void Document::setRecordingOn(bool on) {
    if (recordOn == on) return;
    recordOn = on;
    recordBlink = false;
    if (recordButton) recordButton->setIcon(recordIcon);
    if (position) {
        position->setStyleSheet(on ? QStringLiteral("color:#e06c6c;font-weight:bold;") : QString());
        if (!on) position->setText(timeText(wave ? wave->cursor : 0, data.rate));
    }
}
void Document::setRecordTime(qint64 frames, int rate) {
    if (position) position->setText(timeText(frames, rate));
}
void Document::toggleRecordBlink() {
    if (!recordButton || !recordOn) return;
    recordBlink = !recordBlink;
    recordButton->setIcon(recordBlink ? recordIcon : recordIconDark);
}
void Document::setTitle(const QString &title) {
    fullTitle = title;
    if (!titleLabel) return;
    if (minimized) titleLabel->setText(QFontMetrics(titleLabel->font()).elidedText(title, Qt::ElideRight, 92));
    else titleLabel->setText(title);
}

// El módulo activo se distingue por la barra de título (propiedad `active`).
void Document::setActive(bool active) {
    if (!header || header->property("active").toBool() == active) return;
    header->setProperty("active", active);
    for (QWidget *widget : {static_cast<QWidget *>(header), static_cast<QWidget *>(titleLabel)}) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

// Minimizar convierte el módulo en un botón bajo, apoyado en el pie del área de
// trabajo, con la flecha de restaurar, el título recortado y el cierre, como los
// documentos minimizados de los editores clásicos.
void Document::setMinimized(bool value) {
    if (!body || minimized == value) return;
    minimized = value;
    auto *frame = moduleWindow(this);
    auto *row = header ? qobject_cast<QHBoxLayout *>(header->layout()) : nullptr;
    if (value) {
        if (frame) { expandedSize = frame->size(); expandedPosition = frame->pos(); }
        body->hide();
        if (grip) grip->hide();
        if (minimizeButton) {
            minimizeButton->setIcon(Theme::icon("restore"));
            minimizeButton->setToolTip("Restaurar módulo");
            if (row) { row->removeWidget(minimizeButton); row->insertWidget(0, minimizeButton, 0, Qt::AlignVCenter); }
        }
        if (header) header->setProperty("minimized", true);
        if (frame) frame->setFixedSize(MinimizedWidth, MinimizedHeight);
        if (titleLabel) titleLabel->setText(QFontMetrics(titleLabel->font()).elidedText(fullTitle, Qt::ElideRight, 92));
    } else {
        if (frame) {
            frame->setMinimumSize(0, 0);
            frame->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            frame->resize(expandedSize.isValid() ? expandedSize : QSize(700, 340));
            frame->move(expandedPosition);
        }
        body->show();
        if (grip) grip->show();
        if (minimizeButton) {
            minimizeButton->setIcon(Theme::icon("minimize"));
            minimizeButton->setToolTip("Minimizar módulo");
            if (row) { row->removeWidget(minimizeButton); row->insertWidget(row->count() - 1, minimizeButton, 0, Qt::AlignVCenter); }
        }
        if (header) header->setProperty("minimized", false);
        if (titleLabel) titleLabel->setText(fullTitle);
    }
    if (header) { header->style()->unpolish(header); header->style()->polish(header); header->update(); }
    if (moduleStateChanged) moduleStateChanged();
}

void Document::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (grip && grip->isVisible()) grip->move(width() - grip->width(), height() - grip->height());
}

static QString timeText(qint64 frame, int rate) {
    qint64 ms = frame * 1000 / rate;
    return QString("%1:%2:%3.%4").arg(ms / 3600000, 2, 10, QChar('0')).arg(ms / 60000 % 60, 2, 10, QChar('0')).arg(ms / 1000 % 60, 2, 10, QChar('0')).arg(ms % 1000, 3, 10, QChar('0'));
}

MainWindow::MainWindow(std::function<Recorder *()> source, bool monitorInput) : playback(new Playback), recordingSource(std::move(source)), inputMonitoringEnabled(monitorInput) {
    outputDevice = QSettings().value("audio/outputDevice", quint32(0xffffffffu)).toUInt();
    inputDevice = QSettings().value("audio/inputDevice", quint32(0xffffffffu)).toUInt();
    setWindowTitle("ForgeCraft"); resize(1500, 850);
    workspace = new QMdiArea(this); workspace->setObjectName("workspace"); setCentralWidget(workspace);
    auto *file = menuBar()->addMenu("&Archivo");
    auto *editing = menuBar()->addMenu("&Edición");
    auto *view = menuBar()->addMenu("&Ver");
    auto *process = menuBar()->addMenu("&Proceso");
    auto *effects = menuBar()->addMenu("&Efectos");
    auto *transport = new QMenu(this);   // sin menú visible: solo agrupa las acciones del transporte
    auto *options = menuBar()->addMenu("&Opciones");
    auto *windows = menuBar()->addMenu("&Ventana");
    auto *help = menuBar()->addMenu("A&yuda");
    auto *tools = addToolBar("Archivo y edición"); tools->setObjectName("mainTools"); tools->setIconSize(QSize(18,18)); tools->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto add = [this](QMenu *menu, const QString &text, const QKeySequence &key, auto fn, const QString &icon = QString()) {
        QAction *a = menu->addAction(text); a->setShortcut(key); if (!icon.isEmpty()) a->setIcon(Theme::icon(icon)); connect(a, &QAction::triggered, this, fn); return a;
    };
    tools->addAction(add(file, "&Nuevo", QKeySequence::New, [this] {
        NewAudioDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted) return;
        AudioData data; data.rate = dialog.rate(); data.channels = dialog.channels();
        data.bitDepth = dialog.bitDepth(); data.floatFormat = dialog.floatFormat();
        addDocument(data);
    }, "new"));
    tools->addAction(add(file, "&Abrir audio…", QKeySequence::Open, [this] { const auto paths = QFileDialog::getOpenFileNames(this, "Abrir audio", {}, AudioFormats::openFilters()); for (int i = 0; i < paths.size(); ++i) openFile(paths[i], i > 0); }, "open"));
    tools->addAction(add(file, "&Guardar", QKeySequence::Save, [this] { if (current()) save(current()); }, "save"));
    add(file, "Guardar &como…", QKeySequence::SaveAs, [this] { if (current()) save(current(), true); }, "save-as");
    file->addSeparator(); add(file, "&Cerrar documento", QKeySequence::Close, [this] { workspace->closeActiveSubWindow(); });
    add(file, "&Salir", QKeySequence::Quit, [this] { close(); });
    tools->addSeparator();
    undoAction = add(editing, "&Deshacer", QKeySequence::Undo, [this] { stopPlayback(); if (current()) current()->undo.undo(); }, "undo"); tools->addAction(undoAction);
    redoAction = add(editing, "&Rehacer", QKeySequence::Redo, [this] { stopPlayback(); if (current()) current()->undo.redo(); }, "redo"); tools->addAction(redoAction);
    editing->addSeparator();
    cutAction = add(editing, "Cor&tar", QKeySequence::Cut, [this] { copy(); edit("delete"); }, "cut"); tools->addAction(cutAction);
    copyAction = add(editing, "&Copiar", QKeySequence::Copy, [this] { copy(); }, "copy"); tools->addAction(copyAction);
    pasteAction = add(editing, "&Pegar", QKeySequence::Paste, [this] { paste(); }, "paste"); tools->addAction(pasteAction);
    deleteAction = add(editing, "&Eliminar selección", QKeySequence::Delete, [this] { edit("delete"); });
    trimAction = add(editing, "&Recortar a selección", QKeySequence("Ctrl+T"), [this] { edit("trim"); });
    selectAllAction = add(editing, "Seleccionar &todo", QKeySequence::SelectAll, [this] { if (auto *d = current()) { d->wave->anchor = 0; d->wave->cursor = d->data.frames(); d->wave->update(); refresh(); } });
    add(editing, "&Insertar marca", QKeySequence("M"), [this] { addMarker(); });
    add(editing, "&Eliminar marca", QKeySequence("Shift+M"), [this] { removeMarker(); });
    editing->addSeparator();
    // Qué ocurre al soltar una selección sobre otra ventana de audio.
    auto *dropMenu = editing->addMenu("Al soltar en otra ventana");
    auto *insertDrop = dropMenu->addAction("Insertar en el punto de destino");
    insertDrop->setObjectName("dropInsert"); insertDrop->setCheckable(true);
    auto *mixDrop = dropMenu->addAction("Mezclar con el audio de destino");
    mixDrop->setObjectName("dropMix"); mixDrop->setCheckable(true); mixDrop->setChecked(true);
    auto *dropGroup = new QActionGroup(this); dropGroup->addAction(insertDrop); dropGroup->addAction(mixDrop); dropGroup->setExclusive(true);
    connect(insertDrop, &QAction::triggered, this, [this] { mixOnDrop = false; statusBar()->showMessage("Al soltar otra ventana: insertar en el punto.", 3000); });
    connect(mixDrop, &QAction::triggered, this, [this] { mixOnDrop = true; statusBar()->showMessage("Al soltar otra ventana: mezclar con el audio de destino.", 3000); });
    tools->addSeparator();
    tools->addAction(add(view, "Acercar", QKeySequence("Ctrl++"), [this] { if (current()) current()->wave->zoomBy(2); }, "zoom-in"));
    tools->addAction(add(view, "Alejar", QKeySequence("Ctrl+-"), [this] { if (current()) current()->wave->zoomBy(0.5); }, "zoom-out"));
    add(view, "Ver todo", QKeySequence("Ctrl+0"), [this] { if (current()) { current()->wave->zoom = 1; current()->wave->start = 0; current()->wave->update(); } });
    setupProcessingMenus(process, effects);
    process->addSeparator(); add(process, "Generar &tono…", {}, [this] { tone(); });
    auto *controls = new QToolBar("Transporte", this); addToolBar(Qt::BottomToolBarArea, controls); controls->setObjectName("transport"); controls->setIconSize(QSize(24,24));
    auto *leftSpace = new QWidget; leftSpace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); controls->addWidget(leftSpace);
    auto *recordAction = add(transport, "Grabar", QKeySequence("Ctrl+R"), [this] { startRecording(); }); recordAction->setObjectName("recordAudio");
    QPixmap recordDot(20,20); recordDot.fill(Qt::transparent); { QPainter p(&recordDot); p.setPen(Theme::RecordDot.darker(150)); p.setBrush(Theme::RecordDot); p.drawEllipse(4,4,13,13); } recordAction->setIcon(QIcon(recordDot)); controls->addAction(recordAction);
    controls->addAction(add(transport, "Ir al inicio", QKeySequence("Home"), [this] { stopPlayback(); if (current()) { current()->wave->anchor = current()->wave->cursor = 0; current()->wave->start = 0; refresh(); current()->wave->update(); } }, "start"));
    controls->addAction(add(transport, "Ir al final", QKeySequence("End"), [this] { if (auto *document = current(); document && document->ending) document->ending(); }, "end"));
    controls->addAction(add(transport, "Reproducir / detener", QKeySequence("Space"), [this] {
        auto *document = current();
        if (!document) return;
        // Cambiar el cursor o la selección prepara otro punto de arranque:
        // una sola pulsación reinicia allí, en vez de limitarse a detener.
        const bool sameSelection = document->wave->anchor == playback->selectionAnchor && document->wave->cursor == playback->selectionCursor;
        if (playbackIsRunning(document) && sameSelection) stopPlayback();
        else play();
    }, "play"));
    controls->addAction(add(transport, "Detener", QKeySequence("Esc"), [this] { stopDocument(current()); }, "stop"));
    followPlaybackAction = new QAction(Theme::icon("follow-playhead"), "Seguir reproducción", this);
    followPlaybackAction->setObjectName("followPlaybackAction");
    followPlaybackAction->setCheckable(true);
    followPlaybackAction->setToolTip("Seguir reproducción del MDI enfocado: fijar la línea roja en el centro y desplazar la forma de onda");
    controls->addAction(followPlaybackAction);
    connect(followPlaybackAction, &QAction::triggered, this, [this](bool follow) {
        if (auto *d = current()) d->followButton->setChecked(follow);
    });
    controls->addSeparator(); positionLabel = new QLabel; positionLabel->setObjectName("timeField"); positionLabel->setMinimumWidth(104); positionLabel->setFixedHeight(24); positionLabel->setAlignment(Qt::AlignCenter);
    controls->addWidget(positionLabel);
    auto *meterPanel = new QFrame;
    meterPanel->setObjectName("commonVuMeters");
    auto *meterLayout = new QHBoxLayout(meterPanel);
    meterLayout->setContentsMargins(8, 0, 0, 0);
    meterLayout->setSpacing(6);
    auto meterIcon = [](const QString &name, const QString &description) {
        auto *label = new QLabel;
        label->setPixmap(Theme::icon(name).pixmap(20, 20));
        label->setFixedSize(24, 24); label->setAlignment(Qt::AlignCenter);
        label->setAccessibleName(description); label->setToolTip(description);
        return label;
    };
    meterLayout->addWidget(meterIcon("microphone", "Entrada: micrófono / grabación"));
    inputVu = new VuMeter; inputVu->setObjectName("inputVu");
    inputVu->setFixedHeight(28); inputVu->setMinimumWidth(220); inputVu->setMaximumWidth(380);
    inputVu->setToolTip("Entrada L/R continua · escala de −60 a 0 dBFS");
    meterLayout->addWidget(inputVu);
    meterLayout->addSpacing(8);
    meterLayout->addWidget(meterIcon("speaker", "Salida: altavoz / reproducción"));
    outputVu = new VuMeter; outputVu->setObjectName("outputVu");
    outputVu->setFixedHeight(28); outputVu->setMinimumWidth(220); outputVu->setMaximumWidth(380);
    outputVu->setToolTip("Salida L/R de reproducción y previsualización · escala de −60 a 0 dBFS");
    meterLayout->addWidget(outputVu);
    tools->addSeparator();
    auto *meterSpace = new QWidget;
    meterSpace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tools->addWidget(meterSpace);
    auto *meterAction = tools->addWidget(meterPanel);
    auto *showMeters = view->addAction("Vúmetros");
    showMeters->setCheckable(true); showMeters->setChecked(true);
    connect(showMeters, &QAction::toggled, meterAction, &QAction::setVisible);
    auto *spacer = new QWidget; spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred); controls->addWidget(spacer);
    add(windows, "Cascada", {}, [this] { workspace->cascadeSubWindows(); layoutMinimized(); });
    add(windows, "Mosaico", {}, [this] { workspace->tileSubWindows(); layoutMinimized(); });
    add(windows, "Siguiente", QKeySequence::NextChild, [this] { workspace->activateNextSubWindow(); });
    add(windows, "Anterior", QKeySequence::PreviousChild, [this] { workspace->activatePreviousSubWindow(); });
    selectionLabel = new QLabel; formatLabel = new QLabel; statusBar()->addWidget(selectionLabel, 1); statusBar()->addPermanentWidget(formatLabel);
    add(options, "&Opciones generales…", QKeySequence("Ctrl+,"), [this] { showOptions(); });
    add(help, "Acerca de ForgeCraft", {}, [this] { auto *about = new AboutDialog(this); about->show(); about->raise(); });
    connect(workspace, &QMdiArea::subWindowActivated, this, [this] { refresh(); });
    connect(&timer, &QTimer::timeout, this, &MainWindow::tick); timer.setInterval(35);
    inputMonitorRetry.setSingleShot(true); inputMonitorRetry.setInterval(2000);
    connect(&inputMonitorRetry, &QTimer::timeout, this, &MainWindow::startInputMonitor);
    if (inputMonitoringEnabled) QTimer::singleShot(0, this, &MainWindow::startInputMonitor);
    setAcceptDrops(true); workspace->viewport()->setAcceptDrops(true); qApp->installEventFilter(this);
    this->showMaximized();
    refresh();
}
MainWindow::~MainWindow() {
    inputMonitoringEnabled = false; inputMonitorRetry.stop();
    if (recorder) recorder->stop();
    qApp->removeEventFilter(this);
    stopPlayback();
    livePreview.reset();
    disconnect(workspace, nullptr, this, nullptr);
    for (auto *window : workspace->subWindowList()) {
        auto *d = static_cast<Document *>(window->widget());
        disconnect(&d->undo, nullptr, this, nullptr);
        disconnect(d->wave, nullptr, this, nullptr);
    }
}
Document *MainWindow::current() const { auto *w = workspace->currentSubWindow(); return w ? static_cast<Document *>(w->widget()) : nullptr; }

// Nombre «Sin título N» con el primer número libre entre los documentos abiertos.
QString MainWindow::nextUntitledName() const {
    auto taken = [this](int number) {
        const QString candidate = QStringLiteral("Sin título %1").arg(number);
        for (auto *window : workspace->subWindowList())
            if (static_cast<Document *>(window->widget())->name == candidate) return true;
        return false;
    };
    int number = 1;
    while (taken(number)) ++number;
    return QStringLiteral("Sin título %1").arg(number);
}

// Menú contextual del área de trabajo con las mismas acciones que el menú Edición:
// comparten atajos y estado habilitado.
void MainWindow::showEditMenu(const QPoint &global) {
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setObjectName("editContextMenu");
    menu->addAction(undoAction);
    menu->addAction(redoAction);
    menu->addSeparator();
    menu->addAction(cutAction);
    menu->addAction(copyAction);
    menu->addAction(pasteAction);
    menu->addAction(deleteAction);
    menu->addAction(trimAction);
    menu->addSeparator();
    menu->addAction(selectAllAction);
    menu->popup(global);
}

// Bordes de redimensionado bajo un punto global. La franja de la barra de título
// queda excluida: ahí se arrastra el módulo, no se cambia el tamaño.
Qt::Edges MainWindow::moduleEdges(QMdiSubWindow *sub, const QPoint &global) const {
    if (!sub || !sub->isVisible() || sub->isMaximized()) return Qt::Edges();
    const QPoint local = sub->mapFromGlobal(global);
    constexpr int grab = 6, titleBar = 30;
    if (local.y() < titleBar) return Qt::Edges();
    Qt::Edges edges;
    if (local.x() <= grab) edges |= Qt::LeftEdge;
    if (local.x() >= sub->width() - grab) edges |= Qt::RightEdge;
    if (local.y() >= sub->height() - grab) edges |= Qt::BottomEdge;
    return edges;
}

static Qt::CursorShape resizeCursor(Qt::Edges edges) {
    const bool horizontal = edges & (Qt::LeftEdge | Qt::RightEdge), vertical = edges & Qt::BottomEdge;
    if (horizontal && vertical) return (edges & Qt::LeftEdge) ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor;
    if (horizontal) return Qt::SizeHorCursor;
    return vertical ? Qt::SizeVerCursor : Qt::ArrowCursor;
}

void MainWindow::updateResizeCursor(QMdiSubWindow *sub, Qt::Edges edges) {
    const Qt::CursorShape shape = resizeCursor(edges);
    if (cursorTarget && (cursorTarget != sub || shape == Qt::ArrowCursor)) { cursorTarget->unsetCursor(); cursorTarget = nullptr; }
    if (sub && shape != Qt::ArrowCursor && sub->cursor().shape() != shape) { sub->setCursor(shape); cursorTarget = sub; }
}

// Los módulos minimizados se alinean en el pie del área de trabajo, de izquierda
// a derecha, como los documentos minimizados de los editores clásicos.
void MainWindow::layoutMinimized() {
    const QRect area = workspace->viewport()->rect();
    int x = area.left() + 8;
    const int y = area.bottom() - Document::MinimizedHeight - 6;
    for (auto *window : workspace->subWindowList(QMdiArea::StackingOrder)) {
        auto *doc = dynamic_cast<Document *>(window->widget());
        if (!doc || !doc->isMinimized()) continue;
        window->resize(Document::MinimizedWidth, Document::MinimizedHeight);
        window->move(x, y);
        x += Document::MinimizedWidth + 4;
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    auto *widget = qobject_cast<QWidget *>(watched);
    // Redimensionado en curso: el módulo sigue al puntero hasta soltar el botón.
    if (resizeTarget) {
        if (event->type() == QEvent::MouseMove) {
            const QPoint delta = static_cast<QMouseEvent *>(event)->globalPosition().toPoint() - resizeOrigin;
            QRect frame = resizeFrame;
            if (resizeEdges & Qt::LeftEdge) frame.setLeft(qMin(frame.left() + delta.x(), frame.right() - 320));
            if (resizeEdges & Qt::RightEdge) frame.setRight(qMax(frame.right() + delta.x(), frame.left() + 320));
            if (resizeEdges & Qt::BottomEdge) frame.setBottom(qMax(frame.bottom() + delta.y(), frame.top() + 160));
            resizeTarget->setGeometry(frame);
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) { resizeTarget = nullptr; return true; }
    }
    // El fondo del área de trabajo consume el evento de contexto antes de que
    // llegue al viewport, así que el menú se atiende desde este filtro.
    if (widget && (widget == workspace || widget == workspace->viewport()) && event->type() == QEvent::ContextMenu) {
        showEditMenu(static_cast<QContextMenuEvent *>(event)->globalPos());
        return true;
    }
    // Al cambiar el tamaño del área de trabajo se recolocan los módulos minimizados.
    if (watched == workspace->viewport() && event->type() == QEvent::Resize) QTimer::singleShot(0, this, [this] { layoutMinimized(); });
    // Sin marco del sistema, el puntero indica qué borde se va a arrastrar.
    if (widget && event->type() == QEvent::MouseMove && !static_cast<QMouseEvent *>(event)->buttons()) {
        auto *sub = moduleWindow(widget);
        updateResizeCursor(sub, sub ? moduleEdges(sub, static_cast<QMouseEvent *>(event)->globalPosition().toPoint()) : Qt::Edges());
    }
    // Activar la subventana al pulsar en cualquier punto de su contenido mantiene
    // el foco y los atajos; si la pulsación cae en un borde, empieza el ajuste.
    if (widget && event->type() == QEvent::MouseButtonPress) {
        auto *sub = moduleWindow(widget);
        if (sub && sub->mdiArea() == workspace) {
            if (workspace->activeSubWindow() != sub) workspace->setActiveSubWindow(sub);
            auto *press = static_cast<QMouseEvent *>(event);
            const Qt::Edges edges = moduleEdges(sub, press->globalPosition().toPoint());
            if (edges && press->button() == Qt::LeftButton) {
                resizeTarget = sub; resizeEdges = edges;
                resizeOrigin = press->globalPosition().toPoint();
                resizeFrame = sub->geometry();
                return true;
            }
        }
    }
    if (widget && (widget == this || isAncestorOf(widget)) &&
        (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop)) {
        auto *drop = static_cast<QDropEvent *>(event);
        QStringList paths;
        for (const auto &url : drop->mimeData()->urls()) if (url.isLocalFile()) {
            QString path = url.toLocalFile();
            if (QFileInfo(path).isFile() && AudioFormats::canOpen(path)) paths.append(path);
        }
        if (!paths.isEmpty()) {
            drop->setDropAction(Qt::CopyAction); drop->accept();
            if (event->type() == QEvent::Drop) QTimer::singleShot(0, this, [this, paths] { for (const auto &path : paths) openFile(path, true); });
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
void MainWindow::addDocument(AudioData data, const QString &path, Waveform::PeakData prepared) {
    auto *d = new Document; d->data = std::move(data); d->path = path;
    d->wave->setPreparedPeaks(std::move(prepared));
    d->setAcceptDrops(true); d->wave->setAcceptDrops(true);
    d->name = path.isEmpty() ? nextUntitledName() : QFileInfo(path).fileName();
    d->contextMenu = [this](const QPoint &global) { showEditMenu(global); };
    d->moduleStateChanged = [this] { layoutMinimized(); };
    connect(d->wave, &Waveform::selectionChanged, this, &MainWindow::refresh);
    connect(&d->undo, &QUndoStack::indexChanged, this, &MainWindow::refresh);
    auto *window = new AudioWindow; window->setWidget(d); window->setAttribute(Qt::WA_DeleteOnClose); workspace->addSubWindow(window);
    // Módulo sin marco nativo: la barra de título y las pinzas las pone Document.
    window->setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    window->setWindowTitle(d->name); window->resize(qMax(640, workspace->width() - 170), 340);
    int offset = (workspace->subWindowList().size() - 1) % 6;
    window->move(45 + offset * 38, 65 + offset * 48);
    window->canClose = [this, d] {
        if (!confirmClose(d)) return false;
        if (playback->document == d) stopPlayback();
        disconnect(&d->undo, nullptr, this, nullptr); disconnect(d->wave, nullptr, this, nullptr); return true;
    };
    auto activate = [this, window] { workspace->setActiveSubWindow(window); };
    d->play = [this, activate] { activate(); play(); }; d->stop = [this, d] { stopDocument(d); };
    d->record = [this, activate] { activate(); startRecording(); };
    d->beginning = [this, d, activate] { activate(); stopPlayback(); d->wave->anchor = d->wave->cursor = d->wave->start = 0; d->wave->update(); refresh(); };
    d->ending = [this, d, activate] { activate(); stopPlayback(); d->wave->anchor = d->wave->cursor = d->data.frames(); d->wave->start = qMax(qint64(0), d->data.frames() - d->wave->visibleFrames()); d->wave->update(); refresh(); };
    // Arrastrar la selección fuera de la onda: sobre otro módulo se inserta en su
    // documento y sobre el área de trabajo abre un documento nuevo con el fragmento.
    // En ambos casos el documento de origen no cambia.
    d->copySelectionToDocument = [this, d](qint64 first, qint64 last, const QPoint &global) {
        if (first < 0 || last > d->data.frames() || last <= first) return;
        const AudioData fragment = d->data.fragment(first, last);
        Waveform *dropWave = nullptr;
        Document *target = documentAt(workspace->viewport(), global, dropWave);
        if (target == d) return;   // soltada sobre su propio módulo: no se hace nada
        if (target) {
            const qint64 dropFrame = dropWave ? dropWave->frameAt(dropWave->mapFromGlobal(global).x()) : -1;
            if (mixOnDrop) {
                MixDialog dialog(fragment.frames(), fragment.rate, this);
                // Escucha de la mezcla: se construye un tramo de la región afectada
                // con la mezcla aplicada y se reproduce sin tocar el documento.
                dialog.preview = [this, target, fragment, dropFrame](double sourceGain, double targetGain, qint64 tune, std::function<void()> finished) {
                    const AudioData &destination = target->data;
                    const bool empty = destination.frames() == 0;
                    const int channels = empty ? fragment.channels : destination.channels;
                    const int rate = empty ? fragment.rate : destination.rate;
                    AudioData region;
                    region.rate = rate; region.channels = channels;
                    region.bitDepth = empty ? fragment.bitDepth : destination.bitDepth;
                    region.floatFormat = empty ? fragment.floatFormat : destination.floatFormat;
                    const qint64 at = dropFrame >= 0 ? qBound(qint64(0), dropFrame, destination.frames()) : destination.frames();
                    const qint64 begin = at + tune;
                    const qint64 from = qMax(qint64(0), begin - rate);          // 1 s de contexto
                    qint64 to = qMin(qMax(destination.frames(), begin + fragment.frames() + rate / 2), from + 15 * qint64(rate));
                    if (to <= from) return false;
                    region.samples = destination.samples.mid(from * channels, (to - from) * channels);
                    region.samples.resize((to - from) * channels);
                    qint64 first = 0, last = 0;
                    AudioData blended = blendFragment(region, fragment, begin - from, sourceGain, targetGain, 0, first, last);
                    return playPreview(blended, 0, blended.frames(), finished);
                };
                dialog.stopPreview = [this] { stopPlayback(); };
                if (dialog.exec() != QDialog::Accepted) return;
                insertFragment(target, dropFrame, fragment, true, dialog.sourceGain(), dialog.targetGain(), dialog.tuneFrames());
            } else insertFragment(target, dropFrame, fragment);
            return;
        }
        const QString name = nextUntitledName();
        addDocument(fragment);
        auto *fresh = current();
        if (!fresh) return;
        if (auto *window = workspace->activeSubWindow()) {
            const QPoint drop = workspace->viewport()->mapFromGlobal(global) - QPoint(60, 20);
            window->move(qMax(0, drop.x()), qMax(0, drop.y()));
        }
        refresh();
        statusBar()->showMessage(QString("Selección copiada a «%1» (%2)").arg(fresh->name, timeText(last - first, fragment.rate)), 4000);
    };
    // Al arrastrar, la barra de estado muestra la duración del fragmento y la
    // ventana que hay debajo del puntero.
    d->wave->dragToggleMode = [this] {
        if (auto *action = findChild<QAction *>(mixOnDrop ? "dropInsert" : "dropMix")) action->trigger();
    };
    d->wave->dragStatus = [this, d](qint64 first, qint64 last, const QPoint &global) {
        Waveform *dropWave = nullptr;
        Document *target = documentAt(workspace->viewport(), global, dropWave);
        const QString length = timeText(last - first, d->data.rate);
        if (target && target != d) statusBar()->showMessage(QString("Selección de %1 · %2 en «%3» · clic derecho: cambiar modo").arg(length, mixOnDrop ? "mezclar" : "insertar", target->name), 5000);
        else if (target == d) statusBar()->showMessage(QString("Selección de %1 · suelta en otra ventana o en el área de trabajo").arg(length), 5000);
        else statusBar()->showMessage(QString("Selección de %1 · documento nuevo al soltar").arg(length), 5000);
        // Fantasma del fragmento en el documento que hay debajo del puntero.
        Waveform *previewTarget = (target && target != d) ? target->wave : nullptr;
        if (previewTarget != dragPreviewWave && dragPreviewWave) { dragPreviewWave->clearDropPreview(); dragPreviewWave = nullptr; }
        if (previewTarget) {
            const qint64 at = previewTarget->frameAt(previewTarget->mapFromGlobal(global).x());
            previewTarget->setDropPreview(d->data.samples.constData(), d->data.channels, first, last, at, mixOnDrop);
            dragPreviewWave = previewTarget;
        }
    };
    d->wave->dragFinished = [this] {
        if (dragPreviewWave) { dragPreviewWave->clearDropPreview(); dragPreviewWave = nullptr; }
    };
    d->wave->markerMenu = [this, d](int index, const QPoint &global) {
        if (index < 0 || index >= d->data.markers.size()) return;
        if (auto *window = qobject_cast<QMdiSubWindow *>(d->parentWidget())) workspace->setActiveSubWindow(window);
        QMenu menu(this);
        QAction *rename = menu.addAction("Renombrar marca…");
        QAction *remove = menu.addAction("Eliminar marca");
        QAction *chosen = menu.exec(global);
        if (chosen == rename) { if (d->wave->markerRename) d->wave->markerRename(index); }
        else if (chosen == remove) removeMarkerAt(d, index);
    };
    d->wave->markerRename = [this, d](int index) {
        if (index < 0 || index >= d->data.markers.size()) return;
        bool accepted = false;
        const QString name = QInputDialog::getText(this, "Renombrar marca", "Nombre de la marca:", QLineEdit::Normal, d->data.markers[index].name, &accepted);
        if (!accepted || name.trimmed().isEmpty()) return;
        d->data.markers[index].name = name.trimmed();
        d->wave->update();
    };
    window->show(); workspace->setActiveSubWindow(window); refresh();
}
void MainWindow::openFile(const QString &path, bool alwaysNew) {
    RawAudioOptions raw;
    if (AudioFormats::isRaw(path) && !chooseRawAudio(this, raw)) return;
    struct LoadedAudio { AudioData data; Waveform::PeakData peaks; QString error; bool ok = false; };
    auto percentage = std::make_shared<std::atomic_int>(0);
    auto canceled = std::make_shared<std::atomic_bool>(false);
    const QString name = QFileInfo(path).fileName();
    QProgressDialog progress("Leyendo y decodificando: " + name, "Cancelar", 0, 100, this);
    progress.setObjectName("audioLoadProgress"); progress.setWindowTitle("Cargando audio");
    progress.setMinimumDuration(0); progress.setWindowModality(Qt::ApplicationModal);
    progress.setAutoClose(false); progress.setAutoReset(false); progress.setMinimumWidth(410); progress.setValue(0);
    connect(&progress, &QProgressDialog::canceled, this, [canceled] { canceled->store(true); });
    QTimer progressTimer; progressTimer.setInterval(25);
    connect(&progressTimer, &QTimer::timeout, &progress, [&] {
        int value = percentage->load(); progress.setValue(value);
        progress.setLabelText((value < 85 ? "Leyendo y decodificando: " : "Preparando forma de onda: ") + name);
    });
    QFutureWatcher<LoadedAudio> watcher;
    connect(&watcher, &QFutureWatcher<LoadedAudio>::finished, &progress, &QDialog::accept);
    watcher.setFuture(QtConcurrent::run([path, raw, percentage, canceled] {
        LoadedAudio loaded;
        try {
            auto report = [percentage, canceled](int value) { percentage->store(value * 85 / 100); return !canceled->load(); };
            loaded.ok = AudioFormats::isRaw(path) ? AudioFormats::read(path, loaded.data, loaded.error, report, raw) : AudioData::read(path, loaded.data, loaded.error, report);
            if (loaded.ok && !canceled->load()) {
                loaded.peaks = Waveform::preparePeaks(loaded.data, [percentage, canceled](int value) { percentage->store(85 + value * 14 / 100); return !canceled->load(); });
                loaded.ok = !loaded.peaks.canceled;
            }
        } catch (const std::bad_alloc &) { loaded.ok = false; loaded.error = "No hay memoria suficiente para cargar este audio."; }
        return loaded;
    }));
    progressTimer.start();
    if (!watcher.isFinished() && progress.exec() != QDialog::Accepted) canceled->store(true);
    watcher.waitForFinished(); progressTimer.stop();
    if (canceled->load()) { statusBar()->showMessage("Carga cancelada: " + name, 4000); return; }
    auto loaded = watcher.result();
    if (!loaded.ok) { QMessageBox::warning(this, "Abrir audio", loaded.error); return; }
    auto *empty = current(); bool replace = !alwaysNew && empty && empty->path.isEmpty() && empty->data.frames() == 0 && empty->undo.isClean();
    auto *emptyWindow = workspace->currentSubWindow(); addDocument(std::move(loaded.data), path, std::move(loaded.peaks));
    if (replace) emptyWindow->close();
    progress.setValue(100);
}
void MainWindow::refresh() {
    auto *d = current(); undoAction->setEnabled(d && d->undo.canUndo()); redoAction->setEnabled(d && d->undo.canRedo());
    if (followPlaybackAction) {
        QSignalBlocker block(followPlaybackAction);
        followPlaybackAction->setEnabled(d != nullptr);
        followPlaybackAction->setChecked(d && d->wave->followPlayback);
    }
    if (auto *menu = findChild<QMenu *>("processMenu")) menu->setEnabled(d != nullptr);
    if (auto *menu = findChild<QMenu *>("effectsMenu")) menu->setEnabled(d && d->data.frames() > 0);
    for (auto *action : findChildren<QAction *>()) if (action->objectName().startsWith("dsp.")) action->setEnabled(d && (d->data.frames() > 0 || action->objectName() == "dsp.insert_silence"));
    for (auto *window : workspace->subWindowList()) {
        auto *doc = static_cast<Document *>(window->widget());
        window->setWindowTitle(doc->name + (doc->undo.isClean() ? "" : " *"));
        doc->setTitle(window->windowTitle());
        doc->setActive(window == workspace->activeSubWindow());
    }
    if (!d) { positionLabel->setText("00:00:00.000"); selectionLabel->clear(); formatLabel->clear(); setWindowTitle("ForgeCraft"); return; }
    positionLabel->setText(timeText(d->wave->playhead >= 0 ? d->wave->playhead : d->wave->cursor, d->data.rate));
    d->position->setText(timeText(d->wave->first(), d->data.rate));
    d->selectionEnd->setText(timeText(d->wave->last(), d->data.rate)); d->selectionLength->setText(timeText(d->wave->last() - d->wave->first(), d->data.rate));
    { QSignalBlocker block(d->scroll); qint64 available = qMax(qint64(0), d->data.frames() - d->wave->visibleFrames()); d->scroll->setRange(0, available ? 100000 : 0); d->scroll->setPageStep(int(100000.0 / qMax(1.0, d->wave->zoom))); d->scroll->setValue(available ? int(double(d->wave->start) / available * 100000) : 0); }
    selectionLabel->setText(QString("Selección: %1 → %2   |   Duración: %3").arg(timeText(d->wave->first(), d->data.rate), timeText(d->wave->last(), d->data.rate), timeText(d->wave->last() - d->wave->first(), d->data.rate)));
    const QString depth = d->data.floatFormat ? "32 float" : QString::number(d->data.bitDepth) + " bit";
    formatLabel->setText(QString("%1 Hz  |  %2  |  %3  |  %4").arg(d->data.rate).arg(depth).arg(d->data.channels == 2 ? "Estéreo" : "Mono").arg(timeText(d->data.frames(), d->data.rate)));
    setWindowTitle(d->name + (d->undo.isClean() ? "" : " *") + " — ForgeCraft");
}
void MainWindow::commit(Document *d, const AudioData &after, const QString &name, qint64 a, qint64 b) { if (playback->document == d) stopPlayback(); d->undo.push(new AudioEdit(d, after, name, a, b)); refresh(); }

void MainWindow::showOptions() {
    QVector<QPair<quint32, QString>> devices;
#ifdef Q_OS_WIN
    const UINT count = waveOutGetNumDevs();
    for (UINT index = 0; index < count; ++index) {
        WAVEOUTCAPSW caps{};
        if (waveOutGetDevCapsW(index, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            devices.append({quint32(index), QString::fromWCharArray(caps.szPname)});
    }
#endif
    QVector<QPair<quint32, QString>> inputs;
#ifdef Q_OS_WIN
    const UINT inputCount = waveInGetNumDevs();
    for (UINT index = 0; index < inputCount; ++index) {
        WAVEINCAPSW caps{};
        if (waveInGetDevCapsW(index, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            inputs.append({quint32(index), QString::fromWCharArray(caps.szPname)});
    }
#endif
    OptionsDialog dialog(devices, outputDevice, inputs, inputDevice, this);
    dialog.testDevice = [this](quint32 device) { return playTestTone(device); };
    if (dialog.exec() != QDialog::Accepted) return;
    outputDevice = dialog.outputDevice();
    inputDevice = dialog.inputDevice();
    if (!recording && recorder) recorder->stop();
    startInputMonitor();
    QSettings().setValue("audio/outputDevice", outputDevice);
    QSettings().setValue("audio/inputDevice", inputDevice);
    statusBar()->showMessage("Dispositivos de audio guardados.", 3000);
}
bool MainWindow::playTestTone(quint32 device) {
    stopPlayback();
    const quint32 previous = outputDevice;
    outputDevice = device;
    const AudioData tone = AudioData::tone(0.6, 440.0, 44100, 1);
    const bool started = playPreview(tone, 0, tone.frames(), [] {});
    outputDevice = previous;
    return started;
}
// Grabación directa: la captura se acumula en memoria y, al detener, se añade al
// final del documento enfocado como una operación de deshacer.
void MainWindow::ensureRecorder() {
    if (recorder) return;
    recorder = recordingSource ? recordingSource() : new Recorder(this);
    connect(recorder, &Recorder::updated, this, [this] {
        if (!inputVu) return;
        const auto &buffer = recorder->buffer;
        inputVu->setLevels(buffer.peaks[0], buffer.audio.channels == 1 ? buffer.peaks[0] : buffer.peaks[1]);
    });
    connect(recorder, &Recorder::failed, this, [this](const QString &error) {
        if (recording) { stopRecording(); statusBar()->showMessage(error, 5000); return; }
        if (inputVu) { inputVu->reset(); inputVu->setToolTip("Entrada no disponible: " + error); }
        if (inputMonitoringEnabled) inputMonitorRetry.start();
    });
    connect(recorder, &Recorder::limitReached, this, [this] {
        if (recording) stopRecording();
        else if (inputMonitoringEnabled) inputMonitorRetry.start();
    });
}
void MainWindow::startInputMonitor() {
    if (!inputMonitoringEnabled || recording) return;
    ensureRecorder();
    if (recorder->active()) return;
    inputMonitorRetry.stop();
    recorder->clear(44100, 2);
    QString error;
    if (!recorder->start(inputDevice, 44100, 2, false, error)
        && !recorder->start(inputDevice, 44100, 1, false, error)) {
        if (inputVu) { inputVu->reset(); inputVu->setToolTip("Entrada no disponible: " + error); }
        inputMonitorRetry.start();
        return;
    }
    if (inputVu) inputVu->setToolTip("Entrada L/R continua · escala de −60 a 0 dBFS");
}
void MainWindow::startRecording() {
    if (recording) { stopRecording(); return; }
    Document *target = current();
    if (!target) {
        AudioData empty; empty.rate = 44100; empty.channels = 2;
        addDocument(empty);
        target = current();
        if (target) target->name = "Grabación " + QString::number(++recordingNumber);
    }
    if (!target) return;
    const int rate = target->data.rate;
    const int channels = target->data.channels;
    ensureRecorder();
    inputMonitorRetry.stop();
    recorder->clear(rate, channels);
    QString error;
    if (!recorder->start(inputDevice, rate, channels, true, error)) {
        startInputMonitor();
        QMessageBox::warning(this, "Grabar", error.isEmpty() ? QString("No se pudo abrir la entrada de audio.") : error);
        return;
    }
    recording = true;
    recordTarget = target;
    recordBlinkTicks = 0;
    target->setRecordingOn(true);
    target->setRecordTime(0, rate);
    if (inputVu) inputVu->reset();
    timer.start();
    statusBar()->showMessage(QString("Grabando en «%1» …").arg(target->name), 2000);
}
void MainWindow::stopRecording() {
    if (!recording) return;
    recording = false;
    if (recorder) recorder->stop();
    AudioData take;
    if (recorder) take = recorder->buffer.audio;
    if (recorder) recorder->clear(take.rate > 0 ? take.rate : 44100, take.channels > 0 ? take.channels : 2);
    Document *target = recordTarget;
    recordTarget = nullptr;
    if (target) target->setRecordingOn(false);
    if (inputVu) inputVu->reset();
    startInputMonitor();
    if (!target || !take.frames()) { if (!playback->document) timer.stop(); refresh(); return; }
    const qint64 end = target->data.frames();
    auto result = Recording::merge(target->data, take, Recording::Insert, end, end);
    if (!result.error.isEmpty()) { if (!playback->document) timer.stop(); QMessageBox::warning(this, "Grabar", result.error); refresh(); return; }
    commit(target, result.audio, "Grabar audio", result.first, result.last);
    target->wave->anchor = result.first;
    target->wave->cursor = result.last;
    target->wave->start = qMax(qint64(0), result.last - target->wave->visibleFrames());
    target->wave->update();
    if (!playback->document) timer.stop();
    statusBar()->showMessage(QString("Grabación de %1 añadida a «%2»").arg(timeText(take.frames(), take.rate), target->name), 4000);
}
void MainWindow::addMarker() {
    auto *d = current(); if (!d) return;
    const qint64 frame = qBound(qint64(0), d->wave->cursor, d->data.frames());
    auto taken = [d](const QString &name) {
        for (const auto &marker : d->data.markers) if (marker.name == name) return true;
        return false;
    };
    int number = d->data.markers.size() + 1;
    QString name = QString("Marca %1").arg(number, 2, 10, QChar('0'));
    while (taken(name)) name = QString("Marca %1").arg(++number, 2, 10, QChar('0'));
    AudioData next = d->data;
    next.markers.append({frame, name});
    commit(d, next, "Insertar marca", frame, frame);
    d->wave->selectedMarker = next.markers.size() - 1;
    d->wave->update();
    statusBar()->showMessage(QString("%1 en %2").arg(name, timeText(frame, d->data.rate)), 4000);
}
void MainWindow::removeMarker() {
    auto *d = current(); if (!d || d->data.markers.isEmpty()) return;
    int best = -1; qint64 distance = 0;
    for (int index = 0; index < d->data.markers.size(); ++index) {
        const qint64 currentDistance = qAbs(d->data.markers[index].frame - d->wave->cursor);
        if (best < 0 || currentDistance < distance) { best = index; distance = currentDistance; }
    }
    removeMarkerAt(d, best);
}
void MainWindow::removeMarkerAt(Document *document, int index) {
    if (!document || index < 0 || index >= document->data.markers.size()) return;
    const QString name = document->data.markers[index].name;
    const qint64 frame = document->data.markers[index].frame;
    AudioData next = document->data;
    next.markers.remove(index);
    document->wave->selectedMarker = -1;
    commit(document, next, "Eliminar marca", frame, frame);
    document->wave->update();
    statusBar()->showMessage(QString("Marca «%1» eliminada").arg(name), 4000);
}
// Coloca un fragmento en otro documento, en la posición indicada (-1: inicio de
// la selección del destino). Con `mix` se suma al audio existente en lugar de
// insertarse, como el modo de mezcla de Sound Forge; en ambos casos es una
// operación deshacible del documento destino.
void MainWindow::insertFragment(Document *target, qint64 position, const AudioData &fragment, bool mix, double sourceGain, double targetGain, qint64 tune) {
    if (!target || fragment.samples.isEmpty()) return;
    AudioData next = target->data;
    if (next.frames() && (next.rate != fragment.rate || next.channels != fragment.channels)) {
        QMessageBox::information(this, mix ? "Mezclar selección" : "Insertar selección", "El documento de destino usa otra frecuencia o número de canales. Ajusta su formato con Resample o Channel Converter antes de soltar el audio.");
        return;
    }
    if (!next.frames()) { next.rate = fragment.rate; next.channels = fragment.channels; next.bitDepth = fragment.bitDepth; next.floatFormat = fragment.floatFormat; }
    const qint64 at = position >= 0 ? qBound(qint64(0), position, next.frames()) : target->wave->first();
    if (mix) {
        qint64 first = 0, last = 0;
        AudioData blended = blendFragment(next, fragment, at, sourceGain, targetGain, tune, first, last);
        if (last <= first) return;
        stopPlayback();
        commit(target, blended, "Mezclar selección", first, last);
        if (auto *window = qobject_cast<QMdiSubWindow *>(target->parentWidget())) workspace->setActiveSubWindow(window);
        refresh();
        statusBar()->showMessage(QString("Selección mezclada en «%1» (%2)").arg(target->name, timeText(fragment.frames(), fragment.rate)), 4000);
        return;
    }
    next.shiftMarkers(at, fragment.frames());
    next.samples = next.samples.first(at * next.channels) + fragment.samples + next.samples.mid(at * next.channels);
    for (const auto &marker : fragment.markers) next.markers.append({at + marker.frame, marker.name});
    stopPlayback();
    commit(target, next, "Insertar selección", at, at + fragment.frames());
    if (auto *window = qobject_cast<QMdiSubWindow *>(target->parentWidget())) workspace->setActiveSubWindow(window);
    refresh();
    statusBar()->showMessage(QString("Selección insertada en «%1» (%2)").arg(target->name, timeText(fragment.frames(), fragment.rate)), 4000);
}
void MainWindow::copy() {
    auto *d = current(); if (!d || d->wave->first() == d->wave->last()) return;
    clipboard = d->data.fragment(d->wave->first(), d->wave->last());
}
void MainWindow::paste() {
    auto *d = current(); if (!d || clipboard.samples.isEmpty()) return;
    if (d->data.frames() && (d->data.rate != clipboard.rate || d->data.channels != clipboard.channels)) { QMessageBox::information(this, "Pegar", "La frecuencia y los canales deben coincidir. Usa Resample o Channel Converter para ajustarlos antes de pegar."); return; }
    AudioData next = d->data; if (!next.frames()) { next.rate = clipboard.rate; next.channels = clipboard.channels; }
    qint64 a = d->wave->first(), b = d->wave->last();
    next.samples = next.samples.first(a * next.channels) + clipboard.samples + next.samples.mid(b * next.channels);
    next.shiftMarkers(a, clipboard.frames());
    for (const auto &marker : clipboard.markers) next.markers.append({a + marker.frame, marker.name});
    commit(d, next, "Pegar", a, a + clipboard.frames());
}
void MainWindow::edit(const QString &op, double gain) {
    auto *d = current(); if (!d || !d->data.frames()) return;
    qint64 a = d->wave->first(), b = d->wave->last(); bool selected = a != b;
    if (!selected && (op == "delete" || op == "trim")) return;
    if (!selected) { a = 0; b = d->data.frames(); }
    AudioData next = d->data;
    if (op == "delete") { next.samples.remove(a * next.channels, (b - a) * next.channels); next.removeMarkers(a, b); b = a; }
    else if (op == "trim") { next.samples = next.samples.mid(a * next.channels, (b - a) * next.channels); next.trimMarkers(a, b); b -= a; a = 0; }
    else next.process(a, b, op, gain);
    commit(d, next, op, a, b);
}
bool MainWindow::save(Document *d, bool saveAs) {
    QString path = d->path;
    const auto *originalFormat = AudioFormats::forPath(path);
    QString filter = AudioFormats::filter(originalFormat ? *originalFormat : AudioFormats::formats().first());
    if (path.isEmpty() || saveAs || !originalFormat) {
        QFileDialog dialog(this, "Guardar audio");
        dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setNameFilters(AudioFormats::saveFilters()); dialog.selectNameFilter(filter);
        dialog.setDefaultSuffix(originalFormat ? originalFormat->suffix : "wav");
        dialog.selectFile(path.isEmpty() ? d->name : path);
        connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog](const QString &selected) {
            if (const auto *format = AudioFormats::forFilter(selected)) {
                dialog.setDefaultSuffix(format->suffix);
                if (!dialog.selectedFiles().isEmpty() && AudioFormats::forPath(dialog.selectedFiles().first()))
                    dialog.selectFile(AudioFormats::pathForFormat(dialog.selectedFiles().first(), *format));
            }
        });
        if (dialog.exec() != QDialog::Accepted) return false;
        const QString chosen = dialog.selectedFiles().value(0);
        const auto *selected = AudioFormats::forFilter(dialog.selectedNameFilter());
        if (chosen.isEmpty() || !selected) return false;
        path = AudioFormats::pathForFormat(chosen, *selected);
        if (path != chosen && QFileInfo::exists(path) && QMessageBox::question(this, "Reemplazar archivo", "El archivo «" + QFileInfo(path).fileName() + "» ya existe. ¿Quieres reemplazarlo?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;
    }
    if (path.isEmpty()) return false;
    const auto *format = AudioFormats::forPath(path);
    if (!format) return false;
    AudioExportOptions exportOptions;
    if (format->id != "wav" && format->id != "mp3" && !chooseAudioExport(this, *format, d->data, exportOptions)) return false;
    int bitrate = d->mp3Bitrate;
    if (path.endsWith(".mp3", Qt::CaseInsensitive)) {
        const QStringList qualities = {"128 kbps", "192 kbps", "256 kbps", "320 kbps"};
        bool ok; QString quality = QInputDialog::getItem(this, "Guardar MP3", "Calidad MP3 (bitrate constante):", qualities, qMax(0, int(qualities.indexOf(QString::number(bitrate) + " kbps"))), false, &ok);
        if (!ok) return false;
        bitrate = quality.section(' ', 0, 0).toInt();
        if (d->data.rate != 32000 && d->data.rate != 44100 && d->data.rate != 48000)
            if (QMessageBox::information(this, "Frecuencia MP3", "El MP3 se guardará a 44.100 Hz. El audio de este documento conservará su frecuencia original.", QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok) return false;
    }
    struct Saved { bool ok = false; QString error; };
    auto percentage = std::make_shared<std::atomic_int>(0);
    auto canceled = std::make_shared<std::atomic_bool>(false);
    QProgressDialog progress("Codificando: " + QFileInfo(path).fileName(), "Cancelar", 0, 100, this);
    progress.setWindowTitle("Guardando audio"); progress.setMinimumDuration(0);
    progress.setWindowModality(Qt::ApplicationModal); progress.setAutoClose(false); progress.setAutoReset(false);
    connect(&progress, &QProgressDialog::canceled, this, [canceled] { canceled->store(true); });
    QTimer timer; timer.setInterval(30);
    connect(&timer, &QTimer::timeout, &progress, [&] { progress.setValue(qMin(99, percentage->load())); });
    QFutureWatcher<Saved> watcher;
    connect(&watcher, &QFutureWatcher<Saved>::finished, &progress, &QDialog::accept);
    const bool extendedFormat = format->id != "wav" && format->id != "mp3";
    watcher.setFuture(QtConcurrent::run([audio = d->data, path, bitrate, exportOptions, percentage, canceled, extendedFormat] {
        Saved saved;
        auto report = [percentage, canceled](int value) { percentage->store(value); return !canceled->load(); };
        try { saved.ok = extendedFormat ? AudioFormats::write(audio, path, exportOptions, saved.error, report) : audio.write(path, saved.error, bitrate, report); }
        catch (const std::bad_alloc &) { saved.error = "No hay memoria suficiente para guardar este audio."; }
        return saved;
    }));
    timer.start(); if (!watcher.isFinished()) progress.exec();
    watcher.waitForFinished(); timer.stop();
    const auto result = watcher.result();
    if (!result.ok) { if (!canceled->load()) QMessageBox::warning(this, "Guardar", result.error); return false; }
    d->mp3Bitrate = bitrate;
    d->path = path; d->name = QFileInfo(path).fileName(); d->undo.setClean(); refresh(); statusBar()->showMessage("Guardado: " + path, 5000); return true;
}
bool MainWindow::confirmClose(Document *d) {
    if (d->undo.isClean()) return true;
    auto result = QMessageBox::question(this, "Cambios sin guardar", "¿Guardar los cambios de «" + d->name + "»?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return result == QMessageBox::Discard || (result == QMessageBox::Save && save(d));
}
void MainWindow::closeEvent(QCloseEvent *event) { if (recording) stopRecording(); for (auto *window : workspace->subWindowList()) if (!confirmClose(static_cast<Document *>(window->widget()))) { event->ignore(); return; } inputMonitoringEnabled = false; inputMonitorRetry.stop(); if (recorder) recorder->stop(); stop(); event->accept(); }
void MainWindow::tone() {
    bool ok; double frequency = QInputDialog::getDouble(this, "Generar tono", "Frecuencia (Hz):", 440, 20, 20000, 1, &ok); if (!ok) return;
    double seconds = QInputDialog::getDouble(this, "Generar tono", "Duración (segundos):", 5, 0.01, 600, 2, &ok); if (!ok) return;
    addDocument(AudioData{}); auto *d = current(); d->name = "Tono " + QString::number(frequency) + " Hz";
    auto data = AudioData::tone(seconds, frequency); commit(d, data, "Generar tono", 0, data.frames());
}
bool MainWindow::playbackIsRunning(Document *document) const {
#ifdef Q_OS_WIN
    return document && playback->document == document && playback->handle && !(playback->header.dwFlags & WHDR_DONE);
#else
    Q_UNUSED(document) return false;
#endif
}
void MainWindow::play(bool loop) { playDocument(current(), loop); }
void MainWindow::playDocument(Document *document, bool loop) {
    if (!document || !document->data.frames()) return;
    qint64 first = document->wave->first(), last = document->wave->last();
    if (first == last) { first = document->wave->cursor; last = document->data.frames(); if (first == last) first = 0; }
    QString error;
    if (!startPlayback(document->data, first, last, document, loop, error)) QMessageBox::warning(this, "Reproducción", error);
}

bool MainWindow::startPlayback(const AudioData &audio, qint64 first, qint64 last, Document *document, bool loop, QString &error, bool forcePcm16) {
    error.clear();
    if (audio.channels < 1 || audio.channels > 2 || audio.rate < 8000) { error = "Formato de reproducción no válido."; return false; }
    first = std::clamp(first, qint64(0), audio.frames()); last = std::clamp(last, first, audio.frames());
    if (last <= first) { error = "No hay audio para reproducir."; return false; }
    // Copia compartida: no duplica las muestras y permite soltar/editar la fuente
    // sin invalidar el búfer que todavía está leyendo el dispositivo.
    AudioData snapshot = audio;
    stopPlayback();
#ifdef Q_OS_WIN
    playback->source = std::move(snapshot);
    playback->floatOutput = !forcePcm16;
    auto open = [&] {
        WAVEFORMATEX format{};
        format.wFormatTag = playback->floatOutput ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
        format.nChannels = WORD(audio.channels); format.nSamplesPerSec = DWORD(audio.rate);
        format.wBitsPerSample = playback->floatOutput ? 32 : 16;
        format.nBlockAlign = WORD(audio.channels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        playback->bytesPerFrame = format.nBlockAlign;
        return waveOutOpen(&playback->handle, UINT(outputDevice), &format, 0, 0, CALLBACK_NULL);
    };
    MMRESULT result = open();
    if (result == WAVERR_BADFORMAT && playback->floatOutput) {
        playback->handle = nullptr; playback->floatOutput = false; result = open();
    }
    if (result != MMSYSERR_NOERROR) { playback->handle = nullptr; stopPlayback(); error = "No se pudo abrir el dispositivo de audio (código " + QString::number(result) + ")."; return false; }
    const qint64 byteCount = (last - first) * playback->bytesPerFrame;
    if (quint64(byteCount) > std::numeric_limits<DWORD>::max()) { stopPlayback(); error = "El tramo es demasiado largo para la salida de audio."; return false; }
    playback->header = {};
    if (playback->floatOutput) {
        // El motor trabaja en float32: waveOut puede leerlo directamente, sin
        // recorrer y convertir una canción completa antes de arrancar Play.
        playback->header.lpData = const_cast<char *>(reinterpret_cast<const char *>(playback->source.samples.constData() + first * audio.channels));
    } else {
        try { playback->bytes.resize(byteCount); }
        catch (const std::bad_alloc &) { stopPlayback(); error = "No hay memoria suficiente para reproducir este audio."; return false; }
        const float *input = playback->source.samples.constData() + first * audio.channels;
        char *output = playback->bytes.data();
        for (qint64 i = 0; i < (last - first) * audio.channels; ++i)
            qToLittleEndian(quint16(qint16(std::lround(std::clamp(double(input[i]), -1.0, 1.0) * 32767))), output + i * 2);
        playback->header.lpData = playback->bytes.data();
    }
    playback->header.dwBufferLength = DWORD(byteCount);
    result = waveOutPrepareHeader(playback->handle, &playback->header, sizeof(WAVEHDR));
    if (result == MMSYSERR_NOERROR) result = waveOutWrite(playback->handle, &playback->header, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) { stopPlayback(); error = "No se pudo enviar el audio al dispositivo (código " + QString::number(result) + ")."; return false; }
    playback->document = document; playback->begin = first; playback->end = last; playback->loop = loop;
    playback->selectionAnchor = document ? document->wave->anchor : 0;
    playback->selectionCursor = document ? document->wave->cursor : 0;
    timer.start(); return true;
#else
    Q_UNUSED(document) Q_UNUSED(loop) Q_UNUSED(forcePcm16)
    error = "La reproducción requiere Windows."; return false;
#endif
}

bool MainWindow::playPreview(const AudioData &audio, qint64 first, qint64 last, std::function<void()> finished) {
    QString error;
    if (!startPlayback(audio, first, last, nullptr, false, error)) {
        if (!audio.samples.isEmpty()) QMessageBox::warning(this, "Previsualización", error);
        return false;
    }
    previewEnded = std::move(finished); return true;
}
// Los controles de un módulo solo detienen el transporte de ese documento,
// aunque otro módulo esté enfocado, reproduciendo o grabando.
void MainWindow::stopDocument(Document *document) {
    if (!document) return;
    if (recording && recordTarget == document) stopRecording();
    if (playback->document == document) stopPlayback();
}
// Parada completa interna para el cierre de la aplicación. Los botones y
// atajos del transporte inferior se dirigen al documento enfocado.
void MainWindow::stop() {
    if (recording) stopRecording();
    stopPlayback();
}
void MainWindow::stopPlayback() {
    if (livePreview) livePreview->stop();
    // Si hay una grabación en curso, el temporizador sigue vivo para su vúmetro.
    if (!recording) timer.stop();
    previewEnded = nullptr;
#ifdef Q_OS_WIN
    if (playback->handle) { waveOutReset(playback->handle); waveOutUnprepareHeader(playback->handle, &playback->header, sizeof(WAVEHDR)); waveOutClose(playback->handle); playback->handle = nullptr; }
#endif
    if (auto *playing = playback->document) {
        playing->wave->playhead = -1; playing->wave->update();
        playback->document = nullptr;
    }
    if (outputVu) outputVu->reset();
    if (previewVu) previewVu->reset();
    playback->bytes.clear();
    playback->source = AudioData{}; playback->bytesPerFrame = 0;
#ifdef Q_OS_WIN
    playback->header = {};
#endif
    refresh();
}
void MainWindow::tick() {
#ifdef Q_OS_WIN
    // Grabación y reproducción son independientes: se puede grabar en un módulo
    // mientras suena otro.
    if (recording) {
        const qint64 frames = recorder ? recorder->buffer.audio.frames() : 0;
        const int rate = recorder && recorder->buffer.audio.rate > 0 ? recorder->buffer.audio.rate : 44100;
        if (recordTarget) {
            recordTarget->setRecordTime(frames, rate);
            // Parpadeo del botón de grabación en un rojo más oscuro (~350 ms).
            if (++recordBlinkTicks % 10 == 0) recordTarget->toggleRecordBlink();
        }
        statusBar()->showMessage(QString("Grabando en «%1» · %2").arg(recordTarget ? recordTarget->name : QString(), timeText(frames, rate)));
    }
    if (outputVu && playback->handle && !(playback->header.dwFlags & WHDR_DONE)) {
        MMTIME position{}; position.wType = TIME_SAMPLES;
        if (waveOutGetPosition(playback->handle, &position, sizeof(position)) == MMSYSERR_NOERROR) {
            const auto &audio = playback->source;
            const qint64 offset = position.wType == TIME_SAMPLES ? position.u.sample :
                position.wType == TIME_BYTES ? position.u.cb / qMax(1, playback->bytesPerFrame) :
                qint64(position.u.ms) * audio.rate / 1000;
            const qint64 frame = std::clamp(playback->begin + offset, qint64(0), audio.frames());
            double peaks[2] = {0, 0};
            for (qint64 f = frame; f < qMin(audio.frames(), frame + 1024); ++f)
                for (int c = 0; c < audio.channels; ++c)
                    peaks[c] = std::max(peaks[c], double(std::abs(audio.samples[f * audio.channels + c])));
            outputVu->setLevels(peaks[0], audio.channels == 1 ? peaks[0] : peaks[1]);
            if (previewVu) previewVu->setLevels(peaks[0], audio.channels == 1 ? peaks[0] : peaks[1]);
        }
    }
    auto *d = playback->document;
    if (!d) {
        // Las previsualizaciones también alimentan el vúmetro de salida.
        if (playback->handle && (playback->header.dwFlags & WHDR_DONE)) {
            auto callback = previewEnded;
            stopPlayback();
            if (callback) callback();
        }
        return;
    }
    if (playback->header.dwFlags & WHDR_DONE) { bool loop = playback->loop; stopPlayback(); if (loop) playDocument(d, true); return; }
    MMTIME t{}; t.wType = TIME_SAMPLES;
    if (waveOutGetPosition(playback->handle, &t, sizeof(t)) != MMSYSERR_NOERROR) return;
    qint64 offset = t.wType == TIME_SAMPLES ? t.u.sample : t.wType == TIME_BYTES ? t.u.cb / qMax(1, playback->bytesPerFrame) : qint64(t.u.ms) * d->data.rate / 1000;
    qint64 f = qMin(playback->begin + offset, playback->end); d->wave->setPlayhead(f);
    if (!recording) refresh();
#endif
}

int playbackTests() {
    int failures = 0;
    auto check = [&](bool ok, const char *message) { std::printf("%s playback %s\n", ok ? "PASS" : "FAIL", message); std::fflush(stdout); if (!ok) ++failures; };
#ifdef Q_OS_WIN
    if (!waveOutGetNumDevs()) { std::printf("SKIP playback native tests: no output device\n"); return 0; }
    MainWindow window({}, false); window.show(); window.outputDevice = WAVE_MAPPER;
    window.addDocument(AudioData{});
    auto *first = window.current();
    first->data = AudioData{}; first->data.samples.fill(0, 4410 * 2); first->wave->invalidateCache();
    const float *original = first->data.samples.constData(); QString error;
    if (!window.startPlayback(first->data, 0, first->data.frames(), first, false, error)) {
        std::printf("SKIP playback native tests: %s\n", error.toUtf8().constData()); return 0;
    }
    window.timer.stop();
    check(window.playbackIsRunning(first), "first start queues silent audio");
    window.tick();
    check(first->data.samples.constData() == original && window.playback->source.samples.constData() == original, "start and meter updates keep source shared without detaching audio");
    auto waitForEnd = [&] {
        QElapsedTimer timeout; timeout.start();
        while (!(window.playback->header.dwFlags & WHDR_DONE) && timeout.elapsed() < 3000) { QEventLoop loop; QTimer::singleShot(10, &loop, &QEventLoop::quit); loop.exec(); }
        return bool(window.playback->header.dwFlags & WHDR_DONE);
    };
    check(waitForEnd(), "native driver reports buffer completion");
    check(window.playback->document == first && !window.playbackIsRunning(first), "finished buffer is not mistaken for active playback before timer cleanup");
    for (auto *action : window.findChildren<QAction *>()) if (action->text() == "Reproducir / detener") { action->trigger(); break; }
    check(window.playbackIsRunning(first), "one Play press restarts after natural end with delayed cleanup");
    window.stopPlayback();
    for (int attempt = 0; attempt < 3; ++attempt) {
        bool started = window.startPlayback(first->data, 0, first->data.frames(), first, false, error);
        check(started && window.playbackIsRunning(first), "repeat first-click start succeeds"); window.stopPlayback();
    }
    bool fallback = window.startPlayback(first->data, 0, first->data.frames(), first, false, error, true);
    check(fallback && !window.playback->floatOutput && window.playback->bytes.size() == first->data.samples.size() * 2, "PCM16 compatibility path queues the correct byte count"); window.stopPlayback();
    window.addDocument(AudioData{}); auto *secondWindow = window.workspace->currentSubWindow();
    auto *second = window.current();
    window.startPlayback(first->data, 0, first->data.frames(), first, false, error);
    for (auto *action : window.findChildren<QAction *>()) if (action->text() == "Reproducir / detener") { action->trigger(); break; }
    check(window.playbackIsRunning(first) && second->data.frames() == 0, "Play on empty focused MDI preserves other playback");
    for (auto *action : window.findChildren<QAction *>()) if (action->text() == "Detener") { action->trigger(); break; }
    check(window.playbackIsRunning(first), "Stop on another focused MDI preserves playback"); window.stopPlayback();
    window.startPlayback(first->data, 0, first->data.frames(), first, true, error); window.timer.stop();
    check(waitForEnd(), "loop fixture reaches its end"); window.workspace->setActiveSubWindow(secondWindow); window.tick();
    check(window.playback->document == first && window.playbackIsRunning(first) && window.current() == second, "loop restarts its owner after focus changes"); window.stopPlayback();
    int callbacks = 0;
    window.recording = true;
    window.startPlayback(first->data, 0, first->data.frames(), nullptr, false, error);
    window.previewEnded = [&] { ++callbacks; }; window.timer.stop();
    check(waitForEnd(), "preview fixture reaches completion"); window.tick(); window.tick();
    check(callbacks == 1 && !window.playback->handle, "preview completion is delivered once while recording");
    window.recording = false; window.stopPlayback();
    AudioData longAudio; longAudio.samples.fill(0, 44100 * 2 * 120);
    QElapsedTimer startup; startup.start(); bool started = window.startPlayback(longAudio, 0, longAudio.frames(), first, false, error);
    std::printf("BENCH playback 120s startup_ms=%.2f format=%s (silent output probe)\n", startup.nsecsElapsed() / 1000000., window.playback->floatOutput ? "float32" : "PCM16");
    if (started && window.playback->floatOutput) check(window.playback->bytes.isEmpty() && window.playback->header.lpData == reinterpret_cast<const char *>(longAudio.samples.constData()), "long audio starts without a full-track PCM conversion or copy");
    else check(started, "long audio can start on compatibility output");
    window.stopPlayback();
    // Cerrar un MDI ajeno no debe reiniciar ni detener el dispositivo de salida.
    first->data.samples.fill(0, 44100 * 2 * 3); first->wave->invalidateCache();
    const bool closeStarted = window.startPlayback(first->data, 0, first->data.frames(), first, false, error);
    window.timer.stop();
    const auto playingHandle = window.playback->handle;
    QPointer<Document> closedSecond = second;
    window.workspace->setActiveSubWindow(secondWindow);
    const bool closedFocused = secondWindow->close();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(closeStarted && closedFocused && closedSecond.isNull() && window.playback->handle == playingHandle
        && window.playbackIsRunning(first), "closing another focused MDI preserves the playing document and output device");
    window.addDocument(AudioData{});
    auto *thirdWindow = window.workspace->currentSubWindow();
    auto *firstWindow = qobject_cast<QMdiSubWindow *>(first->parentWidget());
    window.workspace->setActiveSubWindow(firstWindow);
    const bool closedBackground = thirdWindow->close();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(closedBackground && window.playback->handle == playingHandle && window.playbackIsRunning(first),
        "closing another background MDI preserves playback");
    QPointer<Document> closedOwner = first;
    const bool closedPlaying = firstWindow->close();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(closedPlaying && closedOwner.isNull() && !window.playback->handle && !window.playback->document,
        "closing the playing MDI stops output and releases its document");
    window.close();
#else
    std::printf("SKIP playback native tests: Windows required\n");
#endif
    return failures;
}
