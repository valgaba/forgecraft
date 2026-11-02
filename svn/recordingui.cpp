#include "mainwindow.h"
#include "recorddialog.h"
#include <QMessageBox>

void MainWindow::recordAudio() {
    stop();
    auto *target = current();
    const AudioData destination = target ? target->data : AudioData{};
    RecordDialog dialog(destination, target ? target->wave->first() : 0, target ? target->wave->last() : 0, this, recordingSource ? recordingSource() : nullptr);
    // Cada toma terminada va directamente al documento enfocado; si ya tiene
    // audio, se añade al final y queda como una operación de deshacer.
    dialog.applyTake = [this, &dialog, target](const AudioData &take) {
        Document *document = target;
        if (!document) {
            AudioData empty; empty.rate = take.rate; empty.channels = take.channels; empty.bitDepth = take.bitDepth;
            addDocument(empty);
            document = current();
            if (document) document->name = "Grabación " + QString::number(++recordingNumber);
        }
        if (!document || !take.frames()) return false;
        const qint64 end = document->data.frames();
        auto result = Recording::merge(document->data, take, Recording::Insert, end, end);
        if (!result.error.isEmpty()) { QMessageBox::warning(&dialog, "Añadir grabación", result.error); return false; }
        commit(document, result.audio, "Grabar audio", result.first, result.last);
        document->wave->anchor = result.first;
        document->wave->cursor = result.last;
        document->wave->start = qMax(qint64(0), result.last - document->wave->visibleFrames());
        document->wave->update();
        return true;
    };
    dialog.exec();
}
