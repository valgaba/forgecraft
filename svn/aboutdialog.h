#pragma once
#include <QDialog>

// Ventana «Acerca de» con la disposición de Radit: descripción y autor a la
// izquierda, logotipo y versión en el centro y créditos a la derecha.
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
protected:
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
};
