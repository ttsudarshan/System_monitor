#include "MainWindow.h"
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);

    metricsTab = new MetricsTab(this);
    processesTab = new ProcessesTab(this);
    logsTab = new LogsTab(this);

    tabs->addTab(metricsTab, "📊 Metrics");
    tabs->addTab(processesTab, "📋 Processes");
    tabs->addTab(logsTab, "📝 Logs");

    setCentralWidget(tabs);

    // Initial read so monitors have baseline data
    cpuMonitor.read();
    memMonitor.read();

    // Auto-refresh timer
    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &MainWindow::onRefresh);
    refreshTimer->start(REFRESH_MS);
}

void MainWindow::onRefresh() {
    cpuMonitor.read();
    memMonitor.read();

    metricsTab->updateCpu(cpuMonitor.overallUsage(), cpuMonitor.perCoreUsage());
    metricsTab->updateMemory(memMonitor.usedMB(), memMonitor.totalMB(),
                             memMonitor.swapUsedMB(), memMonitor.swapTotalMB());
    processesTab->refresh();
}