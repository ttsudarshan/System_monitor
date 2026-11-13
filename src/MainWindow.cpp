#include "MainWindow.h"

#include <algorithm>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->setTabPosition(QTabWidget::North);

    metricsTab = new MetricsTab(this);
    processesTab = new ProcessesTab(this);
    logsTab = new LogsTab(this);
    batteryStatsTab = new BatteryStatsTab(this);
    screenTimeTab = new ScreenTimeTab(this);

    tabs->addTab(metricsTab, "  Metrics  ");
    tabs->addTab(processesTab, "  Processes  ");
    tabs->addTab(batteryStatsTab, "  Battery  ");
    tabs->addTab(screenTimeTab, "  Screen Time  ");
    tabs->addTab(logsTab, "  Logs  ");
    setCentralWidget(tabs);

    // All sampling (/proc, sysfs, X11, SQLite) happens on the worker thread.
    qRegisterMetaType<Snapshot>("Snapshot");
    worker = new SampleWorker();
    worker->moveToThread(&workerThread);
    connect(&workerThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &SampleWorker::ready, this, &MainWindow::onSnapshot, Qt::QueuedConnection);
    workerThread.start();

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &MainWindow::requestSample);
    refreshTimer->start(REFRESH_MS);
    requestSample();
}

MainWindow::~MainWindow() {
    refreshTimer->stop();
    // Flush pending screen time / energy before the thread goes away.
    QMetaObject::invokeMethod(worker, "shutdown", Qt::BlockingQueuedConnection);
    workerThread.quit();
    workerThread.wait();
}

void MainWindow::requestSample() {
    // Never queue up samples: if the worker is still busy, skip this tick.
    if (samplePending) return;
    samplePending = true;
    QMetaObject::invokeMethod(worker, "sample", Qt::QueuedConnection);
}

void MainWindow::onSnapshot(const Snapshot &s) {
    samplePending = false;

    metricsTab->updateCpu(s.cpuPct, s.perCore);
    metricsTab->updateMemory(s.memUsedMB, s.memTotalMB, s.swapUsedMB, s.swapTotalMB);
    metricsTab->updateDisk(s.disks, s.diskReadBps, s.diskWriteBps);
    metricsTab->updateNetwork(s.netRxBps, s.netTxBps, s.netTotalRx, s.netTotalTx);
    metricsTab->updateBattery(s.hasBattery, s.batteryPct, s.batteryStatus, s.batteryPowerW);

    // Only rebuild the heavier tabs when they are visible; the data is in the
    // snapshot anyway and will be shown as soon as the user switches.
    QWidget *current = tabs->currentWidget();
    if (current == processesTab) processesTab->refresh(s.procs);
    if (current == batteryStatsTab) batteryStatsTab->update(s);
    if (current == screenTimeTab) screenTimeTab->updateStats(s.screen);

    double memPct = (s.memTotalMB > 0) ? (s.memUsedMB / s.memTotalMB) * 100.0 : 0.0;
    double maxDiskPct = 0.0;
    for (const auto &d : s.disks) maxDiskPct = std::max(maxDiskPct, d.usagePercent);
    logsTab->checkAlerts(s.cpuPct, memPct, maxDiskPct);

    LogsTab::SystemSnapshot snap;
    snap.time = s.time;
    snap.cpuPct = s.cpuPct;
    snap.memUsedMB = s.memUsedMB;
    snap.memTotalMB = s.memTotalMB;
    snap.netRxBps = s.netRxBps;
    snap.netTxBps = s.netTxBps;
    snap.diskReadBps = s.diskReadBps;
    snap.diskWriteBps = s.diskWriteBps;
    snap.processCount = static_cast<int>(s.procs.size());
    logsTab->recordSnapshot(snap);
}
