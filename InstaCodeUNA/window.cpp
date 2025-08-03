#include "window.h"
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QLabel>
#include <QSpacerItem>

Window::Window(QWidget *parent)
    : QMainWindow(parent) {
    setupUI();
}

Window::~Window() {}

void Window::setupUI() {
    // --- Título ---
    QLabel *titleLabel = new QLabel("🧠 InstaCodeUNA");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 24px; font-weight: bold; padding: 10px;");

    // --- Widgets de texto ---
    inputTextEdit = new QTextEdit;
    outputTextEdit = new QTextEdit;
    inputTextEdit->setPlaceholderText("📄 Aquí aparecerá el contenido del archivo .txt");
    outputTextEdit->setPlaceholderText("💻 Aquí se generará el código C++");

    // --- Estilo texto ---
    QString editStyle = "padding: 10px; font-size: 14px;";
    inputTextEdit->setStyleSheet(editStyle);
    outputTextEdit->setStyleSheet(editStyle);

    // --- Botones ---
    loadButton = new QPushButton("📂 Cargar archivo .txt");
    convertButton = new QPushButton("⚙️ Convertir a C++");
    exportButton = new QPushButton("⬇️ Exportar archivo .cpp");

    QString buttonStyle = "padding: 8px; font-size: 14px;";
    loadButton->setStyleSheet(buttonStyle);
    convertButton->setStyleSheet(buttonStyle);
    exportButton->setStyleSheet(buttonStyle);

    // --- Layout izquierdo (texto + botones) ---
    QVBoxLayout *leftLayout = new QVBoxLayout;
    leftLayout->addWidget(inputTextEdit);
    leftLayout->addWidget(loadButton);
    leftLayout->addWidget(convertButton);

    // --- Layout derecho (texto + botón) ---
    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->addWidget(outputTextEdit);
    rightLayout->addWidget(exportButton);

    // --- Layout horizontal central ---
    QHBoxLayout *centerLayout = new QHBoxLayout;
    centerLayout->addLayout(leftLayout);
    centerLayout->addLayout(rightLayout);

    // --- Layout principal ---
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(titleLabel);
    mainLayout->addLayout(centerLayout);

    // --- Central widget ---
    QWidget *centralWidget = new QWidget(this);
    centralWidget->setLayout(mainLayout);
    setCentralWidget(centralWidget);

    // --- Configuración de ventana ---
    setWindowTitle("InstaCodeUNA - Convertidor de Lenguaje Natural a C++");
    resize(900, 600);

    // --- Conectar botones ---
    connect(loadButton, &QPushButton::clicked, this, &Window::loadFile);
    connect(convertButton, &QPushButton::clicked, this, &Window::convertToCpp);
    connect(exportButton, &QPushButton::clicked, this, &Window::exportCppFile);
}

void Window::loadFile() {
    QString fileName = QFileDialog::getOpenFileName(this, "Seleccionar archivo de texto", "", "Text Files (*.txt)");
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            inputTextEdit->setPlainText(in.readAll());
        } else {
            QMessageBox::warning(this, "Error", "No se pudo abrir el archivo.");
        }
    }
}

void Window::convertToCpp() {
    QString input = inputTextEdit->toPlainText();
    // Por ahora solo se copia con encabezado
    QString output = "// Código C++ generado por InstaCodeUNA\n\n" + input;
    outputTextEdit->setPlainText(output);
}

void Window::exportCppFile() {
    QString fileName = QFileDialog::getSaveFileName(this, "Guardar archivo como", "codigo_generado.cpp", "C++ Files (*.cpp)");
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << outputTextEdit->toPlainText();
            file.close();
            QMessageBox::information(this, "Éxito", "Archivo exportado correctamente.");
        } else {
            QMessageBox::warning(this, "Error", "No se pudo guardar el archivo.");
        }
    }
}
