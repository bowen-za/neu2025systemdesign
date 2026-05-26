#include "qt_mainwindow.h"
#include "vfs.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    vfs::configureConsoleEncoding();
    QApplication app(argc, argv);

    MainWindow window;
    window.resize(1120, 720);
    window.show();

    return app.exec();
}
