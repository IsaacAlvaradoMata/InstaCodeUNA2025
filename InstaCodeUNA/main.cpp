#include <QApplication>
#include "window.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    ConverterWindow window;
    window.setWindowTitle("InstaCodeUNA");
    window.resize(900, 500);
    window.show();

    return app.exec();
}
