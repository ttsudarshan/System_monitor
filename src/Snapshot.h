#pragma once

#include "core/ProcScan.h"
#include "monitors/DiskMonitor.h"
#include "monitors/EnergyMonitor.h"
#include "monitors/ScreenTimeTracker.h"

#include <QDateTime>
#include <QMetaType>
#include <string>
#include <vector>

// Everything the UI needs for one refresh, produced on the worker thread.
// Plain data only, so it can be copied across the thread boundary.
struct Snapshot {
    QDateTime time;
    double sampleIntervalSec = 1.5;

    // CPU / memory
    double cpuPct = 0;
    std::vector<double> perCore;
    double memUsedMB = 0, memTotalMB = 0, swapUsedMB = 0, swapTotalMB = 0;

    // Disk / network
    std::vector<DiskMonitor::DiskInfo> disks;
    double diskReadBps = 0, diskWriteBps = 0;
    double netRxBps = 0, netTxBps = 0;
    uint64_t netTotalRx = 0, netTotalTx = 0;

    // Battery
    bool hasBattery = false;
    double batteryPct = 0;
    std::string batteryStatus;
    double batteryPowerW = 0;
    int batteryMinutesLeft = -1;

    // Energy
    std::string cpuVendor, cpuModel;
    int cpuCores = 0;
    bool hwEnergy = false;
    double packageW = 0, coreW = 0, dramW = 0;
    EnergyMonitor::PowerSource powerSource = EnergyMonitor::PowerSource::None;
    double attributedW = 0;
    std::vector<EnergyMonitor::AppEnergy> liveEnergy;
    std::vector<AppEnergyTotal> energyToday, energyWeek;

    // Processes
    std::vector<ProcInfo> procs;

    // Screen time
    ScreenTimeStats screen;
};

Q_DECLARE_METATYPE(Snapshot)
