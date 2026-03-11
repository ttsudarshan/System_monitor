#include "LogsTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>

LogsTab::LogsTab(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);

    logView = new QTextEdit(this);
    logView->setReadOnly(true);
    logView->setStyleSheet("background: #1e1e1e; color: #d4d4d4; font-family: monospace; font-size: 12px; border: 1px solid #444;");
    layout->addWidget(logView);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    clearBtn = new QPushButton("Clear Logs");
    clearBtn->setStyleSheet("padding: 6px 16px; background: #555; color: white; border: none; border-radius: 4px;");
    connect(clearBtn, &QPushButton::clicked, this, &LogsTab::onClear);
    btnLayout->addWidget(clearBtn);
    layout->addLayout(btnLayout);

    appendLog("System Monitor started.");
}

void LogsTab::appendLog(const QString &msg) {
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    logView->append(QString("[%1] %2").arg(ts, msg));
}

void LogsTab::onClear() {
    logView->clear();
}