#include "SampleWorker.h"

#include <QDebug>

SampleWorker::SampleWorker() = default;

SampleWorker::~SampleWorker() {
    shutdown();
}

void SampleWorker::init() {
    // Done lazily on the worker thread so the SQLite connection and the X
    // display belong to this thread.
    db_ = std::make_unique<SysmonDb>();
    if (!db_->isOpen())
        qWarning() << "System Monitor: cannot open database" << QString::fromStdString(db_->path())
                   << QString::fromStdString(db_->lastError());
    else if (db_->importLegacy(SysmonDb::legacyPath()))
        qInfo() << "System Monitor: imported history from the old 'System Monitor' database";

    focus_ = std::make_unique<FocusDetector>();
    energy_ = std::make_unique<EnergyMonitor>();
    screen_ = std::make_unique<ScreenTimeTracker>(*db_, *focus_);

    // Baseline reads so the first visible sample has deltas.
    cpu_.read(); mem_.read(); disk_.read(); net_.read();
    procs_.scan();
    lastSampleMs_ = lastEnergyFlushMs_ = TimeUtil::nowMs();
    initialised_ = true;
}

void SampleWorker::flushEnergy() {
    if (!energy_ || !db_ || !db_->isOpen()) return;
    auto pending = energy_->takePendingWh();
    if (!pending.empty()) db_->addEnergy(pending, TimeUtil::dateString(0));
    lastEnergyFlushMs_ = TimeUtil::nowMs();
}

void SampleWorker::shutdown() {
    if (!initialised_) return;
    if (screen_) screen_->flush();
    flushEnergy();
    screen_.reset();
    energy_.reset();
    focus_.reset();
    db_.reset();
    initialised_ = false;
}

void SampleWorker::sample() {
    if (!initialised_) init();

    long long now = TimeUtil::nowMs();
    long long elapsedMs = now - lastSampleMs_;
    lastSampleMs_ = now;
    if (elapsedMs <= 0) elapsedMs = 1;
    double elapsedSec = elapsedMs / 1000.0;

    Snapshot s;
    s.time = QDateTime::currentDateTime();
    s.sampleIntervalSec = elapsedSec;

    cpu_.read();
    mem_.read();
    disk_.read();
    net_.read();
    ProcScanResult scan = procs_.scan();
    energy_->read(scan, elapsedSec);
    screen_->tick(elapsedMs);

    s.cpuPct = cpu_.overallUsage();
    s.perCore = cpu_.perCoreUsage();
    s.memUsedMB = mem_.usedMB();
    s.memTotalMB = mem_.totalMB();
    s.swapUsedMB = mem_.swapUsedMB();
    s.swapTotalMB = mem_.swapTotalMB();

    s.disks = disk_.disks();
    s.diskReadBps = disk_.readBytesPerSec();
    s.diskWriteBps = disk_.writeBytesPerSec();
    s.netRxBps = net_.rxBytesPerSec();
    s.netTxBps = net_.txBytesPerSec();
    s.netTotalRx = net_.totalRxBytes();
    s.netTotalTx = net_.totalTxBytes();

    const BatteryMonitor &bat = energy_->battery();
    s.hasBattery = bat.hasBattery();
    s.batteryPct = bat.percent();
    s.batteryStatus = bat.status();
    s.batteryPowerW = bat.powerDrawWatts();
    s.batteryMinutesLeft = bat.minutesRemaining();

    s.cpuVendor = energy_->vendorName();
    s.cpuModel = energy_->cpuModelName();
    s.cpuCores = energy_->coreCount();
    s.hwEnergy = energy_->hasHardwareEnergy();
    s.packageW = energy_->packagePowerW();
    s.coreW = energy_->corePowerW();
    s.dramW = energy_->dramPowerW();
    s.powerSource = energy_->powerSource();
    s.attributedW = energy_->attributedPowerW();
    s.liveEnergy = energy_->perAppEnergy();

    if (now - lastEnergyFlushMs_ >= 10000) flushEnergy();
    if (db_->isOpen()) {
        std::string today = TimeUtil::dateString(0);
        s.energyToday = db_->energyTotals(today, today);
        s.energyWeek = db_->energyTotals(TimeUtil::dateString(6), today);
    }

    s.procs = std::move(scan.procs);
    s.screen = screen_->stats();

    emit ready(s);
}
