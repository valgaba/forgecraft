#include "mainwindow.h"
#include "dsp.h"
#include "theme.h"
#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QAction>
#include <QPainter>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QMimeData>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QStringList>
#include <QDragEnterEvent>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QToolButton>
#include <QProgressDialog>
#include <QMenuBar>
#include <QMenu>
#include <cmath>
#include <cstdio>

int dspTests();
int recordingTests();
int recordingUiTests();

static int selfTest() {
    int failures = 0;
    auto check = [&failures](bool ok, const char *name) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", name); std::fflush(stdout); if (!ok) ++failures; };
    AudioData a = AudioData::tone(0.1, 440); check(a.frames() == 4410 && a.channels == 2, "tone frames and channels");
    QTemporaryDir temp(QApplication::applicationDirPath() + "/../build/audio-test-XXXXXX"); QString error;
    check(temp.isValid(), "test workspace");
    check(a.write(temp.filePath("test.wav"), error), "write WAV");
    if (!error.isEmpty()) { std::printf("%s\n", error.toUtf8().constData()); std::fflush(stdout); }
    AudioData b; bool loaded = AudioData::read(temp.filePath("test.wav"), b, error); check(loaded, "read WAV");
    if (!loaded) { std::printf("%s\n", error.toUtf8().constData()); return 1; }
    check(b.frames() == a.frames() && b.rate == a.rate && b.channels == 2, "WAV metadata round trip");
    double delta = 0; for (qsizetype i = 0; i < qMin(a.samples.size(), b.samples.size()); ++i) delta = qMax(delta, double(std::abs(a.samples[i] - b.samples[i])));
    check(delta < 0.00005, "PCM16 round trip precision");
    b.process(0, b.frames(), "normalize"); double peak = 0; for (float f : b.samples) peak = qMax(peak, double(std::abs(f))); check(std::abs(peak - 0.999) < 0.00001, "normalize peak");
    b.process(0, b.frames(), "fadeout"); check(b.samples.last() == 0, "fade out endpoint");
    b.process(0, 100, "silence"); bool silent = true; for (int i = 0; i < 200; ++i) silent &= b.samples[i] == 0; check(silent, "silence selected frames");
    AudioData c; c.samples = {0.1f, 0.2f, 0.3f, 0.4f}; c.process(0, 2, "reverse"); check(c.samples == QVector<float>({0.3f, 0.4f, 0.1f, 0.2f}), "reverse preserves stereo pairs");
    QFile bad(temp.filePath("bad.wav")); bad.open(QIODevice::WriteOnly); bad.write("RIFF\xFF\xFF\xFF\x7F" "WAVEdata\xFF\xFF\xFF\x7F", 20); bad.close();
    check(!AudioData::read(bad.fileName(), b, error), "reject truncated WAV");
    for (int channels : {1, 2}) {
        auto original = AudioData::tone(1.0, 440, 44100, channels);
        for (int bitrate : {128, 192, 256, 320}) {
            QString label = QString("MP3 %1 channels %2 kbps").arg(channels).arg(bitrate);
            QString path = temp.filePath(QString("Audio español %1 %2.MP3").arg(channels).arg(bitrate));
            bool written = original.write(path, error, bitrate); check(written, (label + " encode").toUtf8().constData());
            if (!written) { std::printf("%s\n", error.toUtf8().constData()); continue; }
            QFile encoded(path); encoded.open(QIODevice::ReadOnly); QByteArray header = encoded.read(4);
            check(header.size() == 4 && quint8(header[0]) == 0xff && (quint8(header[1]) & 0xe0) == 0xe0, "real MPEG frame header, not WAV");
            const int bitrates[] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
            check(header.size() == 4 && bitrates[quint8(header[2]) >> 4] == bitrate, "requested MP3 bitrate");
            AudioData decoded; bool loaded = AudioData::read(path, decoded, error); check(loaded, (label + " decode").toUtf8().constData());
            check(loaded && decoded.channels == channels && decoded.rate == original.rate && decoded.frames() == original.frames(), "MP3 gapless duration and format");
            double mse = 0;
            if (decoded.samples.size() == original.samples.size()) { for (qsizetype i = 0; i < original.samples.size(); ++i) { double difference = decoded.samples[i] - original.samples[i]; mse += difference * difference; } mse /= original.samples.size(); }
            else mse = 1;
            check(mse < 0.002, "MP3 decoded signal matches source within lossy tolerance");
        }
    }
    auto lowRate = AudioData::tone(.25, 440, 22050, 1); AudioData resampled;
    QString converted = temp.filePath("resampled.mp3");
    bool conversion = lowRate.write(converted, error, 192) && AudioData::read(converted, resampled, error);
    check(conversion && resampled.rate == 44100 && std::abs(resampled.duration() - lowRate.duration()) < .001, "MP3 export resampling preserves duration");
    QFile invalid(temp.filePath("invalid.mp3")); invalid.open(QIODevice::WriteOnly); invalid.write("This is not MP3 audio"); invalid.close();
    auto sentinel = AudioData::tone(.01, 1000);
    auto unchanged = sentinel.samples;
    check(!AudioData::read(invalid.fileName(), sentinel, error) && sentinel.samples == unchanged, "invalid MP3 fails without changing destination");
    QString protectedPath = temp.filePath("protected.mp3");
    QFile protectedFile(protectedPath); protectedFile.open(QIODevice::WriteOnly); protectedFile.write("keep original"); protectedFile.close();
    check(!AudioData{}.write(protectedPath, error), "empty MP3 rejected");
    protectedFile.open(QIODevice::ReadOnly); check(protectedFile.readAll() == "keep original", "failed MP3 export preserves existing file"); protectedFile.close();
    check(!a.write(protectedPath, error, 777), "unsupported MP3 bitrate rejected");
    Document doc; doc.data = a; doc.undo.push(new QUndoCommand("change")); check(!doc.undo.isClean(), "document dirty state"); doc.undo.undo(); check(doc.undo.isClean(), "document undo clean state");
    failures += dspTests();
    failures += recordingTests();
    std::printf("%d failure(s)\n", failures); return failures ? 1 : 0;
}
// Aplica la identidad visual RADIT: estilo Fusion (dibuja con la paleta, sin
// restos nativos claros), paleta oscura y hoja de estilo externa.
//
// La hoja se busca junto al ejecutable para poder ajustarla sin recompilar
// (`build.ps1 -Deploy` la copia) y, si no está, se usa la copia incrustada en
// el .qrc. Nunca una ruta relativa al directorio de trabajo: Radit lo hace así
// y se queda sin tema al arrancar desde otra carpeta.
static void applyTheme(QApplication &app) {
    if (QStyle *style = QStyleFactory::create("Fusion")) app.setStyle(style);
    app.setPalette(Theme::palette());
    QFont font = app.font();
    font.setFamily(QStringLiteral("Segoe UI"));
    font.setPointSizeF(9);
    app.setFont(font);
    QString sheet;
    QFile external(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("forgecraft.qss")));
    if (external.open(QIODevice::ReadOnly)) sheet = QString::fromUtf8(external.readAll());
    if (sheet.isEmpty()) {
        QFile embedded(QStringLiteral(":/style/forgecraft.qss"));
        if (embedded.open(QIODevice::ReadOnly)) sheet = QString::fromUtf8(embedded.readAll());
        else qWarning("No se pudo cargar forgecraft.qss");
    }
    app.setStyleSheet(sheet);
}

int main(int argc, char **argv) {
    QApplication app(argc, argv); app.setApplicationName("ForgeCraft"); app.setApplicationVersion("0.1.0"); app.setOrganizationName("ForgeCraft");
    QPixmap emblem(16, 16); emblem.fill(Qt::transparent);
    { QPainter p(&emblem); p.setPen(Qt::black); p.setBrush(Qt::white); p.drawRect(3, 1, 10, 13); p.setPen(Theme::RecordDot); QPolygon wave; wave << QPoint(1,8) << QPoint(4,8) << QPoint(5,3) << QPoint(7,13) << QPoint(9,4) << QPoint(11,10) << QPoint(15,8); p.drawPolyline(wave); }
    app.setWindowIcon(QIcon(emblem));
    applyTheme(app);
    if (app.arguments().contains("--self-test")) return selfTest();
    if (app.arguments().contains("--smoke-test")) app.setAttribute(Qt::AA_DontUseNativeDialogs);
    MainWindow window; window.show();
    if (app.arguments().contains("--smoke-test")) {
        QTimer::singleShot(100, &window, [&] {
            QTemporaryDir temp(QApplication::applicationDirPath() + "/../build/ui-test-XXXXXX");
            QString error; auto tone = AudioData::tone(3, 440); QString path = temp.filePath("Prueba estéreo.wav");
            if (!tone.write(path, error)) { app.exit(1); return; }
            window.openFile(path);
            auto *workspace = window.findChild<QMdiArea *>(); auto *doc = static_cast<Document *>(workspace->activeSubWindow()->widget());
            auto trigger = [&](const QString &name) { for (auto *a : window.findChildren<QAction *>()) if (a->text() == name) { a->trigger(); return true; } return false; };
            doc->wave->anchor = 22050; doc->wave->cursor = 66150;
            bool ok = trigger("&Copiar") && trigger("&Eliminar selección") && doc->data.frames() == 88200;
            trigger("&Deshacer"); ok &= doc->data.frames() == 132300;
            trigger("&Rehacer"); ok &= doc->data.frames() == 88200;
            trigger("&Pegar"); ok &= doc->data.frames() == 132300;
            trigger("In");
            const QString savedMp3 = temp.filePath("Guardado español.mp3");
            QTimer dialogResponder; dialogResponder.setInterval(10);
            bool cancelQuality = false, dialogFailure = false; int saveDialogs = 0, qualityDialogs = 0;
            QElapsedTimer deadline; deadline.start();
            QObject::connect(&dialogResponder, &QTimer::timeout, &window, [&] {
                auto *modal = QApplication::activeModalWidget(); if (!modal) return;
                if (deadline.elapsed() > 5000 || qobject_cast<QMessageBox *>(modal)) { dialogFailure = true; if (auto *dialog = qobject_cast<QDialog *>(modal)) dialog->reject(); return; }
                if (modal->property("testHandled").toBool()) return;
                modal->setProperty("testHandled", true);
                if (auto *dialog = qobject_cast<QFileDialog *>(modal)) {
                    ++saveDialogs;
                    dialog->selectFile(temp.filePath("Guardado español.wav"));
                    dialog->selectNameFilter("Audio MP3 (*.mp3)");
                    QMetaObject::invokeMethod(dialog, "filterSelected", Qt::DirectConnection, Q_ARG(QString, QString("Audio MP3 (*.mp3)")));
                    QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                } else if (auto *dialog = qobject_cast<QInputDialog *>(modal)) {
                    ++qualityDialogs;
                    if (cancelQuality) dialog->reject();
                    else { dialog->setTextValue("256 kbps"); dialog->accept(); }
                }
            });
            dialogResponder.start(); trigger("Guardar &como…"); dialogResponder.stop();
            AudioData reopened;
            bool mp3Saved = !dialogFailure && saveDialogs == 1 && qualityDialogs == 1 && doc->path == savedMp3 && doc->undo.isClean() && doc->mp3Bitrate == 256 && AudioData::read(savedMp3, reopened, error) && reopened.frames() == doc->data.frames();
            ok &= mp3Saved;
            std::printf("%s UI MP3: save-as filter, extension, bitrate and reopen\n", mp3Saved ? "PASS" : "FAIL");
            QFile saved(savedMp3); saved.open(QIODevice::ReadOnly); QByteArray savedBytes = saved.readAll(); saved.close();
            trigger("Out"); cancelQuality = true; deadline.restart(); dialogResponder.start(); trigger("&Guardar"); dialogResponder.stop();
            saved.open(QIODevice::ReadOnly); bool sameBytes = saved.readAll() == savedBytes; bool preserved = sameBytes && !doc->undo.isClean(); saved.close();
            if (!preserved) std::printf("Cancel detail: same bytes %d, dirty %d, quality dialogs %d, dialog failure %d\n", int(sameBytes), int(!doc->undo.isClean()), qualityDialogs, int(dialogFailure));
            ok &= preserved; trigger("&Deshacer");
            std::printf("%s UI MP3: cancel preserves file and unsaved edits\n", preserved ? "PASS" : "FAIL");
            window.openFile(savedMp3);
            auto *opened = static_cast<Document *>(workspace->activeSubWindow()->widget());
            ok &= opened != doc && opened->data.frames() == doc->data.frames() && opened->path == savedMp3;
            workspace->closeActiveSubWindow(); QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); app.processEvents();
            QTimer effectResponder; effectResponder.setInterval(5); int responseMode = 0; bool processingFailure = false, canceledWork = false;
            bool capturedDialog = false, capturedEq = false, previewControlsChecked = false, previewControlsOk = true;
            QObject::connect(&effectResponder, &QTimer::timeout, &window, [&] {
                auto *modal = QApplication::activeModalWidget(); if (!modal) return;
                if (auto *progress = qobject_cast<QProgressDialog *>(modal)) {
                    if (responseMode == 2) for (auto *button : progress->findChildren<QPushButton *>()) if (button->text().contains("Cancelar")) { canceledWork = true; button->click(); break; }
                    return;
                }
                if (auto *dialog = qobject_cast<QDialog *>(modal)) {
                    if (dialog->objectName() == "processDialog") {
                        if (!capturedDialog && responseMode == 0) { dialog->grab().save(QApplication::applicationDirPath() + "/../build/process-dialog.png"); capturedDialog = true; }
                        if (!capturedEq && dialog->windowTitle().contains("Graphic EQ")) { dialog->grab().save(QApplication::applicationDirPath() + "/../build/process-dialog-eq.png"); capturedEq = true; }
                        if (!previewControlsChecked) {
                            auto *preview = dialog->findChild<QPushButton *>("processPreview");
                            auto *bypass = dialog->findChild<QPushButton *>("processBypass");
                            previewControlsOk = preview && preview->isCheckable() && bypass && bypass->isCheckable() && !bypass->isEnabled();
                            previewControlsChecked = true;
                        }
                        if (responseMode == 1) dialog->reject();
                        else {
                            if (responseMode == 2) if (auto *rate = dialog->findChild<QDoubleSpinBox *>("rate")) rate->setValue(192000);
                            dialog->accept();
                        }
                    } else { processingFailure = true; dialog->reject(); }
                }
            });
            auto runEffect = [&](const QString &id) { for (auto *action : window.findChildren<QAction *>()) if (action->objectName() == "dsp." + id) { action->trigger(); return true; } return false; };
            auto baseline = doc->data; doc->wave->anchor = 22050; doc->wave->cursor = 66150;
            effectResponder.start();
            for (const auto &op : Dsp::operations()) {
                int before = doc->undo.index(); bool applied = runEffect(op.id) && doc->undo.index() == before + 1;
                if (applied) trigger("&Deshacer");
                applied &= doc->data.samples == baseline.samples && doc->data.rate == baseline.rate && doc->data.channels == baseline.channels && doc->data.bitDepth == baseline.bitDepth;
                ok &= applied;
                std::printf("%s UI process %s applies and undoes\n", applied ? "PASS" : "FAIL", op.id.toUtf8().constData()); std::fflush(stdout);
            }
            responseMode = 1; int unchangedIndex = doc->undo.index(); runEffect("volume");
            bool parameterCancel = doc->undo.index() == unchangedIndex && doc->data.samples == baseline.samples; ok &= parameterCancel;
            responseMode = 2; runEffect("resample");
            bool workCancel = canceledWork && doc->undo.index() == unchangedIndex && doc->data.samples == baseline.samples; ok &= workCancel && !processingFailure;
            effectResponder.stop();
            std::printf("%s UI parameter cancel, %s UI processing cancel\n", parameterCancel ? "PASS" : "FAIL", workCancel ? "PASS" : "FAIL");
            ok &= previewControlsOk;
            std::printf("%s UI process dialog preview and bypass controls\n", previewControlsOk ? "PASS" : "FAIL");
            auto *originalWindow = workspace->currentSubWindow(); int beforeDrop = workspace->subWindowList().size();
            QMimeData mime; mime.setUrls({QUrl::fromLocalFile(path), QUrl::fromLocalFile(savedMp3)});
            QDragEnterEvent enter(QPoint(20, 40), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(doc->wave, &enter);
            QDropEvent drop(QPointF(20, 40), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(doc->wave, &drop); app.processEvents();
            bool dropped = enter.isAccepted() && drop.isAccepted() && workspace->subWindowList().size() == beforeDrop + 2 && doc->data.samples == baseline.samples;
            ok &= dropped; std::printf("%s UI drop WAV + MP3 over waveform opens two new documents\n", dropped ? "PASS" : "FAIL");
            for (auto *child : workspace->subWindowList()) if (child != originalWindow) child->close();
            QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); app.processEvents(); workspace->setActiveSubWindow(originalWindow);
            QMimeData remote; remote.setUrls({QUrl("https://example.com/not-a-local-file.mp3")});
            QDragEnterEvent ignored(QPoint(20, 40), Qt::CopyAction, &remote, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(workspace->viewport(), &ignored); ok &= !ignored.isAccepted();
            doc->wave->anchor = doc->wave->cursor = 0; doc->wave->update();
            auto *audioWindow = workspace->activeSubWindow();
            trigger("&Nuevo"); auto *blank1 = workspace->activeSubWindow();
            trigger("&Nuevo"); auto *blank2 = workspace->activeSubWindow();
            ok &= workspace->subWindowList().size() == 3;
            trigger("Mosaico"); trigger("Cascada");
            // Módulos sin marco (Opción A): la barra arrastra, los bordes
            // redimensionan y pulsar dentro activa la subventana.
            auto sendMouse = [](QWidget *target, QEvent::Type type, const QPoint &local, Qt::MouseButtons buttons) {
                const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
                QMouseEvent event(type, QPointF(local), QPointF(target->mapToGlobal(local)), button, buttons, Qt::NoModifier);
                QApplication::sendEvent(target, &event);
            };
            const QPoint moduleOrigin = audioWindow->pos();
            const QPoint headerPoint(60, 12);
            sendMouse(doc->header, QEvent::MouseButtonPress, headerPoint, Qt::LeftButton);
            sendMouse(doc->header, QEvent::MouseMove, headerPoint + QPoint(24, 16), Qt::LeftButton);
            sendMouse(doc->header, QEvent::MouseButtonRelease, headerPoint + QPoint(24, 16), Qt::NoButton);
            const bool moved = audioWindow->pos() == moduleOrigin + QPoint(24, 16);
            const QSize moduleSize = audioWindow->size();
            // Arrastrar el borde derecho ensancha el módulo sin moverlo.
            const QPoint edgeGlobal = audioWindow->mapToGlobal(QPoint(audioWindow->width() - 3, audioWindow->height() / 2));
            QWidget *edgeWidget = QApplication::widgetAt(edgeGlobal);
            if (!edgeWidget) edgeWidget = audioWindow->widget();
            sendMouse(edgeWidget, QEvent::MouseMove, edgeWidget->mapFromGlobal(edgeGlobal), Qt::NoButton);
            const bool cursorHint = audioWindow->cursor().shape() == Qt::SizeHorCursor;
            sendMouse(edgeWidget, QEvent::MouseButtonPress, edgeWidget->mapFromGlobal(edgeGlobal), Qt::LeftButton);
            sendMouse(edgeWidget, QEvent::MouseMove, edgeWidget->mapFromGlobal(edgeGlobal + QPoint(30, 0)), Qt::LeftButton);
            sendMouse(edgeWidget, QEvent::MouseButtonRelease, edgeWidget->mapFromGlobal(edgeGlobal + QPoint(30, 0)), Qt::NoButton);
            const bool resized = audioWindow->width() == moduleSize.width() + 30 && audioWindow->height() == moduleSize.height();
            sendMouse(static_cast<Document *>(blank1->widget())->wave, QEvent::MouseButtonPress, QPoint(40, 40), Qt::LeftButton);
            const bool activated = workspace->activeSubWindow() == blank1;
            ok &= moved && resized && cursorHint && activated;
            std::printf("%s UI module header drag, edge resize, resize cursor and click activation\n", (moved && resized && cursorHint && activated) ? "PASS" : "FAIL");
            // Minimizar deja el módulo como botón en el pie del área de trabajo.
            const QSize expandedModuleSize = audioWindow->size();
            const QPoint expandedModulePos = audioWindow->pos();
            auto *minimizeButton = doc->findChild<QToolButton *>("moduleMinimize");
            bool collapsedModule = false, restoredModule = false;
            if (minimizeButton) {
                minimizeButton->click();
                app.processEvents();
                const QRect area = workspace->viewport()->rect();
                collapsedModule = doc->isMinimized() && audioWindow->width() == Document::MinimizedWidth
                                  && audioWindow->height() == Document::MinimizedHeight
                                  && audioWindow->pos().y() > area.bottom() - 80 && audioWindow->pos().x() < 40;
                minimizeButton->click();
                app.processEvents();
                restoredModule = !doc->isMinimized() && audioWindow->size() == expandedModuleSize && audioWindow->pos() == expandedModulePos;
            }
            ok &= collapsedModule && restoredModule;
            std::printf("%s UI module minimize to the bottom bar and restore\n", (collapsedModule && restoredModule) ? "PASS" : "FAIL");
            // Arrastrar sobre la onda deja una selección que se conserva al soltar.
            doc->wave->anchor = doc->wave->cursor = 0; doc->wave->update();
            const int selectionStartX = Theme::GutterWidth + 150, selectionEndX = Theme::GutterWidth + 320;
            sendMouse(doc->wave, QEvent::MouseButtonPress, QPoint(selectionStartX, doc->wave->height() / 2), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseMove, QPoint(selectionEndX, doc->wave->height() / 2), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseButtonRelease, QPoint(selectionEndX, doc->wave->height() / 2), Qt::NoButton);
            const bool keepsSelection = doc->wave->last() > doc->wave->first() && doc->wave->first() > 0;
            ok &= keepsSelection;
            std::printf("%s UI waveform drag keeps the selection after release\n", keepsSelection ? "PASS" : "FAIL");
            // Arrastrar desde dentro de la selección no la modifica: el gesto queda
            // reservado para sacarla del lienzo o soltarla en otro módulo.
            const qint64 heldFirst = doc->wave->first(), heldLast = doc->wave->last();
            const int middleX = (selectionStartX + selectionEndX) / 2;
            sendMouse(doc->wave, QEvent::MouseButtonPress, QPoint(middleX, doc->wave->height() / 2), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseMove, QPoint(middleX + 40, doc->wave->height() / 2), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseButtonRelease, QPoint(middleX + 40, doc->wave->height() / 2), Qt::NoButton);
            const bool selectionKept = doc->wave->first() == heldFirst && doc->wave->last() == heldLast;
            ok &= selectionKept;
            std::printf("%s UI dragging from inside the selection keeps it\n", selectionKept ? "PASS" : "FAIL");
            // Los extremos de la selección se arrastran y el puntero lo anuncia.
            doc->wave->anchor = 22050; doc->wave->cursor = 66150; doc->wave->update();
            const int handleStartX = doc->wave->pixelAt(22050), handleEndX = doc->wave->pixelAt(66150);
            const auto waveCursorShape = [doc] { return static_cast<QWidget *>(doc->wave)->cursor().shape(); };
            const int waveMiddleY = doc->wave->height() / 2;
            sendMouse(doc->wave, QEvent::MouseMove, QPoint(handleEndX, waveMiddleY), Qt::NoButton);
            const bool edgeCursor = waveCursorShape() == Qt::SplitHCursor;
            sendMouse(doc->wave, QEvent::MouseMove, QPoint(handleEndX - 80, waveMiddleY), Qt::NoButton);
            const bool plainCursor = waveCursorShape() != Qt::SplitHCursor;
            sendMouse(doc->wave, QEvent::MouseButtonPress, QPoint(handleEndX, waveMiddleY), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseMove, QPoint(handleEndX + 60, waveMiddleY), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseButtonRelease, QPoint(handleEndX + 60, waveMiddleY), Qt::NoButton);
            const qint64 extendedEnd = doc->wave->last();
            const bool extended = extendedEnd > 66150 && doc->wave->first() == 22050;
            sendMouse(doc->wave, QEvent::MouseButtonPress, QPoint(handleStartX, waveMiddleY), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseMove, QPoint(handleStartX - 50, waveMiddleY), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseButtonRelease, QPoint(handleStartX - 50, waveMiddleY), Qt::NoButton);
            const bool movedStart = doc->wave->first() < 22050 && doc->wave->last() == extendedEnd;
            ok &= edgeCursor && plainCursor && extended && movedStart;
            std::printf("%s UI selection edges resize with the split cursor\n", (edgeCursor && plainCursor && extended && movedStart) ? "PASS" : "FAIL");
            if (!(edgeCursor && plainCursor && extended && movedStart))
                std::printf("Edge detail: cursor %d plain %d extended %d(start %lld end %lld) movedStart %d(first %lld last %lld handleStart %d handleEnd %d)\n",
                            int(edgeCursor), int(plainCursor), int(extended), (long long)22050, (long long)extendedEnd, int(movedStart),
                            (long long)doc->wave->first(), (long long)doc->wave->last(), handleStartX, handleEndX);
            // Arrastrar la selección fuera de la onda abre un documento con el fragmento.
            doc->wave->anchor = 22050; doc->wave->cursor = 66150; doc->wave->update();
            const qint64 fragmentFrames = doc->wave->last() - doc->wave->first();
            const int windowsBeforeCopy = workspace->subWindowList().size();
            const int insideX = Theme::GutterWidth + int(double(44100 - doc->wave->start) / doc->wave->visibleFrames() * (doc->wave->width() - Theme::GutterWidth - 2));
            sendMouse(doc->wave, QEvent::MouseButtonPress, QPoint(insideX, doc->wave->height() / 2), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseMove, QPoint(-40, doc->wave->height() / 2), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseButtonRelease, QPoint(-40, doc->wave->height() / 2), Qt::NoButton);
            const bool copiedOut = workspace->subWindowList().size() == windowsBeforeCopy + 1;
            auto *fragment = copiedOut ? static_cast<Document *>(workspace->activeSubWindow()->widget()) : nullptr;
            bool nameUnique = fragment != nullptr;
            if (fragment) for (auto *window : workspace->subWindowList())
                if (window->widget() != fragment && static_cast<Document *>(window->widget())->name == fragment->name) nameUnique = false;
            const bool fragmentOk = fragment && fragment->data.frames() == fragmentFrames && fragment->data.rate == doc->data.rate
                                    && fragment->data.channels == doc->data.channels && doc->data.frames() == 132300
                                    && fragment->name.startsWith("Sin título ") && nameUnique;
            ok &= copiedOut && fragmentOk;
            std::printf("%s UI drag selection out opens a document with the fragment\n", (copiedOut && fragmentOk) ? "PASS" : "FAIL");
            if (fragment) {
                workspace->closeActiveSubWindow();
                QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                workspace->setActiveSubWindow(audioWindow);
            }
            // Soltar la selección sobre otro módulo la inserta en su documento.
            blank1->setGeometry(700, 60, 700, 210); blank1->raise(); app.processEvents();
            auto *receiver = static_cast<Document *>(blank1->widget());
            const qint64 receiverBefore = receiver->data.frames();
            const int windowsBeforeInsert = workspace->subWindowList().size();
            const QPoint dropGlobal = receiver->wave->mapToGlobal(QPoint(120, 90));
            sendMouse(doc->wave, QEvent::MouseButtonPress, QPoint(insideX, doc->wave->height() / 2), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseMove, doc->wave->mapFromGlobal(dropGlobal), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseButtonRelease, doc->wave->mapFromGlobal(dropGlobal), Qt::NoButton);
            const bool inserted = receiver->data.frames() == receiverBefore + fragmentFrames
                                  && workspace->subWindowList().size() == windowsBeforeInsert && doc->data.frames() == 132300;
            if (!inserted) {
                const QPoint local = workspace->viewport()->mapFromGlobal(dropGlobal);
                QWidget *hit = workspace->viewport()->childAt(local);
                std::printf("Insert detail: hit %s/%s receiver frames %lld (expected %lld) windows %d (expected %d) source %lld\n",
                            hit ? hit->metaObject()->className() : "none", hit ? qUtf8Printable(hit->objectName()) : "",
                            (long long)receiver->data.frames(), (long long)(receiverBefore + fragmentFrames),
                            int(workspace->subWindowList().size()), int(windowsBeforeInsert), (long long)doc->data.frames());
            }
            ok &= inserted;
            std::printf("%s UI drag selection onto another module inserts it there\n", inserted ? "PASS" : "FAIL");
            if (inserted) { workspace->setActiveSubWindow(blank1); trigger("&Deshacer"); }
            // Copiar en un módulo y pegar en otro.
            workspace->setActiveSubWindow(audioWindow);
            doc->wave->anchor = 0; doc->wave->cursor = 44100; doc->wave->update();
            trigger("&Copiar");
            workspace->setActiveSubWindow(blank2);
            auto *pastedDoc = static_cast<Document *>(blank2->widget());
            trigger("&Pegar");
            const bool pastedAcross = pastedDoc->data.frames() == 44100 && pastedDoc->data.rate == doc->data.rate && doc->data.frames() == 132300;
            ok &= pastedAcross;
            std::printf("%s UI copy in one module and paste in another\n", pastedAcross ? "PASS" : "FAIL");
            if (pastedAcross) trigger("&Deshacer");
            // Cortar en un módulo y pegar en otro.
            workspace->setActiveSubWindow(audioWindow);
            doc->wave->anchor = 0; doc->wave->cursor = 44100; doc->wave->update();
            trigger("Cor&tar");
            const bool cutSource = doc->data.frames() == 132300 - 44100;
            workspace->setActiveSubWindow(blank2);
            trigger("&Pegar");
            const bool cutPasted = cutSource && static_cast<Document *>(blank2->widget())->data.frames() == 44100;
            ok &= cutPasted;
            std::printf("%s UI cut in one module and paste in another\n", cutPasted ? "PASS" : "FAIL");
            if (cutPasted) { trigger("&Deshacer"); workspace->setActiveSubWindow(audioWindow); trigger("&Deshacer"); }
            workspace->setActiveSubWindow(audioWindow);
            // Menú contextual del área de trabajo con las acciones de Edición.
            QContextMenuEvent viewportContext(QContextMenuEvent::Mouse, QPoint(30, 30), workspace->viewport()->mapToGlobal(QPoint(30, 30)));
            QApplication::sendEvent(workspace->viewport(), &viewportContext);
            app.processEvents();
            auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QStringList entries;
            if (popup) for (auto *action : popup->actions()) if (!action->isSeparator()) entries << action->text();
            const bool contextMenuOk = popup && entries.contains("&Deshacer") && entries.contains("Cor&tar") && entries.contains("&Copiar")
                                       && entries.contains("&Pegar") && entries.contains("Seleccionar &todo");
            if (!contextMenuOk) {
                auto *found = window.findChild<QMenu *>("editContextMenu");
                std::printf("Context detail: popup %p found %p policy %d entries [%s]\n", static_cast<void *>(popup), static_cast<void *>(found),
                            int(workspace->viewport()->contextMenuPolicy()), qUtf8Printable(entries.join('|')));
            }
            ok &= contextMenuOk;
            std::printf("%s UI context menu with the edit actions\n", contextMenuOk ? "PASS" : "FAIL");
            if (popup) popup->close();
            app.processEvents();
            blank1->setGeometry(100, 90, 1120, 340); blank2->setGeometry(45, 370, 1120, 280);
            audioWindow->setGeometry(210, 245, 1150, 300); workspace->setActiveSubWindow(audioWindow); audioWindow->raise();
            app.processEvents();
            // Captura del módulo minimizado en el pie del área de trabajo.
            auto *minimizedDoc = static_cast<Document *>(blank2->widget());
            minimizedDoc->setMinimized(true);
            app.processEvents();
            ok &= window.grab().save(QApplication::applicationDirPath() + "/../build/minimized.png");
            minimizedDoc->setMinimized(false);
            app.processEvents();
            ok &= window.grab().save(QApplication::applicationDirPath() + "/../build/preview.png");
            for (auto *action : window.menuBar()->actions()) if (action->text() == "&Proceso" || action->text() == "&Efectos") {
                auto *menu = action->menu(); menu->popup(window.mapToGlobal(QPoint(170, 20))); app.processEvents();
                menu->grab().save(QApplication::applicationDirPath() + (action->text() == "&Proceso" ? "/../build/process-menu.png" : "/../build/effects-menu.png")); menu->hide();
            }
            doc->wave->zoomBy(4); ok &= doc->scroll->maximum() > 0;
            doc->scroll->setValue(50000); ok &= doc->wave->start > 0;
            workspace->setActiveSubWindow(blank1); ok &= workspace->activeSubWindow() == blank1;
            doc->undo.setClean();
            workspace->closeAllSubWindows(); QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); app.processEvents();
            ok &= workspace->subWindowList().isEmpty();
            trigger("&Nuevo"); responseMode = 0; effectResponder.start(); runEffect("insert_silence"); effectResponder.stop();
            auto *fresh = static_cast<Document *>(workspace->currentSubWindow()->widget());
            bool emptyInsert = fresh->data.frames() == 44100 && window.findChild<QMenu *>("effectsMenu")->isEnabled();
            trigger("&Deshacer"); emptyInsert &= fresh->data.frames() == 0 && !window.findChild<QMenu *>("effectsMenu")->isEnabled();
            QMimeData backgroundMime; backgroundMime.setUrls({QUrl::fromLocalFile(path)});
            QDragEnterEvent backgroundEnter(QPoint(10, 10), Qt::CopyAction, &backgroundMime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(workspace->viewport(), &backgroundEnter);
            QDropEvent backgroundDrop(QPointF(10, 10), Qt::CopyAction, &backgroundMime, Qt::LeftButton, Qt::NoModifier); QApplication::sendEvent(workspace->viewport(), &backgroundDrop); app.processEvents();
            bool background = backgroundDrop.isAccepted() && workspace->subWindowList().size() == 2 && fresh->data.frames() == 0;
            ok &= emptyInsert && background;
            workspace->closeAllSubWindows(); QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); app.processEvents();
            std::printf("%s UI empty-document insert/undo/menu state; %s UI background drop preserves empty window\n", emptyInsert ? "PASS" : "FAIL", background ? "PASS" : "FAIL");
            ok &= recordingUiTests() == 0;
            std::printf("%s UI: editing, MDI activation, cascade, tile, zoom, scroll, recording, close and render\n", ok ? "PASS" : "FAIL"); std::fflush(stdout);
            app.exit(ok ? 0 : 1);
        });
        return app.exec();
    }
    for (int i = 1; i < app.arguments().size(); ++i) window.openFile(app.arguments()[i]);
    return app.exec();
}
