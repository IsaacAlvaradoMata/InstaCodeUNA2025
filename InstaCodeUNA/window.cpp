#include "window.h"
#include <QApplication>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QLabel>
#include <QSpacerItem>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPixmap>
#include <QIcon> // <-- Añade esta inclusión



Window::Window(QWidget *parent)
    : QMainWindow(parent), isDarkTheme(false) { // Inicializa el flag
    setupUI();
}

Window::~Window() {}

void Window::setupUI() {
    // --- Layout del Título con imágenes ---
    QHBoxLayout *titleLayout = new QHBoxLayout();

    // Imagen izquierda
    QLabel *leftImageLabel = new QLabel;
    QPixmap appLogoPixmap(":/images/AppLogo.png");
    leftImageLabel->setPixmap(appLogoPixmap.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    leftImageLabel->setAlignment(Qt::AlignRight);

    // Texto del título
     QLabel *titleLabel = new QLabel("<font color='black'>InstaCode</font><font color='red'>UNA</font>");
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 24px; font-weight: bold; padding: 10px;");

    // Imagen derecha
    QLabel *rightImageLabel = new QLabel;
    rightImageLabel->setPixmap(appLogoPixmap.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    rightImageLabel->setAlignment(Qt::AlignLeft);

    // Añadir widgets al layout del título
    titleLayout->addStretch();
    titleLayout->addWidget(leftImageLabel);
    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(rightImageLabel);
    titleLayout->addStretch();

    // --- Widgets de texto ---
    inputTextEdit = new QTextEdit;
    outputTextEdit = new QTextEdit;
    inputTextEdit->setPlaceholderText(" Aquí aparecerá el contenido del archivo .txt");
    outputTextEdit->setPlaceholderText(" Aquí se generará el código C++");

    // --- Estilo texto ---
    // QString editStyle = "padding: 10px; font-size: 14px;";
    // inputTextEdit->setStyleSheet(editStyle);
    // outputTextEdit->setStyleSheet(editStyle);

    // --- Botones ---
    loadButton = new QPushButton(" Cargar archivo .txt"); // Texto actualizado
    convertButton = new QPushButton(" Convertir a C++");   // Texto actualizado
    exportButton = new QPushButton(" Exportar archivo .cpp"); // Texto actualizado
    themeButton = new QPushButton(" Cambiar Tema");

    // Cargar iconos desde los recursos
    QIcon loadIcon(":/images/folder.png");
    QIcon convertIcon(":/images/settings.png");
    QIcon exportIcon(":/images/downloads.png");
    QIcon themeIcon(":/images/themeChange.png");

    // Establecer iconos en los botones
    loadButton->setIcon(loadIcon);
    convertButton->setIcon(convertIcon);
    exportButton->setIcon(exportIcon);
    themeButton->setIcon(themeIcon);

    // (Opcional) Ajustar el tamaño del icono si es necesario
    QSize iconSize(17,17);
    loadButton->setIconSize(iconSize);
    convertButton->setIconSize(iconSize);
    exportButton->setIconSize(iconSize);
    themeButton->setIconSize(iconSize);

    loadButton->setObjectName("loadButton");
    convertButton->setObjectName("convertButton");
    exportButton->setObjectName("exportButton");
    themeButton->setObjectName("themeButton");


    // QString buttonStyle = "padding: 8px; font-size: 14px;";
    // loadButton->setStyleSheet(buttonStyle);
    // convertButton->setStyleSheet(buttonStyle);
    // exportButton->setStyleSheet(buttonStyle);

    // --- Layout izquierdo (texto + botones) ---
    QVBoxLayout *leftLayout = new QVBoxLayout;
    leftLayout->addWidget(inputTextEdit);
    leftLayout->addWidget(loadButton);
    leftLayout->addWidget(convertButton);

    // --- Layout derecho (texto + botón) ---
    QVBoxLayout *rightLayout = new QVBoxLayout;
    rightLayout->addWidget(outputTextEdit);
    rightLayout->addWidget(exportButton);
    rightLayout->addWidget(themeButton);

    // --- Layout horizontal central ---
    QHBoxLayout *centerLayout = new QHBoxLayout;
    centerLayout->addLayout(leftLayout);
    centerLayout->addLayout(rightLayout);

    // --- Layout principal ---
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addLayout(titleLayout);
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
    connect(themeButton, &QPushButton::clicked, this, &Window::toggleTheme); // <-- Conectar nuevo botón
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

void Window::toggleTheme() {
    isDarkTheme = !isDarkTheme; // Invierte el estado
    if (isDarkTheme) {
        applyStylesheet(":/dark.qss");
    } else {
        applyStylesheet(":/light.qss");
    }
}

void Window::applyStylesheet(const QString &path) {
    QFile styleFile(path);
    if (styleFile.open(QFile::ReadOnly)) {
        QTextStream stream(&styleFile);
        qApp->setStyleSheet(stream.readAll());
    }
}