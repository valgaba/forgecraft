#include "mainwindow.h"
#include "recorddialog.h"
#include <QMessageBox>

void MainWindow::recordAudio() {
    stop(); auto *target = current();
    const AudioData original = target ? target->data : AudioData{};
    qint64 first = target ? target->wave->first() : 0, last = target ? target->wave->last() : 0;
    RecordDialog dialog(original, first, last, this, recordingSource ? recordingSource() : nullptr);
    if (dialog.exec() != QDialog::Accepted) return;
    auto take = dialog.take();
    auto result = Recording::merge(original, take, dialog.mode(), dialog.position(), dialog.selectionEnd());
    bool newWindow = dialog.mode() == Recording::NewDocument || !target;
    if (!result.error.isEmpty()) {
        const QString reason = result.error;
        result = Recording::merge(original, take, Recording::NewDocument, 0, 0); newWindow = true;
        if (!result.error.isEmpty()) { QMessageBox::warning(this, "Añadir grabación", result.error); return; }
        QMessageBox::information(this, "Toma conservada", reason + "\nLa toma se abrirá en una ventana nueva.");
    }
    if (newWindow) {
        AudioData empty; empty.rate = take.rate; empty.channels = take.channels;
        addDocument(empty); target = current(); target->name = "Grabación " + QString::number(++recordingNumber);
    }
    commit(target, result.audio, "Grabar audio", result.first, result.last);
    target->wave->zoom = 1; target->wave->start = 0; target->wave->update();
}
