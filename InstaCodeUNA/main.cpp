#include <QApplication>
#include <QFile>
#include <QTextStream>
#include "window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QFile styleFile(":/light.qss");
    if (styleFile.open(QFile::ReadOnly))
    {
        QTextStream stream(&styleFile);
        app.setStyleSheet(stream.readAll());
    }

    Window window;
    window.show();

    return app.exec();
}