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
    void loadFile();
    void convertToCpp();
    void exportCppFile();
    void toggleTheme(); // <-- DECLARACIÓN DEL NUEVO SLOT

private:
    void setupUI();
    void applyStylesheet(const QString &path); // <-- DECLARACIÓN DEL NUEVO MÉTODO

    // Widgets
    QTextEdit *inputTextEdit;
    QTextEdit *outputTextEdit;
    QPushButton *loadButton;
    QPushButton *convertButton;
    QPushButton *exportButton;
    QPushButton *themeButton; // <-- DECLARACIÓN DEL NUEVO BOTÓN

    // Variables de estado
    bool isDarkTheme;         // <-- DECLARACIÓN DEL FLAG DE TEMA
};

#endif // WINDOW_H
