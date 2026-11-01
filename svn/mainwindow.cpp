#include "mainwindow.h"
#include "recorddialog.h"
#include "theme.h"
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QStyle>
#include <QUndoCommand>
#include <QProgressBar>
#include <QPainter>
#include <QDockWidget>
#include <QToolButton>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

struct MainWindow::Playback {
#ifdef Q_OS_WIN
    HWAVEOUT handle = nullptr;
    WAVEHDR header{};
#endif
    QByteArray bytes;
    Document *document = nullptr;
    qint64 begin = 0, end = 0;
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

void LevelMeter::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Theme::Window);
    QFont label = font(); label.setPixelSize(Theme::MeterTextPx); p.setFont(label);
    const int top = Theme::MeterTop, bottom = height() - Theme::MeterBottom;
    const int last = qMax(0, height() - 1);
    p.setPen(Theme::Border); p.drawLine(0, last, width() - 1, last);
    const int span = Theme::MeterBarWidth * 2 + (Theme::MeterGap - Theme::MeterBarWidth);
    const int startX = qMax(4, (width() - span) / 2);
    for (int c = 0; c < 2; ++c) {
        const int x = startX + c * Theme::MeterGap;
        const QRect bar(x, top, Theme::MeterBarWidth, qMax(1, bottom - top));
        const double db = levels[c] > 0 ? 20 * std::log10(levels[c]) : -60;
        p.setPen(Theme::TextDim);
        p.drawText(QRect(x - 8, 2, 33, top - 6), Qt::AlignCenter, levels[c] > 0 ? QString::number(db, 'f', 1) : "−Inf.");
        p.fillRect(bar, Theme::Panel);
        p.setPen(Theme::BorderMid); p.drawRect(bar);
        const QRect inside = bar.adjusted(1, 1, -1, -1);
        const int filled = int(inside.height() * std::clamp((db + 60) / 60, 0.0, 1.0));
        if (filled > 0) p.fillRect(QRect(inside.left(), inside.bottom() - filled + 1, inside.width(), filled), Theme::Accent);
        // Marca de pico de 2 px: retenida en setLevels, sin temporizador propio.
        const double peakDb = peaks[c] > 0 ? 20 * std::log10(peaks[c]) : -60;
        const double peakFraction = std::clamp((peakDb + 60) / 60, 0.0, 1.0);
        if (peakFraction > 0.001) {
            const int y = inside.bottom() - int(inside.height() * peakFraction) + 1;
            p.fillRect(QRect(inside.left(), std::clamp(y, inside.top(), inside.bottom() - 1), inside.width(), 2), Theme::Text);
        }
    }
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
    wave->setToolTip("Arrastra para seleccionar. Arrastra un extremo de la selección para ajustarla. Arrastra desde dentro de la selección y suéltala fuera de la onda para abrirla como documento nuevo o insertarla en otro módulo.");
    wave->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(wave, &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
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
    auto *recordControl = new QToolButton; recordControl->setFixedSize(30, 28); recordControl->setToolTip("Grabar…");
    QPixmap dot(16,16); dot.fill(Qt::transparent); { QPainter p(&dot); p.setPen(Theme::RecordDot.darker(150)); p.setBrush(Theme::RecordDot); p.drawEllipse(3,3,10,10); } recordControl->setIcon(QIcon(dot)); row->addWidget(recordControl);
    connect(recordControl, &QToolButton::clicked, this, [this] { if (record) record(); });
    button(row, "start", "Ir al inicio", [this] { if (beginning) beginning(); });
    button(row, "end", "Ir al final", [this] { if (ending) ending(); });
    button(row, "stop", "Detener", [this] { if (stop) stop(); });
    button(row, "play", "Reproducir", [this] { if (play) play(); });
    row->addStretch();
    position = new QLabel("00:00:00.000"); selectionEnd = new QLabel("00:00:00.000"); selectionLength = new QLabel("00:00:00.000");
    position->setToolTip("Inicio de selección"); selectionEnd->setToolTip("Final de selección"); selectionLength->setToolTip("Duración de selección");
    for (auto *field : {position, selectionEnd, selectionLength}) { field->setObjectName("timeField"); field->setMinimumWidth(92); field->setAlignment(Qt::AlignCenter); row->addWidget(field); }
    grip = new ModuleGrip(this); grip->raise();
    wave->dragOut = [this](qint64 first, qint64 last, const QPoint &global) {
        if (copySelectionToDocument) copySelectionToDocument(first, last, global);
    };
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

MainWindow::MainWindow(std::function<Recorder *()> source) : playback(new Playback), recordingSource(std::move(source)) {
    setWindowTitle("ForgeCraft"); resize(1500, 850);
    workspace = new QMdiArea(this); workspace->setObjectName("workspace"); setCentralWidget(workspace);
    auto *file = menuBar()->addMenu("&Archivo");
    auto *editing = menuBar()->addMenu("&Edición");
    auto *view = menuBar()->addMenu("&Ver");
    auto *process = menuBar()->addMenu("&Proceso");
    auto *effects = menuBar()->addMenu("&Efectos");
    auto *transport = menuBar()->addMenu("&Transporte");
    auto *windows = menuBar()->addMenu("&Ventana");
    auto *help = menuBar()->addMenu("A&yuda");
    auto *tools = addToolBar("Archivo y edición"); tools->setObjectName("mainTools"); tools->setIconSize(QSize(18,18)); tools->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto add = [this](QMenu *menu, const QString &text, const QKeySequence &key, auto fn, const QString &icon = QString()) {
        QAction *a = menu->addAction(text); a->setShortcut(key); if (!icon.isEmpty()) a->setIcon(Theme::icon(icon)); connect(a, &QAction::triggered, this, fn); return a;
    };
    tools->addAction(add(file, "&Nuevo", QKeySequence::New, [this] { addDocument(AudioData{}); }, "new"));
    tools->addAction(add(file, "&Abrir audio…", QKeySequence::Open, [this] { QString p = QFileDialog::getOpenFileName(this, "Abrir audio", {}, "Audio WAV y MP3 (*.wav *.mp3);;Audio WAV (*.wav);;Audio MP3 (*.mp3);;Todos los archivos (*)", nullptr, QFileDialog::DontUseNativeDialog); if (!p.isEmpty()) openFile(p); }, "open"));
    tools->addAction(add(file, "&Guardar", QKeySequence::Save, [this] { if (current()) save(current()); }, "save"));
    add(file, "Guardar &como…", QKeySequence::SaveAs, [this] { if (current()) save(current(), true); }, "save-as");
    file->addSeparator(); add(file, "&Cerrar documento", QKeySequence::Close, [this] { workspace->closeActiveSubWindow(); });
    add(file, "&Salir", QKeySequence::Quit, [this] { close(); });
    tools->addSeparator();
    undoAction = add(editing, "&Deshacer", QKeySequence::Undo, [this] { stop(); if (current()) current()->undo.undo(); }, "undo"); tools->addAction(undoAction);
    redoAction = add(editing, "&Rehacer", QKeySequence::Redo, [this] { stop(); if (current()) current()->undo.redo(); }, "redo"); tools->addAction(redoAction);
    editing->addSeparator();
    cutAction = add(editing, "Cor&tar", QKeySequence::Cut, [this] { copy(); edit("delete"); }, "cut"); tools->addAction(cutAction);
    copyAction = add(editing, "&Copiar", QKeySequence::Copy, [this] { copy(); }, "copy"); tools->addAction(copyAction);
    pasteAction = add(editing, "&Pegar", QKeySequence::Paste, [this] { paste(); }, "paste"); tools->addAction(pasteAction);
    deleteAction = add(editing, "&Eliminar selección", QKeySequence::Delete, [this] { edit("delete"); });
    trimAction = add(editing, "&Recortar a selección", QKeySequence("Ctrl+T"), [this] { edit("trim"); });
    selectAllAction = add(editing, "Seleccionar &todo", QKeySequence::SelectAll, [this] { if (auto *d = current()) { d->wave->anchor = 0; d->wave->cursor = d->data.frames(); d->wave->update(); refresh(); } });
    tools->addSeparator();
    tools->addAction(add(view, "Acercar", QKeySequence("Ctrl++"), [this] { if (current()) current()->wave->zoomBy(2); }, "zoom-in"));
    tools->addAction(add(view, "Alejar", QKeySequence("Ctrl+-"), [this] { if (current()) current()->wave->zoomBy(0.5); }, "zoom-out"));
    add(view, "Ver todo", QKeySequence("Ctrl+0"), [this] { if (current()) { current()->wave->zoom = 1; current()->wave->start = 0; current()->wave->update(); } });
    setupProcessingMenus(process, effects);
    process->addSeparator(); add(process, "Generar &tono…", {}, [this] { tone(); });
    auto *controls = new QToolBar("Transporte", this); addToolBar(Qt::BottomToolBarArea, controls); controls->setObjectName("transport"); controls->setIconSize(QSize(24,24));
    auto *leftSpace = new QWidget; leftSpace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); controls->addWidget(leftSpace);
    auto *recordAction = add(transport, "Grabar…", QKeySequence("Ctrl+R"), [this] { recordAudio(); }); recordAction->setObjectName("recordAudio");
    QPixmap recordDot(20,20); recordDot.fill(Qt::transparent); { QPainter p(&recordDot); p.setPen(Theme::RecordDot.darker(150)); p.setBrush(Theme::RecordDot); p.drawEllipse(4,4,13,13); } recordAction->setIcon(QIcon(recordDot)); controls->addAction(recordAction);
    controls->addAction(add(transport, "Ir al inicio", QKeySequence("Home"), [this] { stop(); if (current()) { current()->wave->anchor = current()->wave->cursor = 0; current()->wave->start = 0; refresh(); current()->wave->update(); } }, "start"));
    controls->addAction(add(transport, "Reproducir / detener", QKeySequence("Space"), [this] { if (playback->document) stop(); else play(); }, "play"));
    controls->addAction(add(transport, "Detener", QKeySequence("Esc"), [this] { stop(); }, "stop"));
    controls->addAction(add(transport, "Reproducir en bucle", QKeySequence("Ctrl+L"), [this] { play(true); }, "loop"));
    controls->addSeparator(); positionLabel = new QLabel; positionLabel->setObjectName("timeField"); positionLabel->setMinimumWidth(104); positionLabel->setAlignment(Qt::AlignCenter);
    controls->addWidget(positionLabel);
    auto *spacer = new QWidget; spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred); controls->addWidget(spacer);
    auto *dock = new QDockWidget("Niveles", this); dock->setObjectName("levelsDock"); meter = new LevelMeter; dock->setWidget(meter); dock->setFixedWidth(92); addDockWidget(Qt::RightDockWidgetArea, dock);
    view->addAction(dock->toggleViewAction());
    add(windows, "Cascada", {}, [this] { workspace->cascadeSubWindows(); layoutMinimized(); });
    add(windows, "Mosaico", {}, [this] { workspace->tileSubWindows(); layoutMinimized(); });
    add(windows, "Siguiente", QKeySequence::NextChild, [this] { workspace->activateNextSubWindow(); });
    add(windows, "Anterior", QKeySequence::PreviousChild, [this] { workspace->activatePreviousSubWindow(); });
    selectionLabel = new QLabel; formatLabel = new QLabel; statusBar()->addWidget(selectionLabel, 1); statusBar()->addPermanentWidget(formatLabel);
    add(help, "Acerca de ForgeCraft", {}, [this] { QMessageBox::about(this, "ForgeCraft", "<h2>ForgeCraft 0.1</h2><p>Editor de audio en Qt 6.7 y C++17.</p><p>Inspirado en los editores de audio clásicos. Implementación independiente.</p><p>WAV PCM / float y MP3 mono y estéreo.<br>Exportación WAV PCM de 8/16 bits y MP3 de 128–320 kbps.<br>Edición en memoria; hasta 20 operaciones de deshacer.</p><p>MP3: <a href='https://www.mpg123.de/'>mpg123</a>, copyright 1995–2020 Michael Hipp y otros, LGPL 2.1; <a href='https://lame.sourceforge.io/'>LAME</a>, copyright 1999 Mark Taylor y colaboradores, LGPL 2 o posterior.<br>Licencias y fuentes: carpeta licenses junto al ejecutable.</p>"); });
    connect(workspace, &QMdiArea::subWindowActivated, this, [this] { stop(); refresh(); });
    connect(&timer, &QTimer::timeout, this, &MainWindow::tick); timer.setInterval(35);
    setAcceptDrops(true); workspace->viewport()->setAcceptDrops(true); qApp->installEventFilter(this);
    addDocument(AudioData{});
}
MainWindow::~MainWindow() {
    qApp->removeEventFilter(this);
    stop();
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
            if (QFileInfo(path).isFile() && (path.endsWith(".wav", Qt::CaseInsensitive) || path.endsWith(".mp3", Qt::CaseInsensitive))) paths.append(path);
        }
        if (!paths.isEmpty()) {
            drop->setDropAction(Qt::CopyAction); drop->accept();
            if (event->type() == QEvent::Drop) QTimer::singleShot(0, this, [this, paths] { for (const auto &path : paths) openFile(path, true); });
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
void MainWindow::addDocument(AudioData data, const QString &path) {
    auto *d = new Document; d->data = std::move(data); d->path = path;
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
        stop(); disconnect(&d->undo, nullptr, this, nullptr); disconnect(d->wave, nullptr, this, nullptr); return true;
    };
    auto activate = [this, window] { workspace->setActiveSubWindow(window); };
    d->play = [this, activate] { activate(); play(); }; d->stop = [this] { stop(); };
    d->record = [this, activate] { activate(); recordAudio(); };
    d->beginning = [this, d, activate] { activate(); stop(); d->wave->anchor = d->wave->cursor = d->wave->start = 0; d->wave->update(); refresh(); };
    d->ending = [this, d, activate] { activate(); stop(); d->wave->anchor = d->wave->cursor = d->data.frames(); d->wave->start = qMax(qint64(0), d->data.frames() - d->wave->visibleFrames()); d->wave->update(); refresh(); };
    // Arrastrar la selección fuera de la onda: sobre otro módulo se inserta en su
    // documento y sobre el área de trabajo abre un documento nuevo con el fragmento.
    // En ambos casos el documento de origen no cambia.
    d->copySelectionToDocument = [this, d](qint64 first, qint64 last, const QPoint &global) {
        if (first < 0 || last > d->data.frames() || last <= first) return;
        AudioData fragment;
        fragment.rate = d->data.rate; fragment.channels = d->data.channels; fragment.bitDepth = d->data.bitDepth;
        fragment.samples = d->data.samples.mid(first * d->data.channels, (last - first) * d->data.channels);
        Waveform *dropWave = nullptr;
        Document *target = documentAt(workspace->viewport(), global, dropWave);
        if (target == d) return;   // soltada sobre su propio módulo: no se hace nada
        if (target) {
            insertFragment(target, dropWave ? dropWave->frameAt(dropWave->mapFromGlobal(global).x()) : -1, fragment);
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
    window->show(); workspace->setActiveSubWindow(window); refresh();
}
void MainWindow::openFile(const QString &path, bool alwaysNew) {
    AudioData data; QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor); bool ok = AudioData::read(path, data, error); QApplication::restoreOverrideCursor();
    if (!ok) { QMessageBox::warning(this, "Abrir audio", error); return; }
    auto *empty = current(); bool replace = !alwaysNew && empty && empty->path.isEmpty() && empty->data.frames() == 0 && empty->undo.isClean();
    auto *emptyWindow = workspace->currentSubWindow(); addDocument(std::move(data), path);
    if (replace) emptyWindow->close();
}
void MainWindow::refresh() {
    auto *d = current(); undoAction->setEnabled(d && d->undo.canUndo()); redoAction->setEnabled(d && d->undo.canRedo());
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
    formatLabel->setText(QString("%1 Hz  |  %2 bit  |  %3  |  %4").arg(d->data.rate).arg(d->data.bitDepth).arg(d->data.channels == 2 ? "Estéreo" : "Mono").arg(timeText(d->data.frames(), d->data.rate)));
    setWindowTitle(d->name + (d->undo.isClean() ? "" : " *") + " — ForgeCraft");
}
void MainWindow::commit(Document *d, const AudioData &after, const QString &name, qint64 a, qint64 b) { stop(); d->undo.push(new AudioEdit(d, after, name, a, b)); refresh(); }

// Inserta un fragmento en otro documento, en la posición indicada (-1: inicio de
// la selección del destino). Es una operación deshacible del documento destino.
void MainWindow::insertFragment(Document *target, qint64 position, const AudioData &fragment) {
    if (!target || fragment.samples.isEmpty()) return;
    AudioData next = target->data;
    if (next.frames() && (next.rate != fragment.rate || next.channels != fragment.channels)) {
        QMessageBox::information(this, "Insertar selección", "El documento de destino usa otra frecuencia o número de canales. Ajusta su formato con Resample o Channel Converter antes de insertar.");
        return;
    }
    if (!next.frames()) { next.rate = fragment.rate; next.channels = fragment.channels; next.bitDepth = fragment.bitDepth; }
    const qint64 at = position >= 0 ? qBound(qint64(0), position, next.frames()) : target->wave->first();
    next.samples = next.samples.first(at * next.channels) + fragment.samples + next.samples.mid(at * next.channels);
    stop();
    commit(target, next, "Insertar selección", at, at + fragment.frames());
    if (auto *window = qobject_cast<QMdiSubWindow *>(target->parentWidget())) workspace->setActiveSubWindow(window);
    refresh();
    statusBar()->showMessage(QString("Selección insertada en «%1» (%2)").arg(target->name, timeText(fragment.frames(), fragment.rate)), 4000);
}
void MainWindow::copy() {
    auto *d = current(); if (!d || d->wave->first() == d->wave->last()) return;
    clipboard = d->data; clipboard.samples = d->data.samples.mid(d->wave->first() * d->data.channels, (d->wave->last() - d->wave->first()) * d->data.channels);
}
void MainWindow::paste() {
    auto *d = current(); if (!d || clipboard.samples.isEmpty()) return;
    if (d->data.frames() && (d->data.rate != clipboard.rate || d->data.channels != clipboard.channels)) { QMessageBox::information(this, "Pegar", "La frecuencia y los canales deben coincidir. Usa Resample o Channel Converter para ajustarlos antes de pegar."); return; }
    AudioData next = d->data; if (!next.frames()) { next.rate = clipboard.rate; next.channels = clipboard.channels; }
    qint64 a = d->wave->first(), b = d->wave->last();
    next.samples = next.samples.first(a * next.channels) + clipboard.samples + next.samples.mid(b * next.channels);
    commit(d, next, "Pegar", a, a + clipboard.frames());
}
void MainWindow::edit(const QString &op, double gain) {
    auto *d = current(); if (!d || !d->data.frames()) return;
    qint64 a = d->wave->first(), b = d->wave->last(); bool selected = a != b;
    if (!selected && (op == "delete" || op == "trim")) return;
    if (!selected) { a = 0; b = d->data.frames(); }
    AudioData next = d->data;
    if (op == "delete") { next.samples.remove(a * next.channels, (b - a) * next.channels); b = a; }
    else if (op == "trim") { next.samples = next.samples.mid(a * next.channels, (b - a) * next.channels); b -= a; a = 0; }
    else next.process(a, b, op, gain);
    commit(d, next, op, a, b);
}
bool MainWindow::save(Document *d, bool saveAs) {
    QString path = d->path;
    QString wavFilter = d->data.bitDepth == 8 ? "Audio WAV PCM de 8 bits (*.wav)" : "Audio WAV PCM de 16 bits (*.wav)";
    QString filter = path.endsWith(".mp3", Qt::CaseInsensitive) ? "Audio MP3 (*.mp3)" : wavFilter;
    if (path.isEmpty() || saveAs) {
        QFileDialog dialog(this, "Guardar audio");
        dialog.setOption(QFileDialog::DontUseNativeDialog);
        dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setNameFilters({wavFilter, "Audio MP3 (*.mp3)"}); dialog.selectNameFilter(filter);
        dialog.setDefaultSuffix(filter.contains("*.mp3") ? "mp3" : "wav");
        dialog.selectFile(path.isEmpty() ? d->name : path);
        connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog](const QString &selected) {
            QString suffix = selected.contains("*.mp3") ? "mp3" : "wav";
            dialog.setDefaultSuffix(suffix);
            if (!dialog.selectedFiles().isEmpty()) {
                QString name = dialog.selectedFiles().first();
                if (name.endsWith(".wav", Qt::CaseInsensitive) || name.endsWith(".mp3", Qt::CaseInsensitive)) dialog.selectFile(name.chopped(3) + suffix);
            }
        });
        if (dialog.exec() != QDialog::Accepted) return false;
        path = dialog.selectedFiles().value(0); filter = dialog.selectedNameFilter();
    }
    if (path.isEmpty()) return false;
    if (!path.endsWith(".wav", Qt::CaseInsensitive) && !path.endsWith(".mp3", Qt::CaseInsensitive)) {
        path += filter.contains("*.mp3") ? ".mp3" : ".wav";
        if (QFileInfo::exists(path) && QMessageBox::question(this, "Reemplazar archivo", "El archivo «" + QFileInfo(path).fileName() + "» ya existe. ¿Quieres reemplazarlo?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;
    }
    int bitrate = d->mp3Bitrate;
    if (path.endsWith(".mp3", Qt::CaseInsensitive)) {
        const QStringList qualities = {"128 kbps", "192 kbps", "256 kbps", "320 kbps"};
        bool ok; QString quality = QInputDialog::getItem(this, "Guardar MP3", "Calidad MP3 (bitrate constante):", qualities, qMax(0, int(qualities.indexOf(QString::number(bitrate) + " kbps"))), false, &ok);
        if (!ok) return false;
        bitrate = quality.section(' ', 0, 0).toInt();
        if (d->data.rate != 32000 && d->data.rate != 44100 && d->data.rate != 48000)
            if (QMessageBox::information(this, "Frecuencia MP3", "El MP3 se guardará a 44.100 Hz. El audio de este documento conservará su frecuencia original.", QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok) return false;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error; bool ok = d->data.write(path, error, bitrate);
    QApplication::restoreOverrideCursor();
    if (!ok) { QMessageBox::warning(this, "Guardar", error); return false; }
    d->mp3Bitrate = bitrate;
    d->path = path; d->name = QFileInfo(path).fileName(); d->undo.setClean(); refresh(); statusBar()->showMessage("Guardado: " + path, 5000); return true;
}
bool MainWindow::confirmClose(Document *d) {
    if (d->undo.isClean()) return true;
    auto result = QMessageBox::question(this, "Cambios sin guardar", "¿Guardar los cambios de «" + d->name + "»?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return result == QMessageBox::Discard || (result == QMessageBox::Save && save(d));
}
void MainWindow::closeEvent(QCloseEvent *event) { for (auto *window : workspace->subWindowList()) if (!confirmClose(static_cast<Document *>(window->widget()))) { event->ignore(); return; } stop(); event->accept(); }
void MainWindow::tone() {
    bool ok; double frequency = QInputDialog::getDouble(this, "Generar tono", "Frecuencia (Hz):", 440, 20, 20000, 1, &ok); if (!ok) return;
    double seconds = QInputDialog::getDouble(this, "Generar tono", "Duración (segundos):", 5, 0.01, 600, 2, &ok); if (!ok) return;
    addDocument(AudioData{}); auto *d = current(); d->name = "Tono " + QString::number(frequency) + " Hz";
    auto data = AudioData::tone(seconds, frequency); commit(d, data, "Generar tono", 0, data.frames());
}
void MainWindow::play(bool loop) {
    stop(); auto *d = current(); if (!d || !d->data.frames()) return;
#ifdef Q_OS_WIN
    qint64 a = d->wave->first(), b = d->wave->last();
    if (a == b) { a = d->wave->cursor; b = d->data.frames(); if (a == b) a = 0; }
    playback->bytes.resize((b - a) * d->data.channels * 2);
    for (qint64 i = a * d->data.channels; i < b * d->data.channels; ++i)
        qToLittleEndian(quint16(qint16(std::lround(d->data.samples[i] * 32767))), playback->bytes.data() + (i - a * d->data.channels) * 2);
    WAVEFORMATEX format{}; format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = WORD(d->data.channels); format.nSamplesPerSec = DWORD(d->data.rate);
    format.wBitsPerSample = 16; format.nBlockAlign = WORD(d->data.channels * 2); format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    MMRESULT result = waveOutOpen(&playback->handle, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) { playback->handle = nullptr; QMessageBox::warning(this, "Reproducción", "No se pudo abrir el dispositivo de audio (código " + QString::number(result) + ")."); return; }
    playback->header = {}; playback->header.lpData = playback->bytes.data(); playback->header.dwBufferLength = DWORD(playback->bytes.size());
    result = waveOutPrepareHeader(playback->handle, &playback->header, sizeof(WAVEHDR));
    if (result == MMSYSERR_NOERROR) result = waveOutWrite(playback->handle, &playback->header, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) { stop(); QMessageBox::warning(this, "Reproducción", "No se pudo enviar el audio al dispositivo."); return; }
    playback->document = d; playback->begin = a; playback->end = b; playback->loop = loop; timer.start();
#endif
}

// Reproduce un fragmento sin documento asociado: lo usan Preview y Bypass de los
// diálogos de proceso. El audio se copia al búfer de reproducción, así que la
// fuente puede liberarse al volver.
bool MainWindow::playPreview(const AudioData &audio, qint64 first, qint64 last, std::function<void()> finished) {
    stop();
#ifdef Q_OS_WIN
    first = std::clamp(first, qint64(0), audio.frames());
    last = std::clamp(last, first, audio.frames());
    if (last <= first || audio.samples.isEmpty()) return false;
    playback->bytes.resize((last - first) * audio.channels * 2);
    for (qint64 i = first * audio.channels; i < last * audio.channels; ++i)
        qToLittleEndian(quint16(qint16(std::lround(audio.samples[i] * 32767))), playback->bytes.data() + (i - first * audio.channels) * 2);
    WAVEFORMATEX format{}; format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = WORD(audio.channels); format.nSamplesPerSec = DWORD(audio.rate);
    format.wBitsPerSample = 16; format.nBlockAlign = WORD(audio.channels * 2); format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    MMRESULT result = waveOutOpen(&playback->handle, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) { playback->handle = nullptr; QMessageBox::warning(this, "Previsualización", "No se pudo abrir el dispositivo de audio (código " + QString::number(result) + ")."); return false; }
    playback->header = {}; playback->header.lpData = playback->bytes.data(); playback->header.dwBufferLength = DWORD(playback->bytes.size());
    result = waveOutPrepareHeader(playback->handle, &playback->header, sizeof(WAVEHDR));
    if (result == MMSYSERR_NOERROR) result = waveOutWrite(playback->handle, &playback->header, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) { stop(); QMessageBox::warning(this, "Previsualización", "No se pudo enviar el audio al dispositivo."); return false; }
    playback->document = nullptr; playback->begin = first; playback->end = last; playback->loop = false;
    previewEnded = std::move(finished);
    timer.start();
    return true;
#else
    Q_UNUSED(first); Q_UNUSED(last); Q_UNUSED(finished);
    return false;
#endif
}
void MainWindow::stop() {
    timer.stop();
    previewEnded = nullptr;
#ifdef Q_OS_WIN
    if (playback->handle) { waveOutReset(playback->handle); waveOutUnprepareHeader(playback->handle, &playback->header, sizeof(WAVEHDR)); waveOutClose(playback->handle); playback->handle = nullptr; }
#endif
    if (playback->document) { playback->document->wave->playhead = -1; playback->document->wave->update(); playback->document = nullptr; }
    playback->bytes.clear(); meter->setLevels(0, 0); refresh();
}
void MainWindow::tick() {
#ifdef Q_OS_WIN
    auto *d = playback->document;
    if (!d) {
        // Vista previa de un proceso: solo hay que detectar el final.
        if (playback->header.dwFlags & WHDR_DONE) {
            auto callback = previewEnded;
            stop();
            if (callback) callback();
        }
        return;
    }
    if (playback->header.dwFlags & WHDR_DONE) { bool loop = playback->loop; stop(); if (loop) play(true); return; }
    MMTIME t{}; t.wType = TIME_SAMPLES;
    if (waveOutGetPosition(playback->handle, &t, sizeof(t)) != MMSYSERR_NOERROR) return;
    qint64 offset = t.wType == TIME_SAMPLES ? t.u.sample : t.wType == TIME_BYTES ? t.u.cb / (d->data.channels * 2) : qint64(t.u.ms) * d->data.rate / 1000;
    qint64 f = qMin(playback->begin + offset, playback->end); d->wave->playhead = f;
    double peaks[2] = {0, 0}; for (qint64 frame = f; frame < qMin(d->data.frames(), f + 1024); ++frame) for (int c = 0; c < d->data.channels; ++c) peaks[c] = std::max(peaks[c], double(std::abs(d->data.samples[frame * d->data.channels + c])));
    meter->setLevels(peaks[0], d->data.channels == 1 ? peaks[0] : peaks[1]);
    if (f > d->wave->start + d->wave->visibleFrames()) d->wave->start = f;
    d->wave->update(); refresh();
#endif
}

