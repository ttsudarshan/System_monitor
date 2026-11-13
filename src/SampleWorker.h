#pragma once

#include "Snapshot.h"
#include "core/FocusDetector.h"
#include "core/ProcScan.h"
#include "core/SysmonDb.h"
#include "monitors/CpuMonitor.h"
#include "monitors/EnergyMonitor.h"
#include "monitors/MemoryMonitor.h"
#include "monitors/NetworkMonitor.h"
#include "monitors/ScreenTimeTracker.h"

#include <QObject>
#include <memory>

// Lives on a QThread. Owns every monitor and the database connection, so the
// UI thread never touches /proc, X11 or SQLite.
class SampleWorker : public QObject {
    Q_OBJECT

public:
    SampleWorker();
    ~SampleWorker() override;

public slots:
    void sample();
    void shutdown();

signals:
    void ready(const Snapshot &snapshot);

private:
    CpuMonitor cpu_;
    MemoryMonitor mem_;
    DiskMonitor disk_;
    NetworkMonitor net_;
    ProcScanner procs_;
    std::unique_ptr<SysmonDb> db_;
    std::unique_ptr<FocusDetector> focus_;
    std::unique_ptr<EnergyMonitor> energy_;
    std::unique_ptr<ScreenTimeTracker> screen_;

    long long lastSampleMs_ = 0;
    long long lastEnergyFlushMs_ = 0;
    bool initialised_ = false;

    void init();
    void flushEnergy();
};
