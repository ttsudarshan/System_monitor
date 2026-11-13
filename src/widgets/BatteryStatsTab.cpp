#include "BatteryStatsTab.h"
#include "core/AppNames.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>
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
    if (wh >= 1.0) return QString("%1 Wh").arg(wh, 0, 'f', 2);
    if (wh >= 0.001) return QString("%1 mWh").arg(wh * 1000, 0, 'f', 1);
    return "0 Wh";
}

QString BatteryStatsTab::makeBar(double value, double maxValue, int maxLen) {
    if (maxValue <= 0 || value <= 0) return "";
    int len = static_cast<int>((value / maxValue) * maxLen);
    if (len < 1) len = 1;
    if (len > maxLen) len = maxLen;
    return QString(len, QChar(0x2588));
}

void BatteryStatsTab::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // ═══ Compact top: CPU + battery + power ═══
    auto *topBar = new QWidget();
    topBar->setStyleSheet("background: #16213e; border-radius: 6px;");
    topBar->setMaximumHeight(70);
    auto *topLayout = new QVBoxLayout(topBar);
    topLayout->setContentsMargins(10, 6, 10, 6);
    topLayout->setSpacing(4);

    auto *row1 = new QHBoxLayout();
    cpuInfoLabel = new QLabel("Detecting CPU...");
    cpuInfoLabel->setStyleSheet("color: #4FC3F7; font-size: 11px; font-weight: bold;");
    row1->addWidget(cpuInfoLabel);
    row1->addStretch();
    energySourceLabel = new QLabel("");
    energySourceLabel->setStyleSheet("color: #81C784; font-size: 11px;");
    row1->addWidget(energySourceLabel);
    topLayout->addLayout(row1);

    auto *row2 = new QHBoxLayout();
    row2->setSpacing(10);
    batteryBar = new QProgressBar();
    batteryBar->setRange(0, 100);
    batteryBar->setTextVisible(true);
    batteryBar->setFormat("%p%");
    batteryBar->setFixedWidth(120);
    batteryBar->setFixedHeight(20);
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

    // ═══ Small power chart ═══
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
    chart->setAnimationOptions(QChart::NoAnimation);

    powerAxisX = new QValueAxis();
    powerAxisX->setRange(0, MAX_POINTS);
    powerAxisX->setVisible(false);
    powerAxisY = new QValueAxis();
    powerAxisY->setRange(0, 20);
    powerAxisY->setLabelFormat("%.1f W");
    powerAxisY->setTickCount(3);
    powerAxisY->setTruncateLabels(false);
    powerAxisY->setLabelsFont(QFont("", 8));
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

    // ═══ Per-app drain ═══
    auto *drainWidget = new QWidget();
    drainWidget->setStyleSheet("background: #1a1a2e; border-radius: 6px;");
    auto *drainLayout = new QVBoxLayout(drainWidget);
    drainLayout->setContentsMargins(8, 8, 8, 8);
    drainLayout->setSpacing(6);

    auto *headerRow = new QHBoxLayout();
    drainSummaryLabel = new QLabel("APPLICATION ENERGY DRAIN");
    drainSummaryLabel->setStyleSheet("color: #78909C; font-size: 11px; font-weight: bold;");
    headerRow->addWidget(drainSummaryLabel);
    headerRow->addStretch();

    const char *comboStyle =
        "QComboBox { padding: 5px 12px; background: #263238; color: white;"
        "border: 1px solid #37474F; border-radius: 4px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #263238; color: white;"
        "selection-background-color: #1565C0; padding: 4px; }";

    viewCombo = new QComboBox();
    viewCombo->addItems({"Live", "Today", "This Week"});
    viewCombo->setMinimumWidth(120);
    viewCombo->setStyleSheet(comboStyle);
    headerRow->addWidget(viewCombo);
    headerRow->addSpacing(8);

    sortCombo = new QComboBox();
    sortCombo->addItems({"By Power", "By Session Energy"});
    sortCombo->setMinimumWidth(150);
    sortCombo->setStyleSheet(comboStyle);
    headerRow->addWidget(sortCombo);
    drainLayout->addLayout(headerRow);

    noteLabel = new QLabel("");
    noteLabel->setStyleSheet("color: #FFA726; font-size: 11px;");
    noteLabel->setWordWrap(true);
    noteLabel->hide();
    drainLayout->addWidget(noteLabel);

    drainTree = new QTreeWidget();
    drainTree->setColumnCount(5);
    drainTree->setHeaderLabels({"Application", "Power", "Share", "Session", ""});
    drainTree->setRootIsDecorated(false);
    drainTree->setUniformRowHeights(true);
    drainTree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    drainTree->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    drainTree->setColumnWidth(0, 180);
    drainTree->setColumnWidth(1, 100);
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
    mainLayout->addWidget(drainWidget, 1);
}

void BatteryStatsTab::populateLive(const Snapshot &s) {
    drainTree->setHeaderLabels({"Application", "Power", "Share", "Session", ""});
    sortCombo->setEnabled(true);
    drainTree->clear();

    auto apps = s.liveEnergy;
    bool bySessionEnergy = sortCombo->currentIndex() == 1;
    if (bySessionEnergy)
        std::sort(apps.begin(), apps.end(), [](const auto &a, const auto &b) {
            return a.energyJoules > b.energyJoules; });

    double maxVal = 1.0;
    for (const auto &a : apps) maxVal = std::max(maxVal, bySessionEnergy ? a.energyJoules : a.percent);
    double totalW = 0;
    for (const auto &a : apps) totalW += a.watts;

    for (const auto &app : apps) {
        auto *item = new QTreeWidgetItem();
        item->setText(0, QString::fromStdString(AppNames::display(app.app)));
        item->setText(1, s.powerSource == EnergyMonitor::PowerSource::None ? "—" : formatPower(app.watts));
        item->setText(2, QString("%1%").arg(app.percent, 0, 'f', 1));
        item->setText(3, formatEnergy(app.energyJoules));
        item->setText(4, makeBar(bySessionEnergy ? app.energyJoules : app.percent, maxVal));

        QColor barColor(100, 181, 246), textColor(180, 180, 180);
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

    QString source;
    switch (s.powerSource) {
        case EnergyMonitor::PowerSource::Rapl: source = "attributing CPU package power"; break;
        case EnergyMonitor::PowerSource::Battery: source = "attributing battery discharge"; break;
        default: source = "CPU share only"; break;
    }
    drainSummaryLabel->setText(QString("LIVE  —  %1 apps  —  %2  —  %3")
        .arg(apps.size()).arg(formatPower(totalW)).arg(source));

    if (s.powerSource == EnergyMonitor::PowerSource::None) {
        noteLabel->setText(s.hwEnergy
            ? "No power reading this tick; showing CPU share only."
            : "No hardware power counter is readable and the battery is not discharging, so only "
              "CPU share is shown. See the README for the RAPL udev rule.");
        noteLabel->show();
    } else {
        noteLabel->hide();
    }
}

void BatteryStatsTab::populateHistory(const std::vector<AppEnergyTotal> &data, bool weekly) {
    drainTree->setHeaderLabels({"Application", "Energy", "Share", weekly ? "This Week" : "Today", ""});
    sortCombo->setEnabled(false);  // history is always ordered by energy
    drainTree->clear();
    noteLabel->hide();

    double totalWh = 0, maxWh = 0;
    for (const auto &d : data) { totalWh += d.wattHours; maxWh = std::max(maxWh, d.wattHours); }
    if (maxWh <= 0) maxWh = 1.0;

    for (const auto &d : data) {
        double pct = (totalWh > 0) ? (d.wattHours / totalWh) * 100.0 : 0.0;
        auto *item = new QTreeWidgetItem();
        item->setText(0, QString::fromStdString(AppNames::display(d.app)));
        item->setText(1, formatWh(d.wattHours));
        item->setText(2, QString("%1%").arg(pct, 0, 'f', 1));
        item->setText(3, formatWh(d.wattHours));
        item->setText(4, makeBar(d.wattHours, maxWh));

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

    drainSummaryLabel->setText(QString("%1 ENERGY  —  %2 apps  —  Total: %3")
        .arg(weekly ? "THIS WEEK" : "TODAY").arg(data.size()).arg(formatWh(totalWh)));
}

void BatteryStatsTab::update(const Snapshot &s) {
    QString model = QString::fromStdString(s.cpuModel);
    QString vendor = QString::fromStdString(s.cpuVendor);
    // "model name" in /proc/cpuinfo usually already starts with the vendor
    if (model.startsWith(vendor, Qt::CaseInsensitive) || vendor == "Unknown") vendor.clear();
    cpuInfoLabel->setText(QString("%1%2 (%3 cores)")
        .arg(vendor.isEmpty() ? "" : vendor + " ", model).arg(s.cpuCores));

    if (s.hwEnergy) {
        energySourceLabel->setText(s.cpuVendor == "AMD" ? "AMD RAPL" : "Intel RAPL");
        energySourceLabel->setStyleSheet("color: #81C784; font-size: 11px;");
    } else {
        energySourceLabel->setText("No hardware power counter");
        energySourceLabel->setStyleSheet("color: #FFA726; font-size: 11px;");
    }

    QString powerText = s.hwEnergy
        ? QString("Pkg: %1  |  Core: %2  |  DRAM: %3")
              .arg(formatPower(s.packageW), formatPower(s.coreW), formatPower(s.dramW))
        : QString();

    if (s.hasBattery) {
        int pct = static_cast<int>(s.batteryPct);
        batteryBar->setValue(pct);
        QString chunk = pct <= 20 ? "#C62828" : pct <= 60 ? "#E65100" : "#43A047";
        batteryBar->setStyleSheet(
            QString("QProgressBar { background: #263238; border: 1px solid #37474F; border-radius: 4px;"
                    "text-align: center; color: white; font-size: 11px; font-weight: bold; }"
                    "QProgressBar::chunk { background: %1; border-radius: 3px; }").arg(chunk));
        batteryStatusLabel->setText(QString::fromStdString(s.batteryStatus));
        if (powerText.isEmpty()) powerText = QString("Draw: %1").arg(formatPower(s.batteryPowerW));
        else powerText += QString("  |  Battery: %1").arg(formatPower(s.batteryPowerW));
        timeRemainingLabel->setText(s.batteryMinutesLeft > 0
            ? QString("~%1h %2m left").arg(s.batteryMinutesLeft / 60).arg(s.batteryMinutesLeft % 60)
            : QString());
    } else {
        batteryBar->setValue(0);
        batteryStatusLabel->setText("AC");
        timeRemainingLabel->setText("");
    }
    powerReadingsLabel->setText(powerText);

    // Power chart
    packageHistory.push_back(s.packageW);
    coreHistory.push_back(s.coreW);
    if (static_cast<int>(packageHistory.size()) > MAX_POINTS) packageHistory.pop_front();
    if (static_cast<int>(coreHistory.size()) > MAX_POINTS) coreHistory.pop_front();
    powerTick++;

    QList<QPointF> pkgPts, corePts;
    int start = powerTick - static_cast<int>(packageHistory.size());
    double maxY = 5.0;
    for (int i = 0; i < static_cast<int>(packageHistory.size()); ++i) {
        pkgPts.append(QPointF(start + i, packageHistory[i]));
        corePts.append(QPointF(start + i, coreHistory[i]));
        maxY = std::max({maxY, packageHistory[i], coreHistory[i]});
    }
    packageSeries->replace(pkgPts);
    coreSeries->replace(corePts);
    powerAxisX->setRange(powerTick - MAX_POINTS, powerTick);
    powerAxisY->setRange(0, maxY * 1.2);

    switch (viewCombo->currentIndex()) {
        case 1: populateHistory(s.energyToday, false); break;
        case 2: populateHistory(s.energyWeek, true); break;
        default: populateLive(s); break;
    }
}
