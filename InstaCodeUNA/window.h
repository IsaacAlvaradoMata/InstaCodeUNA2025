#ifndef WINDOW_H
#define WINDOW_H

#include <QMainWindow>
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QString>
#include <QLabel>

class Window : public QMainWindow {
    Q_OBJECT

public:
    Window(QWidget *parent = nullptr);
    ~Window();

private slots:
    void loadFile();         // Cargar archivo .txt
    void convertToCpp();     // Convertir texto a código C++
    void exportCppFile();    // Exportar código a archivo .cpp

private:
    // Elementos de la interfaz
    QTextEdit *inputTextEdit;     // Donde se muestra el .txt
    QTextEdit *outputTextEdit;    // Donde se muestra el código C++
    QPushButton *loadButton;      // Botón: Cargar .txt
    QPushButton *convertButton;   // Botón: Convertir a C++
    QPushButton *exportButton;    // Botón: Exportar .cpp

    void setupUI();               // Método privado para construir la interfaz
};

#endif // WINDOW_H
