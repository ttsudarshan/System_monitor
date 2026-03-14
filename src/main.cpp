#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("System Monitor");
    app.setApplicationVersion("1.0");

    // Dark palette for a modern system monitor look
    QPalette dark;
    dark.setColor(QPalette::Window, QColor(15, 15, 20));
    dark.setColor(QPalette::WindowText, Qt::white);
    dark.setColor(QPalette::Base, QColor(20, 20, 28));
    dark.setColor(QPalette::AlternateBase, QColor(28, 28, 36));
    dark.setColor(QPalette::Text, Qt::white);
    dark.setColor(QPalette::Button, QColor(30, 30, 40));
    dark.setColor(QPalette::ButtonText, Qt::white);
    dark.setColor(QPalette::Highlight, QColor(21, 101, 192));
    dark.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(dark);

    // Global stylesheet for tabs
    app.setStyleSheet(
        "QTabWidget::pane {"
        "  border: none;"
        "  background: #0f0f14;"
        "}"
        "QTabBar {"
        "  background: #0f0f14;"
        "  border-bottom: 1px solid #1a1a2e;"
        "}"
        "QTabBar::tab {"
        "  background: transparent;"
        "  color: #6b7280;"
        "  padding: 10px 22px;"
        "  margin: 0px 2px;"
        "  border: none;"
        "  border-bottom: 2px solid transparent;"
        "  font-size: 13px;"
        "}"
        "QTabBar::tab:selected {"
        "  color: #60a5fa;"
        "  border-bottom: 2px solid #60a5fa;"
        "}"
        "QTabBar::tab:hover:!selected {"
        "  color: #9ca3af;"
        "  border-bottom: 2px solid #374151;"
        "}"
    );

    MainWindow w;
    w.setWindowTitle("System Monitor");
    w.resize(1100, 700);
    w.show();

    return app.exec();
}