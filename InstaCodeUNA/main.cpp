#include <QApplication>
#include <QFile>
#include <QTextStream>
#include "window.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Cargar la hoja de estilos por defecto (tema claro)
    QFile styleFile(":/light.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        QTextStream stream(&styleFile);
        app.setStyleSheet(stream.readAll());
    }

    Window window;
    window.show(); // <-- ESTA ES LA LÍNEA CLAVE

    return app.exec();
}