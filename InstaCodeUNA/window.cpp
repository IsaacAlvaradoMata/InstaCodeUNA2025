#include "window.h"
#include "parser.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>

ConverterWindow::ConverterWindow(QWidget *parent)
    : QWidget(parent) {
    // Crear los editores de texto
    inputText = new QTextEdit(this);
    outputText = new QTextEdit(this);

    // Desactivar edición en la salida
    outputText->setReadOnly(true);

    // Crear botones
    loadButton = new QPushButton("Cargar archivo", this);
    convertButton = new QPushButton("Convertir a C++", this);

    // Crear layouts
    QHBoxLayout *mainLayout = new QHBoxLayout();       // Layout horizontal para las dos vistas
    QVBoxLayout *leftLayout = new QVBoxLayout();       // Vista izquierda: archivo original
    QVBoxLayout *rightLayout = new QVBoxLayout();      // Vista derecha: salida C++
    QHBoxLayout *buttonLayout = new QHBoxLayout();     // Botones al fondo

    // Agregar los QTextEdit a sus lados
    leftLayout->addWidget(inputText);
    rightLayout->addWidget(outputText);

    // Agregar ambos lados al layout principal
    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addLayout(rightLayout, 1);

    // Agregar los botones al layout inferior
    buttonLayout->addWidget(loadButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(convertButton);

    // Combinar layouts en layout final vertical
    QVBoxLayout *finalLayout = new QVBoxLayout(this);
    finalLayout->addLayout(mainLayout);
    finalLayout->addLayout(buttonLayout);

    // Aplicar layout final al widget
    setLayout(finalLayout);

    // Conectar eventos
    connect(loadButton, &QPushButton::clicked, this, &ConverterWindow::loadFile);
    connect(convertButton, &QPushButton::clicked, this, &ConverterWindow::convertToCpp);
}

void ConverterWindow::loadFile() {
    QString fileName = QFileDialog::getOpenFileName(this, "Seleccionar archivo", "", "Archivos de texto (*.txt)");
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            inputText->setPlainText(in.readAll());
            file.close();
        }
    }
}

void ConverterWindow::convertToCpp() {
    QString input = inputText->toPlainText();
    QString converted = Parser::convert(input);  // Usa la lógica del parser
    outputText->setPlainText(converted);
}
