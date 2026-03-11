#pragma once

#include <QMainWindow>
#include <QTabWidget>
#include <QTimer>
#include "widgets/MetricsTab.h"
#include "widgets/ProcessesTab.h"
#include "widgets/LogsTab.h"
#include "monitors/CpuMonitor.h"
#include "monitors/MemoryMonitor.h"

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

    CpuMonitor cpuMonitor;
    MemoryMonitor memMonitor;

    QTimer *refreshTimer;
    static constexpr int REFRESH_MS = 1500; // 1.5s refresh
};