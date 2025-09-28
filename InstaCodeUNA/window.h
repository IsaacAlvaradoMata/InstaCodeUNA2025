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
#include <QTimer>

class Window : public QMainWindow {
    Q_OBJECT

public:
    Window(QWidget *parent = nullptr);
    ~Window();

    enum class AlertType {Info, Success, Warning, Error};

private slots:
    void loadFile();
    void loadDataFile();
    void convertToCpp();
    void exportCppFile();
    void toggleTheme(); // <-- DECLARACIÓN DEL NUEVO SLOT

    void showAlert(AlertType type,
                   const QString& title,
                   const QString& message,
                   int autoCloseMs = 0);
    void hideAlert();

protected:
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void buildAlertOverlay();
    void applyAlertStyle(AlertType type);

    void setupUI();
    void applyStylesheet(const QString &path); // <-- DECLARACIÓN DEL NUEVO MÉTODO

    // Widgets
    QTextEdit *inputTextEdit;
    QTextEdit *outputTextEdit;
    QPushButton *loadButton;
    QPushButton *dataButton;
    QPushButton *convertButton;
    QPushButton *exportButton;
    QPushButton *themeButton; // <-- DECLARACIÓN DEL NUEVO BOTÓN

    // Variables de estado
    bool isDarkTheme;         // <-- DECLARACIÓN DEL FLAG DE TEMA
    QString dataFilePath;
    QString dataFileContents;

    QWidget     *m_alertOverlay  = nullptr; // capa semitransparente (bloquea clicks)
    QWidget     *m_alertCard     = nullptr; // el “card” centrado
    QLabel      *m_alertIcon     = nullptr;
    QLabel      *m_alertTitle    = nullptr;
    QLabel      *m_alertMessage  = nullptr;
    QPushButton *m_alertClose    = nullptr;
    QTimer       m_alertTimer;    
};

#endif // WINDOW_H
