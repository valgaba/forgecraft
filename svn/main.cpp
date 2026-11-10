#include "mainwindow.h"
#include "dsp.h"
#include "aboutdialog.h"
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
#include <QComboBox>
#include <QSlider>
#include <QTimer>
#include <QPoint>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>
#include <QProgressDialog>
#include <QMenuBar>
#include <QMenu>
#include <cmath>
#include <cstdio>

int dspTests();
int audioFormatTests();
int sampleBufferTests();
int largeAudioTests();
int largeAudioUiTests();
int audioFormatUiTests();
int recordingTests();
int recordingScopeTests();
int inputMonitoringTests();
int normalizerDialogTests();
int previewLoopTests();
int processPreviewTests();
int processPresetTests();
int loadingProgressTests();
int loadingUiTests();
int audioBenchmark();
int playbackTests();

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
    // 24 bits y 32 float: el WAV se escribe en la profundidad del documento.
    AudioData hi = a; hi.bitDepth = 24; hi.floatFormat = false;
    AudioData hiBack; bool ok24 = hi.write(temp.filePath("pcm24.wav"), error) && AudioData::read(temp.filePath("pcm24.wav"), hiBack, error)
        && hiBack.bitDepth == 24 && !hiBack.floatFormat && hiBack.samples.size() == hi.samples.size();
    double delta24 = 0; if (ok24) for (qsizetype i = 0; i < hiBack.samples.size(); ++i) delta24 = qMax(delta24, double(std::abs(hiBack.samples[i] - hi.samples[i])));
    check(ok24 && delta24 < 1e-6, "PCM24 round trip precision");
    AudioData fl = a; fl.bitDepth = 32; fl.floatFormat = true;
    AudioData flBack; bool okf = fl.write(temp.filePath("float32.wav"), error) && AudioData::read(temp.filePath("float32.wav"), flBack, error)
        && flBack.floatFormat && flBack.bitDepth == 32 && flBack.samples == fl.samples;
    check(okf, "32-bit float WAV round trip");
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
    failures += sampleBufferTests();
    failures += largeAudioUiTests();
    failures += audioFormatTests();
    failures += dspTests();
    failures += recordingTests();
    failures += recordingScopeTests();
    failures += inputMonitoringTests();
    failures += normalizerDialogTests();
    failures += previewLoopTests();
    failures += processPreviewTests();
    failures += processPresetTests();
    failures += loadingProgressTests();
    failures += loadingUiTests();
    failures += playbackTests();
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

class ForgeApplication : public QApplication {
public:
    using QApplication::QApplication;
    bool notify(QObject *receiver, QEvent *event) override {
        try { return QApplication::notify(receiver, event); }
        catch (const std::exception &error) { QMessageBox::warning(activeWindow(), "Almacenamiento de audio", QString::fromUtf8(error.what())); return false; }
    }
};
int main(int argc, char **argv) {
    ForgeApplication app(argc, argv); app.setApplicationName("ForgeCraft"); app.setApplicationVersion("0.1.7"); app.setOrganizationName("ForgeCraft");
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/ForgeCraftAppIcon.png")));
    applyTheme(app);
    if (app.arguments().contains("--benchmark-audio")) return audioBenchmark();
    if (app.arguments().contains("--self-test")) return selfTest();
    if (app.arguments().contains("--large-audio-test")) {
        const int failures = largeAudioTests() + largeAudioUiTests();
        std::printf("%d failure(s)\n", failures); return failures ? 1 : 0;
    }
    if (app.arguments().contains("--mute-test")) {
        const int failures = inputMonitoringTests() + previewLoopTests() + playbackTests();
        std::printf("%d failure(s)\n", failures); return failures ? 1 : 0;
    }
    if (app.arguments().contains("--smoke-test")) app.setAttribute(Qt::AA_DontUseNativeDialogs);
    MainWindow window({}, !app.arguments().contains("--smoke-test")); window.show();
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
            bool capturedDialog = false, capturedEq = false, previewControlsChecked = false, previewControlsOk = true, previewMetersOk = true;
            QObject::connect(&effectResponder, &QTimer::timeout, &window, [&] {
                auto *modal = QApplication::activeModalWidget(); if (!modal) return;
                if (auto *progress = qobject_cast<QProgressDialog *>(modal)) {
                    if (responseMode == 2) for (auto *button : progress->findChildren<QPushButton *>()) if (button->text().contains("Cancelar")) { canceledWork = true; button->click(); break; }
                    return;
                }
                if (auto *dialog = qobject_cast<QDialog *>(modal)) {
                    if (dialog->objectName() == "processDialog") {
                        if (!dialog->property("normalizer").toBool()) for (const auto &op : Dsp::operations()) if (op.title == dialog->windowTitle()) {
                            previewMetersOk &= bool(dialog->findChild<VuMeter *>("processPreviewMeters")) == op.previewAvailable;
                            previewMetersOk &= bool(dialog->findChild<QPushButton *>("processPreview")) == op.previewAvailable;
                        }
                        if (dialog->property("normalizer").toBool()) dialog->grab().save(QApplication::applicationDirPath() + "/../build/normalize-dialog.png");
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
            ok &= previewMetersOk;
            std::printf("%s UI process and effect preview meters follow operation availability\n", previewMetersOk ? "PASS" : "FAIL");
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
            // «Nuevo» abre el cuadro de formato: se acepta con los valores de omisión.
            auto newDocument = [&](int rate, int channels, int depth) {
                QTimer::singleShot(0, &window, [&, rate, channels, depth] {
                    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    if (!dialog) return;
                    dialog->grab().save(QApplication::applicationDirPath() + "/../build/new-document.png");
                    if (auto *box = dialog->findChild<QComboBox *>("newRate")) box->setCurrentIndex(box->findData(rate));
                    if (auto *radio = dialog->findChild<QRadioButton *>(QString("newDepth%1").arg(depth))) radio->setChecked(true);
                    if (auto *radio = dialog->findChild<QRadioButton *>(QString("newChannels%1").arg(channels))) radio->setChecked(true);
                    dialog->accept();
                });
                trigger("&Nuevo");
            };
            newDocument(44100, 2, 16); auto *blank1 = workspace->activeSubWindow();
            newDocument(48000, 1, 24); auto *blank2 = workspace->activeSubWindow();
            bool formatsOk = blank1 && blank2
                && static_cast<Document *>(blank1->widget())->data.rate == 44100
                && static_cast<Document *>(blank1->widget())->data.bitDepth == 16
                && static_cast<Document *>(blank2->widget())->data.rate == 48000
                && static_cast<Document *>(blank2->widget())->data.channels == 1
                && static_cast<Document *>(blank2->widget())->data.bitDepth == 24;
            ok &= formatsOk;
            std::printf("%s UI new document dialog sets rate, channels and bit depth\n", formatsOk ? "PASS" : "FAIL");
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
            // Seguimiento por MDI: la barra común controla solo el enfocado.
            auto *followAction = window.findChild<QAction *>("followPlaybackAction");
            workspace->setActiveSubWindow(audioWindow);
            const double previousZoom = doc->wave->zoom;
            const qint64 previousStart = doc->wave->start;
            doc->wave->zoom = 4; doc->wave->start = 0;
            const qint64 viewFrames = doc->wave->visibleFrames();
            doc->wave->setPlayhead(viewFrames);
            const int centeredX = doc->wave->pixelAt(viewFrames);
            const int waveCenter = Theme::GutterWidth + (doc->wave->width() - Theme::GutterWidth - 2) / 2;
            bool followed = qAbs(centeredX - waveCenter) <= 1 && followAction && followAction->isChecked();
            doc->wave->setPlayhead(viewFrames + 100);
            followed &= doc->wave->start == viewFrames + 100 - viewFrames / 2
                && doc->wave->pixelAt(viewFrames + 100) == centeredX;
            if (followAction) followAction->trigger();
            const qint64 fixedStart = doc->wave->start;
            doc->wave->setPlayhead(doc->data.frames());
            bool fixedView = !doc->wave->followPlayback && !doc->followButton->isChecked()
                && doc->wave->start == fixedStart && doc->wave->playhead == doc->data.frames();
            auto *otherDoc = static_cast<Document *>(blank1->widget());
            workspace->setActiveSubWindow(blank1);
            bool independentFollow = followAction && followAction->isChecked() && otherDoc->wave->followPlayback;
            if (followAction) followAction->trigger();
            workspace->setActiveSubWindow(audioWindow);
            independentFollow &= !followAction->isChecked() && !doc->wave->followPlayback && !otherDoc->wave->followPlayback;
            doc->followButton->click();
            followed &= followAction->isChecked() && doc->wave->followPlayback
                && doc->wave->start == doc->data.frames() - viewFrames / 2
                && doc->wave->pixelAt(doc->data.frames()) == centeredX;
            doc->wave->setPlayhead(0);
            followed &= doc->wave->start == -viewFrames / 2 && doc->wave->pixelAt(0) == centeredX;
            ok &= followed && fixedView && independentFollow;
            std::printf("%s UI playback follow keeps the red line centered while scrolling, including both audio edges, and toggles per MDI\n", (followed && fixedView && independentFollow) ? "PASS" : "FAIL");
            doc->wave->zoom = previousZoom; doc->wave->start = previousStart;
            doc->wave->setPlayhead(-1); otherDoc->followButton->setChecked(true);
            auto *dropMixAction = window.findChild<QAction *>("dropMix");
            auto *dropInsertAction = window.findChild<QAction *>("dropInsert");
            const bool defaultMix = dropMixAction && dropInsertAction && dropMixAction->isChecked() && !dropInsertAction->isChecked();
            ok &= defaultMix;
            std::printf("%s UI selection drops default to mixing\n", defaultMix ? "PASS" : "FAIL");
            // La inserción dibuja únicamente una línea vertical; la mezcla muestra el tramo.
            Waveform preview;
            preview.audio = &doc->data; preview.resize(640, 240);
            const QImage cleanPreview = preview.grab().toImage();
            const qint64 previewFrame = 44100;
            const int insertionX = preview.pixelAt(previewFrame);
            preview.setDropPreview(doc->data.samples.constData(), doc->data.channels, 22050, 66150, previewFrame, false);
            const QImage insertPreview = preview.grab().toImage();
            bool lineOnly = true;
            int linePixels = 0;
            for (int y = 0; y < cleanPreview.height(); ++y) for (int x = 0; x < cleanPreview.width(); ++x) {
                if (cleanPreview.pixel(x, y) == insertPreview.pixel(x, y)) continue;
                if (qAbs(x - insertionX) > 1) lineOnly = false;
                ++linePixels;
            }
            lineOnly &= linePixels >= preview.height() - 4;
            preview.clearDropPreview();
            const bool clearedLine = preview.grab().toImage() == cleanPreview;
            preview.setDropPreview(doc->data.samples.constData(), doc->data.channels, 22050, 66150, previewFrame, true);
            const QImage mixPreview = preview.grab().toImage();
            int ghostPixels = 0;
            for (int y = 0; y < cleanPreview.height(); ++y) for (int x = insertionX + 2; x < cleanPreview.width(); ++x)
                if (cleanPreview.pixel(x, y) != mixPreview.pixel(x, y)) ++ghostPixels;
            insertPreview.save(QApplication::applicationDirPath() + "/../build/drop-insert-preview.png");
            mixPreview.save(QApplication::applicationDirPath() + "/../build/drop-mix-preview.png");
            const bool previewsOk = lineOnly && clearedLine && ghostPixels > 1000;
            ok &= previewsOk;
            std::printf("%s UI insert preview is a full-height line, mix preview is a ghost, and clearing removes the line\n", previewsOk ? "PASS" : "FAIL");
            // El centrado deja márgenes vacíos; no repite muestras fuera del audio.
            AudioData centerAudio; centerAudio.samples.fill(.5f, 2000);
            Waveform centerPreview; centerPreview.audio = &centerAudio; centerPreview.resize(640, 240);
            auto hasWaveAt = [&](const QImage &image, int x) {
                for (int y = Theme::RulerHeight + 1; y < image.height() - 2; ++y)
                    if (image.pixelColor(x, y) == Theme::Accent) return true;
                return false;
            };
            centerPreview.setPlayhead(0);
            const QImage startCentered = centerPreview.grab().toImage();
            centerPreview.setPlayhead(centerAudio.frames());
            const QImage endCentered = centerPreview.grab().toImage();
            const bool blankMargins = !hasWaveAt(startCentered, 100) && hasWaveAt(startCentered, 540)
                && hasWaveAt(endCentered, 100) && !hasWaveAt(endCentered, 540);
            startCentered.save(QApplication::applicationDirPath() + "/../build/follow-centered-start.png");
            endCentered.save(QApplication::applicationDirPath() + "/../build/follow-centered-end.png");
            ok &= blankMargins;
            std::printf("%s UI centered playback renders empty margins outside the audio\n", blankMargins ? "PASS" : "FAIL");
            blank1->setGeometry(700, 60, 700, 210); blank1->raise(); app.processEvents();
            auto *receiver = static_cast<Document *>(blank1->widget());
            const qint64 receiverBefore = receiver->data.frames();
            const int windowsBeforeInsert = workspace->subWindowList().size();
            const QPoint dropGlobal = receiver->wave->mapToGlobal(QPoint(120, 90));
            sendMouse(doc->wave, QEvent::MouseButtonPress, QPoint(insideX, doc->wave->height() / 2), Qt::LeftButton);
            sendMouse(doc->wave, QEvent::MouseMove, doc->wave->mapFromGlobal(dropGlobal), Qt::LeftButton);
            const auto rightClickDuringDrag = [&](QEvent::Type type = QEvent::MouseButtonPress) {
                const QPoint local = doc->wave->mapFromGlobal(dropGlobal);
                QMouseEvent press(type, local, dropGlobal, Qt::RightButton, Qt::LeftButton | Qt::RightButton, Qt::NoModifier);
                QApplication::sendEvent(doc->wave, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, local, dropGlobal, Qt::RightButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(doc->wave, &release);
            };
            rightClickDuringDrag();
            bool toggledMode = dropInsertAction && dropInsertAction->isChecked() && !dropMixAction->isChecked();
            rightClickDuringDrag(QEvent::MouseButtonDblClick);
            toggledMode &= dropMixAction && dropMixAction->isChecked() && !dropInsertAction->isChecked();
            rightClickDuringDrag();
            toggledMode &= dropInsertAction && dropInsertAction->isChecked() && doc->wave->selectionDragActive()
                && doc->wave->last() - doc->wave->first() == fragmentFrames;
            int dragContextMenus = 0;
            const auto originalContextMenu = doc->contextMenu;
            doc->contextMenu = [&](const QPoint &) { ++dragContextMenus; };
            QContextMenuEvent dragContext(QContextMenuEvent::Mouse, doc->wave->mapFromGlobal(dropGlobal), dropGlobal);
            QApplication::sendEvent(doc->wave, &dragContext);
            doc->contextMenu = originalContextMenu;
            toggledMode &= dragContextMenus == 0;
            ok &= toggledMode;
            std::printf("%s UI right clicks during a left-button drag toggle mix/insert without losing the selection or opening a menu\n", toggledMode ? "PASS" : "FAIL");
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
            // Modo «Mezclar»: cada suelta abre el cuadro de mezcla, que la prueba
            // acepta sola. El punto de destino se recalcula en cada suelta porque
            // activar el módulo puede cambiar su geometría.
            for (auto *action : window.findChildren<QAction *>()) if (action->objectName() == "dropMix") action->trigger();
            int mixMode = 0;   // 0: volúmenes a 0 dB; 1: fragmento a -6 dB
            QTimer mixResponder; mixResponder.setInterval(10);
            QObject::connect(&mixResponder, &QTimer::timeout, &window, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog || dialog->objectName() != "mixDialog") return;
                dialog->grab().save(QApplication::applicationDirPath() + "/../build/mix-dialog.png");
                if (mixMode == 1) if (auto *slider = dialog->findChild<QSlider *>("mixSource")) slider->setValue(-60);
                dialog->accept();
            });
            doc->wave->anchor = 0; doc->wave->cursor = 44100; doc->wave->update();
            const int mixPressX = Theme::GutterWidth + 10;
            auto dropSelection = [&](const QPoint &at) {
                sendMouse(doc->wave, QEvent::MouseButtonPress, QPoint(mixPressX, doc->wave->height() / 2), Qt::LeftButton);
                sendMouse(doc->wave, QEvent::MouseMove, doc->wave->mapFromGlobal(at), Qt::LeftButton);
                sendMouse(doc->wave, QEvent::MouseButtonRelease, doc->wave->mapFromGlobal(at), Qt::NoButton);
            };
            auto mixDropPoint = [&] { return receiver->wave->mapToGlobal(QPoint(120, 90)); };
            auto mixPosition = [&](const QPoint &point) { return receiver->wave->frameAt(receiver->wave->mapFromGlobal(point).x()); };
            mixResponder.start();
            blank1->raise(); app.processEvents();
            const qint64 fragmentFrames2 = 44100, channels2 = doc->data.channels;
            QPoint mixDrop = mixDropPoint();
            const qint64 firstAt = mixPosition(mixDrop);
            dropSelection(mixDrop);
            const bool mixedOnce = firstAt == 0 && receiver->data.frames() == fragmentFrames2
                && receiver->data.samples == doc->data.samples.mid(0, fragmentFrames2 * channels2);
            mixDrop = mixDropPoint();
            const qint64 blendAt = mixPosition(mixDrop);
            const AudioData beforeBlend = receiver->data;
            dropSelection(mixDrop);
            bool blended = receiver->data.frames() == qMax(beforeBlend.frames(), blendAt + fragmentFrames2);
            for (qint64 frame = blendAt; blended && frame < qMin(blendAt + fragmentFrames2, receiver->data.frames()); frame += 173) {
                for (qsizetype channel = 0; channel < channels2 && blended; ++channel) {
                    const float previous = frame < beforeBlend.frames() ? beforeBlend.samples[frame * channels2 + channel] : 0.0f;
                    const float expected = float(std::clamp(double(previous) + double(doc->data.samples[(frame - blendAt) * channels2 + channel]), -1.0, 1.0));
                    blended = std::abs(receiver->data.samples[frame * channels2 + channel] - expected) < 1e-6f;
                }
            }
            ok &= mixedOnce && blended;
            std::printf("%s UI mix dialog mixes with equal volumes\n", (mixedOnce && blended) ? "PASS" : "FAIL");
            // Fragmento a -6 dB: la suma se pondera y el destino se mantiene.
            mixDrop = mixDropPoint();
            const qint64 gainAt = mixPosition(mixDrop);
            const AudioData beforeGain = receiver->data;
            mixMode = 1;
            dropSelection(mixDrop);
            mixResponder.stop();
            const double sourceGain = std::pow(10.0, -6.0 / 20.0);
            bool weighted = receiver->data.frames() == qMax(beforeGain.frames(), gainAt + fragmentFrames2);
            for (qint64 frame = gainAt; weighted && frame < qMin(gainAt + fragmentFrames2, receiver->data.frames()); frame += 173) {
                for (qsizetype channel = 0; channel < channels2 && weighted; ++channel) {
                    const float previous = frame < beforeGain.frames() ? beforeGain.samples[frame * channels2 + channel] : 0.0f;
                    const float expected = float(std::clamp(double(previous) + double(doc->data.samples[(frame - gainAt) * channels2 + channel]) * sourceGain, -1.0, 1.0));
                    weighted = std::abs(receiver->data.samples[frame * channels2 + channel] - expected) < 1e-6f;
                }
            }
            ok &= weighted;
            std::printf("%s UI mix dialog applies the fragment volume in decibels\n", weighted ? "PASS" : "FAIL");
            workspace->setActiveSubWindow(blank1);
            trigger("&Deshacer");
            const bool undoneBlend = receiver->data.samples == beforeGain.samples;
            trigger("&Deshacer"); trigger("&Deshacer");
            const bool undoneAll = receiver->data.frames() == 0;
            ok &= undoneBlend && undoneAll;
            std::printf("%s UI each mix drop is one undo step\n", (undoneBlend && undoneAll) ? "PASS" : "FAIL");
            for (auto *action : window.findChildren<QAction *>()) if (action->objectName() == "dropInsert") action->trigger();
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
            QTimer::singleShot(0, &window, [&] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->accept(); });
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
            ok &= loadingUiTests() == 0;
            // Ventana «Acerca de»: se abre, se captura y se cierra.
            ok &= !qApp->windowIcon().isNull();
            auto *about = new AboutDialog(&window);
            about->show(); app.processEvents();
            ok &= about->grab().save(QApplication::applicationDirPath() + "/../build/about.png");
            about->close(); app.processEvents();
            std::printf("%s UI: editing, MDI activation, cascade, tile, zoom, scroll, recording, close and render\n", ok ? "PASS" : "FAIL"); std::fflush(stdout);
            ok &= audioFormatUiTests() == 0;
            app.exit(ok ? 0 : 1);
        });
        return app.exec();
    }
    for (int i = 1; i < app.arguments().size(); ++i) window.openFile(app.arguments()[i]);
    return app.exec();
}
