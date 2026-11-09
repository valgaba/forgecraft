#pragma once
#include "waveform.h"
#include "vumeter.h"
#include <QIcon>
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
class PreviewPlayer;
class QToolButton;


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
    QToolButton *followButton;
    QLabel *position;
    QLabel *selectionEnd, *selectionLength;
    QLabel *titleLabel;
    QFrame *header;
    std::function<void()> play, stop, beginning, ending, record;
    // Arrastrar la selección fuera de la onda crea un documento con ese fragmento.
    std::function<void(qint64 first, qint64 last, const QPoint &global)> copySelectionToDocument;
    std::function<void(const QPoint &global)> contextMenu;
    void setTitle(const QString &title);
    // Indicador de grabación: contador en la fila de tiempos y parpadeo del botón.
    void setRecordingOn(bool on);
    void setRecordTime(qint64 frames, int rate);
    void toggleRecordBlink();
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
    QToolButton *recordButton = nullptr;
    QIcon recordIcon, recordIconDark;
    bool recordOn = false;
    bool recordBlink = false;
    QString fullTitle;
    bool minimized = false;
    QSize expandedSize;
    QPoint expandedPosition;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(std::function<Recorder *()> recordingSource = {}, bool monitorInput = true);
    ~MainWindow() override;
    void openFile(const QString &path, bool alwaysNew = false);
protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    struct Playback;
    friend int playbackTests();
    friend int recordingScopeTests();
    friend int inputMonitoringTests();
    friend int normalizerDialogTests();
    friend int processPreviewTests();
    friend int processPresetTests();
    friend int audioFormatUiTests();
    std::unique_ptr<Playback> playback;
    std::unique_ptr<PreviewPlayer> livePreview;
    std::function<Recorder *()> recordingSource;
    int recordingNumber = 0;
    // Grabación directa al documento enfocado, sin cuadro intermedio.
    Recorder *recorder = nullptr;
    bool inputMonitoringEnabled = true;
    QTimer inputMonitorRetry;
    void ensureRecorder();
    void startInputMonitor();
    Document *recordTarget = nullptr;
    bool recording = false;
    int recordBlinkTicks = 0;
    quint32 inputDevice = 0xffffffffu;   // WAVE_MAPPER hasta que se elija otro
    QMdiArea *workspace;
    VuMeter *inputVu = nullptr;
    VuMeter *outputVu = nullptr;
    QToolButton *inputMuteButton = nullptr, *outputMuteButton = nullptr;
    bool inputMuted = false, outputMuted = false;
    void setInputMuted(bool muted);
    void setOutputMuted(bool muted);
    QPointer<VuMeter> previewVu;
    QLabel *positionLabel, *formatLabel, *selectionLabel;
    QTimer timer;
    AudioData clipboard;
    QAction *undoAction, *redoAction;
    QAction *followPlaybackAction = nullptr;
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
    void addDocument(AudioData data, const QString &path = {}, Waveform::PeakData prepared = {});
    void refresh();
    void edit(const QString &op, double gain = 1);
    void copy();
    void paste();
    void commit(Document *d, const AudioData &after, const QString &name, qint64 first, qint64 last);
    bool save(Document *d, bool saveAs = false);
    bool confirmClose(Document *d);
    void insertFragment(Document *target, qint64 position, const AudioData &fragment, bool mix = false, double sourceGain = 1.0, double targetGain = 1.0, qint64 tune = 0);
    void showOptions();
    bool playTestTone(quint32 device);
    quint32 outputDevice = 0xffffffffu;   // WAVE_MAPPER hasta que se elija otro
    void addMarker();
    void removeMarker();
    void removeMarkerAt(Document *document, int index);
    // true: al soltar sobre otra ventana se mezcla; false: se inserta.
    bool mixOnDrop = true;
    Waveform *dragPreviewWave = nullptr;   // ventana que muestra el fantasma del arrastre
    void play(bool loop = false);
    bool playbackIsRunning(Document *document) const;
    void playDocument(Document *document, bool loop);
    bool startPlayback(const AudioData &audio, qint64 first, qint64 last, Document *document, bool loop, QString &error, bool forcePcm16 = false);
    // Vista previa de un proceso: reproduce un audio sin documento asociado.
    // Devuelve si la reproducción llegó a empezar.
    bool playPreview(const AudioData &audio, qint64 first, qint64 last, std::function<void()> finished);
    bool playLivePreview(const AudioData &audio, std::function<void()> finished, QWidget *errorParent);
    void stop();
    void stopDocument(Document *document);
    void stopPlayback();
    void tick();
    void tone();
    void startRecording();
    void stopRecording();
    void setupProcessingMenus(QMenu *process, QMenu *effects);
    void runProcess(const QString &id);
    void showEditMenu(const QPoint &global);
    QString nextUntitledName() const;
    void layoutMinimized();
};
