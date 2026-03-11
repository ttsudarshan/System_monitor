#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QPushButton>

class LogsTab : public QWidget {
    Q_OBJECT

public:
    explicit LogsTab(QWidget *parent = nullptr);

    void appendLog(const QString &msg);

private slots:
    void onClear();

private:
    QTextEdit *logView;
    QPushButton *clearBtn;
};