#ifndef WINDOW_H
#define WINDOW_H

#include <QWidget>
#include <QTextEdit>

class QPushButton;

class ConverterWindow : public QWidget {
    Q_OBJECT

public:
    ConverterWindow(QWidget *parent = nullptr);

private slots:
    void loadFile();
    void convertToCpp();

private:
    QTextEdit *inputText;
    QTextEdit *outputText;
    QPushButton *loadButton;
    QPushButton *convertButton;
};

#endif // WINDOW_H
