#include "MainWindow.h"
#include <QVBoxLayout>
#include <QDate>
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

    // Initial reads
    cpuMonitor.read();
    memMonitor.read();
    diskMonitor.read();
    netMonitor.read();
    battMonitor.read();
    energyMonitor.read();

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &MainWindow::onRefresh);
    refreshTimer->start(REFRESH_MS);
}

void MainWindow::onRefresh() {
    // Read all monitors
    cpuMonitor.read();
    memMonitor.read();
    diskMonitor.read();
    netMonitor.read();
    battMonitor.read();
    battTracker.tick();
    energyMonitor.read();
    screenTracker.tick();

    // Update Metrics tab
    metricsTab->updateCpu(cpuMonitor.overallUsage(), cpuMonitor.perCoreUsage());
    metricsTab->updateMemory(memMonitor.usedMB(), memMonitor.totalMB(),
                             memMonitor.swapUsedMB(), memMonitor.swapTotalMB());
    metricsTab->updateDisk(diskMonitor.disks(),
                           diskMonitor.readBytesPerSec(), diskMonitor.writeBytesPerSec());
    metricsTab->updateNetwork(netMonitor.rxBytesPerSec(), netMonitor.txBytesPerSec(),
                              netMonitor.totalRxBytes(), netMonitor.totalTxBytes());
    metricsTab->updateBattery(battMonitor.hasBattery(), battMonitor.percent(),
                              battMonitor.status(), battMonitor.powerDrawWatts());

    // Update Processes tab
    processesTab->refresh();

    // Update Battery Stats tab (new energy monitor)
    batteryStatsTab->update(energyMonitor, battTracker);

    // Update Screen Time tab
    auto todayScreenTime = screenTracker.todayStats();
    auto weeklyScreenTime = screenTracker.weeklyStats();
    auto last7 = screenTracker.last7Days();
    screenTimeTab->updateStats(todayScreenTime, weeklyScreenTime,
                               screenTracker.totalTodaySeconds(),
                               screenTracker.totalWeekSeconds(),
                               screenTracker.currentApp(),
                               last7,
                               screenTracker.dailyAverageSeconds());

    // Check alerts
    double memPct = (memMonitor.totalMB() > 0)
        ? (memMonitor.usedMB() / memMonitor.totalMB()) * 100.0 : 0.0;
    double maxDiskPct = 0.0;
    for (const auto &d : diskMonitor.disks())
        maxDiskPct = std::max(maxDiskPct, d.usagePercent);
    logsTab->checkAlerts(cpuMonitor.overallUsage(), memPct, maxDiskPct);

    // Record snapshot for export
    LogsTab::SystemSnapshot snap;
    snap.cpuPct = cpuMonitor.overallUsage();
    snap.memUsedMB = memMonitor.usedMB();
    snap.memTotalMB = memMonitor.totalMB();
    snap.netRxBps = netMonitor.rxBytesPerSec();
    snap.netTxBps = netMonitor.txBytesPerSec();
    snap.diskReadBps = diskMonitor.readBytesPerSec();
    snap.diskWriteBps = diskMonitor.writeBytesPerSec();
    snap.processCount = 0;
    logsTab->recordSnapshot(snap);
}