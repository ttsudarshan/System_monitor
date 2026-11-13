#pragma once

#include "SampleWorker.h"
#include "Snapshot.h"
#include "widgets/BatteryStatsTab.h"
#include "widgets/LogsTab.h"
#include "widgets/MetricsTab.h"
#include "widgets/ProcessesTab.h"
#include "widgets/ScreenTimeTab.h"

#include <QMainWindow>
#include <QTabWidget>
#include <QThread>
#include <QTimer>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void requestSample();
    void onSnapshot(const Snapshot &s);

private:
    QTabWidget *tabs;
    MetricsTab *metricsTab;
    ProcessesTab *processesTab;
    LogsTab *logsTab;
    BatteryStatsTab *batteryStatsTab;
    ScreenTimeTab *screenTimeTab;

    QThread workerThread;
    SampleWorker *worker;
    bool samplePending = false;

    QTimer *refreshTimer;
    static constexpr int REFRESH_MS = 1500;
};
