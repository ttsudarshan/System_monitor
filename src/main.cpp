#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("System Monitor");
    app.setApplicationVersion("1.0");

    // Dark palette for a modern system monitor look
    QPalette dark;
    dark.setColor(QPalette::Window, QColor(30, 30, 30));
    dark.setColor(QPalette::WindowText, Qt::white);
    dark.setColor(QPalette::Base, QColor(40, 40, 40));
    dark.setColor(QPalette::AlternateBase, QColor(50, 50, 50));
    dark.setColor(QPalette::Text, Qt::white);
    dark.setColor(QPalette::Button, QColor(50, 50, 50));
    dark.setColor(QPalette::ButtonText, Qt::white);
    dark.setColor(QPalette::Highlight, QColor(70, 130, 180));
    dark.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(dark);

    MainWindow w;
    w.setWindowTitle("System Monitor");
    w.resize(1100, 700);
    w.show();

    return app.exec();
}