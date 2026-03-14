#include "BatteryStatsTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollArea>
#include <QDate>
#include <algorithm>

BatteryStatsTab::BatteryStatsTab(QWidget *parent) : QWidget(parent) {
    setupUI();
}

QString BatteryStatsTab::formatPower(double watts) {
    if (watts >= 1.0) return QString("%1 W").arg(watts, 0, 'f', 2);
    if (watts >= 0.001) return QString("%1 mW").arg(watts * 1000, 0, 'f', 1);
    return "0 W";
}

QString BatteryStatsTab::formatEnergy(double joules) {
    if (joules >= 3600) return QString("%1 Wh").arg(joules / 3600.0, 0, 'f', 3);
    if (joules >= 1.0) return QString("%1 J").arg(joules, 0, 'f', 1);
    if (joules >= 0.001) return QString("%1 mJ").arg(joules * 1000, 0, 'f', 1);
    return "0 J";
}

QString BatteryStatsTab::formatWh(double wh) {
    if (wh >= 1.0) return QString("%1 Wh").arg(wh, 0, 'f', 1);
    if (wh >= 0.001) return QString("%1 mWh").arg(wh * 1000, 0, 'f', 1);
    return "0 Wh";
}

QString BatteryStatsTab::makeBar(double value, double maxValue, int maxLen) {
    if (maxValue <= 0 || value <= 0) return "";
    int len = static_cast<int>((value / maxValue) * maxLen);
    if (len < 1 && value > 0) len = 1;
    if (len > maxLen) len = maxLen;
    return QString(len, QChar(0x2588));
}

void BatteryStatsTab::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // ═══ Compact Top: CPU + Battery + Power (single row) ═══
    auto *topBar = new QWidget();
    topBar->setStyleSheet("background: #16213e; border-radius: 6px;");
    topBar->setMaximumHeight(70);
    auto *topLayout = new QVBoxLayout(topBar);
    topLayout->setContentsMargins(10, 6, 10, 6);
    topLayout->setSpacing(4);

    // Row 1: CPU info + energy source
    auto *row1 = new QHBoxLayout();
    cpuInfoLabel = new QLabel("Detecting CPU...");
    cpuInfoLabel->setStyleSheet("color: #4FC3F7; font-size: 11px; font-weight: bold;");
    row1->addWidget(cpuInfoLabel);
    row1->addStretch();
    energySourceLabel = new QLabel("");
    energySourceLabel->setStyleSheet("color: #81C784; font-size: 11px;");
    row1->addWidget(energySourceLabel);
    topLayout->addLayout(row1);

    // Row 2: Battery bar + power readings + time
    auto *row2 = new QHBoxLayout();
    row2->setSpacing(10);

    batteryBar = new QProgressBar();
    batteryBar->setRange(0, 100);
    batteryBar->setTextVisible(true);
    batteryBar->setFormat("%p%");
    batteryBar->setFixedWidth(120);
    batteryBar->setFixedHeight(20);
    batteryBar->setStyleSheet(
        "QProgressBar { background: #263238; border: 1px solid #37474F; border-radius: 4px;"
        "text-align: center; color: white; font-size: 11px; font-weight: bold; }"
        "QProgressBar::chunk { background: #43A047; border-radius: 3px; }");
    row2->addWidget(batteryBar);

    batteryStatusLabel = new QLabel("—");
    batteryStatusLabel->setStyleSheet("color: #B0BEC5; font-size: 11px;");
    row2->addWidget(batteryStatusLabel);

    powerReadingsLabel = new QLabel("");
    powerReadingsLabel->setStyleSheet("color: #78909C; font-size: 11px;");
    row2->addWidget(powerReadingsLabel);

    row2->addStretch();

    timeRemainingLabel = new QLabel("");
    timeRemainingLabel->setStyleSheet("color: #FFB74D; font-size: 11px; font-weight: bold;");
    row2->addWidget(timeRemainingLabel);

    topLayout->addLayout(row2);
    mainLayout->addWidget(topBar);

    // ═══ Small Power Chart ═══
    auto *chartWidget = new QWidget();
    chartWidget->setStyleSheet("background: #1a1a2e; border-radius: 6px;");
    chartWidget->setMaximumHeight(120);
    auto *chartLayout = new QVBoxLayout(chartWidget);
    chartLayout->setContentsMargins(6, 4, 6, 4);

    packageSeries = new QLineSeries();
    packageSeries->setPen(QPen(QColor(79, 195, 247), 2));
    packageSeries->setName("Package");
    coreSeries = new QLineSeries();
    coreSeries->setPen(QPen(QColor(129, 199, 132), 2));
    coreSeries->setName("Cores");

    auto *chart = new QChart();
    chart->addSeries(packageSeries);
    chart->addSeries(coreSeries);
    chart->setBackgroundBrush(QColor(26, 26, 46));
    chart->legend()->setLabelColor(Qt::white);
    chart->legend()->setAlignment(Qt::AlignRight);
    chart->setMargins(QMargins(2, 2, 2, 2));

    powerAxisX = new QValueAxis();
    powerAxisX->setRange(0, MAX_POINTS);
    powerAxisX->setVisible(false);
    powerAxisY = new QValueAxis();
    powerAxisY->setRange(0, 20);
    powerAxisY->setLabelFormat("%.0f W");
    powerAxisY->setLabelsColor(QColor(100, 120, 140));
    powerAxisY->setGridLineColor(QColor(40, 40, 60));

    chart->addAxis(powerAxisX, Qt::AlignBottom);
    chart->addAxis(powerAxisY, Qt::AlignLeft);
    packageSeries->attachAxis(powerAxisX);
    packageSeries->attachAxis(powerAxisY);
    coreSeries->attachAxis(powerAxisX);
    coreSeries->attachAxis(powerAxisY);

    powerChartView = new QChartView(chart);
    powerChartView->setRenderHint(QPainter::Antialiasing);
    chartLayout->addWidget(powerChartView);
    mainLayout->addWidget(chartWidget);

    // ═══ Per-App Drain (MAIN SECTION — takes all remaining space) ═══
    auto *drainWidget = new QWidget();
    drainWidget->setStyleSheet("background: #1a1a2e; border-radius: 6px;");
    auto *drainLayout = new QVBoxLayout(drainWidget);
    drainLayout->setContentsMargins(8, 8, 8, 8);
    drainLayout->setSpacing(6);

    // Header row
    auto *headerRow = new QHBoxLayout();

    drainSummaryLabel = new QLabel("APPLICATION ENERGY DRAIN");
    drainSummaryLabel->setStyleSheet("color: #78909C; font-size: 11px; font-weight: bold;");
    headerRow->addWidget(drainSummaryLabel);
    headerRow->addStretch();

    viewCombo = new QComboBox();
    viewCombo->addItem("Live");
    viewCombo->addItem("Today");
    viewCombo->addItem("This Week");
    viewCombo->setMinimumWidth(120);
    viewCombo->setStyleSheet(
        "QComboBox { padding: 5px 12px; background: #263238; color: white;"
        "border: 1px solid #37474F; border-radius: 4px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #263238; color: white;"
        "selection-background-color: #1565C0; padding: 4px; }");
    connect(viewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BatteryStatsTab::onViewChanged);
    headerRow->addWidget(viewCombo);

    headerRow->addSpacing(8);

    sortCombo = new QComboBox();
    sortCombo->addItem("By Energy");
    sortCombo->addItem("By Share %");
    sortCombo->setMinimumWidth(120);
    sortCombo->setStyleSheet(
        "QComboBox { padding: 5px 12px; background: #263238; color: white;"
        "border: 1px solid #37474F; border-radius: 4px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #263238; color: white;"
        "selection-background-color: #1565C0; padding: 4px; }");
    connect(sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BatteryStatsTab::onSortChanged);
    headerRow->addWidget(sortCombo);

    drainLayout->addLayout(headerRow);

    // The tree
    drainTree = new QTreeWidget();
    drainTree->setColumnCount(5);
    drainTree->setHeaderLabels({"Application", "Power", "Share", "Today / Total", ""});
    drainTree->setRootIsDecorated(false);
    drainTree->setAlternatingRowColors(false);
    drainTree->setUniformRowHeights(true);

    drainTree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    drainTree->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    drainTree->setColumnWidth(0, 180);
    drainTree->setColumnWidth(1, 80);
    drainTree->setColumnWidth(2, 60);
    drainTree->setColumnWidth(3, 100);

    drainTree->setStyleSheet(
        "QTreeWidget { background: transparent; border: none; font-size: 12px; }"
        "QTreeWidget::item { padding: 5px 8px; min-height: 26px; border-bottom: 1px solid #252545; }"
        "QTreeWidget::item:selected { background: #1565C0; }"
        "QTreeWidget::item:hover:!selected { background: #252545; }"
        "QHeaderView::section { background: #16213e; color: #546E7A; padding: 5px 8px;"
        "border: none; border-bottom: 2px solid #1565C0; font-size: 10px; font-weight: bold; }");

    drainLayout->addWidget(drainTree, 1);

    mainLayout->addWidget(drainWidget, 1); // stretch factor 1 = takes all remaining space
}

void BatteryStatsTab::onSortChanged(int index) { sortMode = index; }
void BatteryStatsTab::onViewChanged(int index) { viewMode = index; }

void BatteryStatsTab::populateLive(const EnergyMonitor &energy) {
    drainTree->setHeaderLabels({"Application", "Power", "Share", "Session", ""});
    drainTree->clear();

    auto apps = energy.perAppEnergy();
    if (sortMode == 1)
        std::sort(apps.begin(), apps.end(),
            [](const EnergyMonitor::AppEnergy &a, const EnergyMonitor::AppEnergy &b) {
                return a.percent > b.percent; });

    double maxVal = apps.empty() ? 1.0 : apps[0].watts;
    if (maxVal < 0.001) maxVal = 1.0;

    double totalW = 0;
    for (const auto &a : apps) totalW += a.watts;

    for (const auto &app : apps) {
        auto *item = new QTreeWidgetItem();
        item->setText(0, QString::fromStdString(app.appName));
        item->setText(1, formatPower(app.watts));
        item->setText(2, QString("%1%").arg(app.percent, 0, 'f', 1));
        item->setText(3, formatEnergy(app.energyJoules));
        item->setText(4, makeBar(app.watts, maxVal));

        QColor barColor(100, 181, 246);
        QColor textColor(180, 180, 180);
        if (app.percent > 40) { barColor = QColor(239, 83, 80); textColor = barColor; }
        else if (app.percent > 20) { barColor = QColor(255, 167, 38); textColor = barColor; }
        else if (app.percent > 10) { barColor = QColor(255, 241, 118); textColor = barColor; }

        item->setForeground(0, QColor(220, 220, 220));
        item->setForeground(1, textColor);
        item->setForeground(2, textColor);
        item->setForeground(3, QColor(120, 120, 120));
        item->setForeground(4, barColor);

        drainTree->addTopLevelItem(item);
    }

    drainSummaryLabel->setText(QString("LIVE POWER DRAIN  —  %1 apps  —  Total: %2")
        .arg(apps.size()).arg(formatPower(totalW)));
}

void BatteryStatsTab::populateHistory(const BatteryTracker &tracker, bool weekly) {
    drainTree->setHeaderLabels({"Application", "Energy", "Share", weekly ? "Weekly" : "Today", ""});
    drainTree->clear();

    auto data = weekly
        ? tracker.weeklyStats()
        : tracker.dailyStats(QDate::currentDate().toString("yyyy-MM-dd").toStdString());

    double totalWh = 0;
    for (const auto &d : data) totalWh += d.totalWh;

    if (sortMode == 1 && totalWh > 0) {
        // Already sorted by totalWh desc from tracker, but re-sort by share
        // (same thing since share = totalWh/total)
    }

    double maxWh = data.empty() ? 1.0 : data[0].totalWh;
    if (maxWh < 0.000001) maxWh = 1.0;

    for (const auto &d : data) {
        auto *item = new QTreeWidgetItem();
        double pct = (totalWh > 0) ? (d.totalWh / totalWh) * 100.0 : 0.0;

        item->setText(0, QString::fromStdString(d.appName));
        item->setText(1, formatWh(d.totalWh));
        item->setText(2, QString("%1%").arg(pct, 0, 'f', 1));
        item->setText(3, formatWh(d.totalWh));
        item->setText(4, makeBar(d.totalWh, maxWh));

        QColor barColor = weekly ? QColor(206, 147, 216) : QColor(79, 195, 247);
        QColor textColor(180, 180, 180);
        if (pct > 40) { barColor = QColor(239, 83, 80); textColor = barColor; }
        else if (pct > 20) { barColor = QColor(255, 167, 38); textColor = barColor; }
        else if (pct > 10) { barColor = QColor(255, 241, 118); textColor = barColor; }

        item->setForeground(0, QColor(220, 220, 220));
        item->setForeground(1, weekly ? QColor(206, 147, 216) : QColor(79, 195, 247));
        item->setForeground(2, textColor);
        item->setForeground(3, QColor(120, 120, 120));
        item->setForeground(4, barColor);

        drainTree->addTopLevelItem(item);
    }

    QString period = weekly ? "THIS WEEK" : "TODAY";
    drainSummaryLabel->setText(QString("%1 ENERGY  —  %2 apps  —  Total: %3")
        .arg(period).arg(data.size()).arg(formatWh(totalWh)));
}

void BatteryStatsTab::update(const EnergyMonitor &energy, const BatteryTracker &tracker) {
    // CPU info
    cpuInfoLabel->setText(QString("%1 %2 (%3 cores)")
        .arg(QString::fromStdString(energy.vendorName()))
        .arg(QString::fromStdString(energy.cpuModelName()))
        .arg(energy.coreCount()));

    if (energy.hasHardwareEnergy()) {
        QString src = (energy.vendor() == EnergyMonitor::CpuVendor::Intel) ? "Intel RAPL" : "AMD RAPL";
        energySourceLabel->setText(src);
        energySourceLabel->setStyleSheet("color: #81C784; font-size: 11px;");
    } else {
        energySourceLabel->setText("CPU estimate");
        energySourceLabel->setStyleSheet("color: #FFA726; font-size: 11px;");
    }

    // Battery
    if (energy.hasBattery()) {
        int pct = static_cast<int>(energy.batteryPercent());
        batteryBar->setValue(pct);

        QString chunk = "#43A047";
        if (pct <= 20) chunk = "#C62828";
        else if (pct <= 60) chunk = "#E65100";
        batteryBar->setStyleSheet(
            QString("QProgressBar { background: #263238; border: 1px solid #37474F; border-radius: 4px;"
                    "text-align: center; color: white; font-size: 11px; font-weight: bold; }"
                    "QProgressBar::chunk { background: %1; border-radius: 3px; }").arg(chunk));

        batteryStatusLabel->setText(QString::fromStdString(energy.batteryStatus()));

        // Power readings inline
        QString powerText;
        if (energy.hasHardwareEnergy()) {
            powerText = QString("Pkg: %1  |  Core: %2  |  DRAM: %3")
                .arg(formatPower(energy.packagePowerW()))
                .arg(formatPower(energy.corePowerW()))
                .arg(formatPower(energy.dramPowerW()));
        } else {
            powerText = QString("Draw: %1").arg(formatPower(energy.batteryPowerW()));
        }
        powerReadingsLabel->setText(powerText);

        int mins = energy.estimatedMinutesRemaining();
        if (mins > 0)
            timeRemainingLabel->setText(QString("~%1h %2m left").arg(mins / 60).arg(mins % 60));
        else
            timeRemainingLabel->setText("");
    } else {
        batteryBar->setValue(0);
        batteryStatusLabel->setText("AC");
        powerReadingsLabel->setText(energy.hasHardwareEnergy()
            ? QString("Pkg: %1  |  Core: %2  |  DRAM: %3")
                .arg(formatPower(energy.packagePowerW()))
                .arg(formatPower(energy.corePowerW()))
                .arg(formatPower(energy.dramPowerW()))
            : "");
        timeRemainingLabel->setText("");
    }

    // Power chart
    double pkg = energy.packagePowerW();
    double core = energy.corePowerW();
    packageHistory.push_back(pkg);
    coreHistory.push_back(core);
    if (static_cast<int>(packageHistory.size()) > MAX_POINTS) packageHistory.pop_front();
    if (static_cast<int>(coreHistory.size()) > MAX_POINTS) coreHistory.pop_front();
    powerTick++;

    packageSeries->clear();
    coreSeries->clear();
    int start = powerTick - static_cast<int>(packageHistory.size());
    double maxY = 5.0;
    for (int i = 0; i < static_cast<int>(packageHistory.size()); ++i) {
        packageSeries->append(start + i, packageHistory[i]);
        if (packageHistory[i] > maxY) maxY = packageHistory[i];
    }
    for (int i = 0; i < static_cast<int>(coreHistory.size()); ++i) {
        coreSeries->append(start + i, coreHistory[i]);
        if (coreHistory[i] > maxY) maxY = coreHistory[i];
    }
    powerAxisX->setRange(powerTick - MAX_POINTS, powerTick);
    powerAxisY->setRange(0, maxY * 1.2);

    // Drain table — main content
    switch (viewMode) {
        case 0: populateLive(energy); break;
        case 1: populateHistory(tracker, false); break;
        case 2: populateHistory(tracker, true); break;
    }
}