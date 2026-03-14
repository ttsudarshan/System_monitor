#pragma once

#include <QMainWindow>
#include <QTabWidget>
#include <QTimer>
#include "widgets/MetricsTab.h"
#include "widgets/ProcessesTab.h"
#include "widgets/LogsTab.h"
#include "widgets/BatteryStatsTab.h"
#include "widgets/ScreenTimeTab.h"
#include "monitors/CpuMonitor.h"
#include "monitors/MemoryMonitor.h"
#include "monitors/DiskMonitor.h"
#include "monitors/NetworkMonitor.h"
#include "monitors/BatteryMonitor.h"
#include "monitors/BatteryTracker.h"
#include "monitors/EnergyMonitor.h"
#include "monitors/ScreenTimeTracker.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() = default;

private slots:
    void onRefresh();

private:
    QTabWidget *tabs;
    MetricsTab *metricsTab;
    ProcessesTab *processesTab;
    LogsTab *logsTab;
    BatteryStatsTab *batteryStatsTab;
    ScreenTimeTab *screenTimeTab;

    CpuMonitor cpuMonitor;
    MemoryMonitor memMonitor;
    DiskMonitor diskMonitor;
    NetworkMonitor netMonitor;
    BatteryMonitor battMonitor;
    BatteryTracker battTracker;
    EnergyMonitor energyMonitor;
    ScreenTimeTracker screenTracker;

    QTimer *refreshTimer;
    static constexpr int REFRESH_MS = 1500;
};