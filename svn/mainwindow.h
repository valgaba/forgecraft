#pragma once
#include "waveform.h"
#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QFrame>
#include <QPointer>
#include <QScrollBar>
#include <QLabel>
#include <QTimer>
#include <QUndoStack>
#include <memory>
#include <functional>

class Recorder;
class QToolButton;

class LevelMeter : public QWidget {
public:
    explicit LevelMeter(QWidget *parent = nullptr) : QWidget(parent) { setMinimumWidth(64); }
    double levels[2] = {0, 0};
    void setLevels(double left, double right) {
        levels[0] = left; levels[1] = right;
        for (int c = 0; c < 2; ++c) peaks[c] = qMax(levels[c], peaks[c] * 0.94);
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    double peaks[2] = {0, 0};
};

// Barra de título propia de un módulo. Las subventanas MDI dejan de usar el
// marco del sistema (Opción A del informe): arrastrar mueve el módulo y el
// doble clic lo maximiza o lo restaura.
class ModuleHeader : public QFrame {
public:
    explicit ModuleHeader(QWidget *parent = nullptr);
protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
private:
    QPoint dragOrigin;
    QPoint windowOrigin;
    bool dragging = false;
};

// Esquina inferior derecha: solo marca visualmente el redimensionado; los
// eventos los gestiona MainWindow para que también funcionen los bordes.
class ModuleGrip : public QWidget {
public:
    explicit ModuleGrip(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("moduleGrip");
        setFixedSize(16, 16);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
protected:
    void paintEvent(QPaintEvent *) override;
};

class AudioWindow : public QMdiSubWindow {
public:
    std::function<bool()> canClose;
protected:
    void closeEvent(QCloseEvent *event) override;
    bool event(QEvent *event) override;
};

class Document : public QWidget {
public:
    explicit Document(QWidget *parent = nullptr);
    AudioData data;
    Waveform *wave;
    QUndoStack undo;
    QString path;
    QString name = "Sin título";
    int mp3Bitrate = 192;
    QScrollBar *scroll;
    QLabel *position;
    QLabel *selectionEnd, *selectionLength;
    QLabel *titleLabel;
    QFrame *header;
    std::function<void()> play, stop, beginning, ending, record;
    // Arrastrar la selección fuera de la onda crea un documento con ese fragmento.
    std::function<void(qint64 first, qint64 last, const QPoint &global)> copySelectionToDocument;
    std::function<void(const QPoint &global)> contextMenu;
    void setTitle(const QString &title);
    void setActive(bool active);
    // Minimizar deja el módulo como botón en el pie del área de trabajo.
    void setMinimized(bool minimized);
    bool isMinimized() const { return minimized; }
    static constexpr int MinimizedWidth = 176;
    static constexpr int MinimizedHeight = 26;
    std::function<void()> moduleStateChanged;
protected:
    void resizeEvent(QResizeEvent *) override;
private:
    ModuleGrip *grip = nullptr;
    QWidget *body = nullptr;
    QToolButton *minimizeButton = nullptr;
    QString fullTitle;
    bool minimized = false;
    QSize expandedSize;
    QPoint expandedPosition;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(std::function<Recorder *()> recordingSource = {});
    ~MainWindow() override;
    void openFile(const QString &path, bool alwaysNew = false);
protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    struct Playback;
    std::unique_ptr<Playback> playback;
    std::function<Recorder *()> recordingSource;
    int recordingNumber = 0;
    QMdiArea *workspace;
    QLabel *positionLabel, *formatLabel, *selectionLabel;
    LevelMeter *meter;
    QTimer timer;
    AudioData clipboard;
    QAction *undoAction, *redoAction;
    QAction *cutAction, *copyAction, *pasteAction, *deleteAction, *trimAction, *selectAllAction;
    std::function<void()> previewEnded;
    // Redimensionado de módulos sin marco: se arrastra cualquiera de sus bordes.
    QPointer<QMdiSubWindow> resizeTarget, cursorTarget;
    Qt::Edges resizeEdges;
    QPoint resizeOrigin;
    QRect resizeFrame;
    Qt::Edges moduleEdges(QMdiSubWindow *sub, const QPoint &global) const;
    void updateResizeCursor(QMdiSubWindow *sub, Qt::Edges edges);
    Document *current() const;
    void addDocument(AudioData data, const QString &path = {});
    void refresh();
    void edit(const QString &op, double gain = 1);
    void copy();
    void paste();
    void commit(Document *d, const AudioData &after, const QString &name, qint64 first, qint64 last);
    bool save(Document *d, bool saveAs = false);
    bool confirmClose(Document *d);
    void insertFragment(Document *target, qint64 position, const AudioData &fragment);
    void play(bool loop = false);
    // Vista previa de un proceso: reproduce un audio sin documento asociado.
    // Devuelve si la reproducción llegó a empezar.
    bool playPreview(const AudioData &audio, qint64 first, qint64 last, std::function<void()> finished);
    void stop();
    void tick();
    void tone();
    void recordAudio();
    void setupProcessingMenus(QMenu *process, QMenu *effects);
    void runProcess(const QString &id);
    void showEditMenu(const QPoint &global);
    QString nextUntitledName() const;
    void layoutMinimized();
};
