#include "MetricsTab.h"
#include <QVBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QHeaderView>

MetricsTab::MetricsTab(QWidget *parent) : QWidget(parent) {
    setupUI();
}

QString MetricsTab::formatBytes(double bytes) {
    if (bytes >= 1073741824) return QString::number(bytes / 1073741824.0, 'f', 1) + " GB";
    if (bytes >= 1048576) return QString::number(bytes / 1048576.0, 'f', 1) + " MB";
    if (bytes >= 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    return QString::number(bytes, 'f', 0) + " B";
}

void MetricsTab::setupUI() {
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *container = new QWidget();
    auto *layout = new QVBoxLayout(container);

    // ==================== CPU ====================
    auto *cpuGroup = new QGroupBox("CPU Usage");
    cpuGroup->setStyleSheet("QGroupBox { color: white; font-weight: bold; }");
    auto *cpuLayout = new QVBoxLayout(cpuGroup);

    cpuLabel = new QLabel("CPU: 0.0%");
    cpuLabel->setStyleSheet("font-size: 14px; color: #4FC3F7;");
    cpuLayout->addWidget(cpuLabel);

    cpuSeries = new QLineSeries();
    cpuSeries->setPen(QPen(QColor(79, 195, 247), 2));
    auto *cpuChart = new QChart();
    cpuChart->addSeries(cpuSeries);
    cpuChart->setBackgroundBrush(QColor(40, 40, 40));
    cpuChart->legend()->hide();
    cpuChart->setMargins(QMargins(5, 5, 5, 5));
    cpuAxisX = new QValueAxis(); cpuAxisX->setRange(0, MAX_POINTS); cpuAxisX->setVisible(false);
    cpuAxisY = new QValueAxis(); cpuAxisY->setRange(0, 100); cpuAxisY->setLabelFormat("%d%%");
    cpuAxisY->setLabelsColor(Qt::white); cpuAxisY->setGridLineColor(QColor(60, 60, 60));
    cpuChart->addAxis(cpuAxisX, Qt::AlignBottom);
    cpuChart->addAxis(cpuAxisY, Qt::AlignLeft);
    cpuSeries->attachAxis(cpuAxisX); cpuSeries->attachAxis(cpuAxisY);
    cpuChartView = new QChartView(cpuChart);
    cpuChartView->setRenderHint(QPainter::Antialiasing);
    cpuChartView->setMinimumHeight(160);
    cpuLayout->addWidget(cpuChartView);

    coreLabel = new QLabel("");
    coreLabel->setStyleSheet("font-size: 11px; color: #B0BEC5;");
    coreLabel->setWordWrap(true);
    cpuLayout->addWidget(coreLabel);
    layout->addWidget(cpuGroup);

    // ==================== Memory ====================
    auto *memGroup = new QGroupBox("Memory Usage");
    memGroup->setStyleSheet("QGroupBox { color: white; font-weight: bold; }");
    auto *memLayout = new QVBoxLayout(memGroup);

    memLabel = new QLabel("RAM: 0 / 0 MB");
    memLabel->setStyleSheet("font-size: 14px; color: #81C784;");
    memLayout->addWidget(memLabel);

    memSeries = new QLineSeries();
    memSeries->setPen(QPen(QColor(129, 199, 132), 2));
    auto *memChart = new QChart();
    memChart->addSeries(memSeries);
    memChart->setBackgroundBrush(QColor(40, 40, 40));
    memChart->legend()->hide();
    memChart->setMargins(QMargins(5, 5, 5, 5));
    memAxisX = new QValueAxis(); memAxisX->setRange(0, MAX_POINTS); memAxisX->setVisible(false);
    memAxisY = new QValueAxis(); memAxisY->setRange(0, 100); memAxisY->setLabelFormat("%d%%");
    memAxisY->setLabelsColor(Qt::white); memAxisY->setGridLineColor(QColor(60, 60, 60));
    memChart->addAxis(memAxisX, Qt::AlignBottom);
    memChart->addAxis(memAxisY, Qt::AlignLeft);
    memSeries->attachAxis(memAxisX); memSeries->attachAxis(memAxisY);
    memChartView = new QChartView(memChart);
    memChartView->setRenderHint(QPainter::Antialiasing);
    memChartView->setMinimumHeight(160);
    memLayout->addWidget(memChartView);

    swapLabel = new QLabel("Swap: 0 / 0 MB");
    swapLabel->setStyleSheet("font-size: 11px; color: #B0BEC5;");
    memLayout->addWidget(swapLabel);
    layout->addWidget(memGroup);

    // ==================== Disk ====================
    auto *diskGroup = new QGroupBox("Disk Usage");
    diskGroup->setStyleSheet("QGroupBox { color: white; font-weight: bold; }");
    auto *diskLayout = new QVBoxLayout(diskGroup);

    diskTable = new QTableWidget();
    diskTable->setColumnCount(5);
    diskTable->setHorizontalHeaderLabels({"Device", "Mount", "Type", "Used / Total", "Usage %"});
    diskTable->horizontalHeader()->setStretchLastSection(true);
    diskTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    diskTable->setMaximumHeight(120);
    diskTable->setStyleSheet(
        "QTableWidget { gridline-color: #444; }"
        "QHeaderView::section { background: #333; color: white; padding: 4px; border: 1px solid #444; }");
    diskLayout->addWidget(diskTable);

    diskIOLabel = new QLabel("Disk I/O: Read: 0 B/s | Write: 0 B/s");
    diskIOLabel->setStyleSheet("font-size: 13px; color: #FFB74D;");
    diskLayout->addWidget(diskIOLabel);

    diskReadSeries = new QLineSeries();
    diskReadSeries->setPen(QPen(QColor(79, 195, 247), 2));
    diskWriteSeries = new QLineSeries();
    diskWriteSeries->setPen(QPen(QColor(239, 83, 80), 2));
    auto *diskIOChart = new QChart();
    diskIOChart->addSeries(diskReadSeries);
    diskIOChart->addSeries(diskWriteSeries);
    diskIOChart->setBackgroundBrush(QColor(40, 40, 40));
    diskIOChart->legend()->setLabelColor(Qt::white);
    diskReadSeries->setName("Read");
    diskWriteSeries->setName("Write");
    diskIOChart->setMargins(QMargins(5, 5, 5, 5));
    diskIOAxisX = new QValueAxis(); diskIOAxisX->setRange(0, MAX_POINTS); diskIOAxisX->setVisible(false);
    diskIOAxisY = new QValueAxis(); diskIOAxisY->setRange(0, 1048576); diskIOAxisY->setLabelFormat("%.0f");
    diskIOAxisY->setLabelsColor(Qt::white); diskIOAxisY->setGridLineColor(QColor(60, 60, 60));
    diskIOAxisY->setTitleText("Bytes/s"); diskIOAxisY->setTitleBrush(Qt::white);
    diskIOChart->addAxis(diskIOAxisX, Qt::AlignBottom);
    diskIOChart->addAxis(diskIOAxisY, Qt::AlignLeft);
    diskReadSeries->attachAxis(diskIOAxisX); diskReadSeries->attachAxis(diskIOAxisY);
    diskWriteSeries->attachAxis(diskIOAxisX); diskWriteSeries->attachAxis(diskIOAxisY);
    diskIOChartView = new QChartView(diskIOChart);
    diskIOChartView->setRenderHint(QPainter::Antialiasing);
    diskIOChartView->setMinimumHeight(150);
    diskLayout->addWidget(diskIOChartView);
    layout->addWidget(diskGroup);

    // ==================== Network ====================
    auto *netGroup = new QGroupBox("Network");
    netGroup->setStyleSheet("QGroupBox { color: white; font-weight: bold; }");
    auto *netLayout = new QVBoxLayout(netGroup);

    netLabel = new QLabel("Download: 0 B/s | Upload: 0 B/s");
    netLabel->setStyleSheet("font-size: 13px; color: #CE93D8;");
    netLayout->addWidget(netLabel);

    netRxSeries = new QLineSeries();
    netRxSeries->setPen(QPen(QColor(129, 199, 132), 2));
    netTxSeries = new QLineSeries();
    netTxSeries->setPen(QPen(QColor(255, 183, 77), 2));
    auto *netChart = new QChart();
    netChart->addSeries(netRxSeries);
    netChart->addSeries(netTxSeries);
    netChart->setBackgroundBrush(QColor(40, 40, 40));
    netChart->legend()->setLabelColor(Qt::white);
    netRxSeries->setName("Download");
    netTxSeries->setName("Upload");
    netChart->setMargins(QMargins(5, 5, 5, 5));
    netAxisX = new QValueAxis(); netAxisX->setRange(0, MAX_POINTS); netAxisX->setVisible(false);
    netAxisY = new QValueAxis(); netAxisY->setRange(0, 1048576); netAxisY->setLabelFormat("%.0f");
    netAxisY->setLabelsColor(Qt::white); netAxisY->setGridLineColor(QColor(60, 60, 60));
    netAxisY->setTitleText("Bytes/s"); netAxisY->setTitleBrush(Qt::white);
    netChart->addAxis(netAxisX, Qt::AlignBottom);
    netChart->addAxis(netAxisY, Qt::AlignLeft);
    netRxSeries->attachAxis(netAxisX); netRxSeries->attachAxis(netAxisY);
    netTxSeries->attachAxis(netAxisX); netTxSeries->attachAxis(netAxisY);
    netChartView = new QChartView(netChart);
    netChartView->setRenderHint(QPainter::Antialiasing);
    netChartView->setMinimumHeight(150);
    netLayout->addWidget(netChartView);

    netTotalLabel = new QLabel("Total Received: 0 B | Total Sent: 0 B");
    netTotalLabel->setStyleSheet("font-size: 11px; color: #B0BEC5;");
    netLayout->addWidget(netTotalLabel);
    layout->addWidget(netGroup);

    // ==================== Battery ====================
    auto *battGroup = new QGroupBox("Battery");
    battGroup->setStyleSheet("QGroupBox { color: white; font-weight: bold; }");
    auto *battLayout = new QVBoxLayout(battGroup);

    batteryLabel = new QLabel("No battery detected");
    batteryLabel->setStyleSheet("font-size: 14px; color: #B0BEC5;");
    battLayout->addWidget(batteryLabel);

    batteryDetailLabel = new QLabel("");
    batteryDetailLabel->setStyleSheet("font-size: 11px; color: #B0BEC5;");
    battLayout->addWidget(batteryDetailLabel);
    layout->addWidget(battGroup);

    layout->addStretch();
    scrollArea->setWidget(container);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(scrollArea);
}

void MetricsTab::appendToChart(QLineSeries *series, QValueAxis *axisX,
                                std::deque<double> &history, int &tick, double value) {
    history.push_back(value);
    if (static_cast<int>(history.size()) > MAX_POINTS)
        history.pop_front();
    tick++;

    series->clear();
    int start = tick - static_cast<int>(history.size());
    for (int i = 0; i < static_cast<int>(history.size()); ++i)
        series->append(start + i, history[i]);
    axisX->setRange(tick - MAX_POINTS, tick);
}

void MetricsTab::appendDualChart(QLineSeries *s1, QLineSeries *s2,
                                  QValueAxis *axisX, QValueAxis *axisY,
                                  std::deque<double> &h1, std::deque<double> &h2,
                                  int &tick, double v1, double v2) {
    h1.push_back(v1);
    h2.push_back(v2);
    if (static_cast<int>(h1.size()) > MAX_POINTS) h1.pop_front();
    if (static_cast<int>(h2.size()) > MAX_POINTS) h2.pop_front();
    tick++;

    s1->clear(); s2->clear();
    int start = tick - static_cast<int>(h1.size());
    double maxVal = 1024; // Minimum Y range
    for (int i = 0; i < static_cast<int>(h1.size()); ++i) {
        s1->append(start + i, h1[i]);
        if (h1[i] > maxVal) maxVal = h1[i];
    }
    for (int i = 0; i < static_cast<int>(h2.size()); ++i) {
        s2->append(start + i, h2[i]);
        if (h2[i] > maxVal) maxVal = h2[i];
    }
    axisX->setRange(tick - MAX_POINTS, tick);
    axisY->setRange(0, maxVal * 1.2); // 20% headroom
}

void MetricsTab::updateCpu(double overall, const std::vector<double> &perCore) {
    QString color = "#4FC3F7";
    if (overall > 90.0) color = "#EF5350";
    else if (overall > 70.0) color = "#FFA726";

    cpuLabel->setText(QString("CPU: %1%").arg(overall, 0, 'f', 1));
    cpuLabel->setStyleSheet(QString("font-size: 14px; color: %1;").arg(color));
    appendToChart(cpuSeries, cpuAxisX, cpuHistory, cpuTick, overall);

    QString coreText = "Cores: ";
    for (size_t i = 0; i < perCore.size(); ++i) {
        if (i > 0) coreText += "  |  ";
        coreText += QString("CPU%1: %2%").arg(i).arg(perCore[i], 0, 'f', 1);
    }
    coreLabel->setText(coreText);
}

void MetricsTab::updateMemory(double usedMB, double totalMB,
                               double swapUsedMB, double swapTotalMB) {
    double pct = (totalMB > 0) ? (usedMB / totalMB) * 100.0 : 0.0;
    QString color = "#81C784";
    if (pct > 90.0) color = "#EF5350";
    else if (pct > 70.0) color = "#FFA726";

    memLabel->setText(QString("RAM: %1 / %2 MB (%3%)")
        .arg(usedMB, 0, 'f', 0).arg(totalMB, 0, 'f', 0).arg(pct, 0, 'f', 1));
    memLabel->setStyleSheet(QString("font-size: 14px; color: %1;").arg(color));
    appendToChart(memSeries, memAxisX, memHistory, memTick, pct);

    swapLabel->setText(QString("Swap: %1 / %2 MB")
        .arg(swapUsedMB, 0, 'f', 0).arg(swapTotalMB, 0, 'f', 0));
}

void MetricsTab::updateDisk(const std::vector<DiskMonitor::DiskInfo> &disks,
                             double readBps, double writeBps) {
    // Filesystem table
    diskTable->setRowCount(static_cast<int>(disks.size()));
    for (int i = 0; i < static_cast<int>(disks.size()); ++i) {
        const auto &d = disks[i];
        diskTable->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(d.device)));
        diskTable->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(d.mountPoint)));
        diskTable->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(d.fsType)));
        diskTable->setItem(i, 3, new QTableWidgetItem(
            QString("%1 / %2 MB").arg(d.usedMB).arg(d.totalMB)));

        auto *pctItem = new QTableWidgetItem(QString("%1%").arg(d.usagePercent, 0, 'f', 1));
        if (d.usagePercent > 90.0)
            pctItem->setForeground(QColor(239, 83, 80));
        else if (d.usagePercent > 70.0)
            pctItem->setForeground(QColor(255, 167, 38));
        diskTable->setItem(i, 4, pctItem);
    }

    diskIOLabel->setText(QString("Disk I/O — Read: %1/s | Write: %2/s")
        .arg(formatBytes(readBps), formatBytes(writeBps)));

    appendDualChart(diskReadSeries, diskWriteSeries, diskIOAxisX, diskIOAxisY,
                    diskReadHistory, diskWriteHistory, diskIOTick, readBps, writeBps);
}

void MetricsTab::updateNetwork(double rxBps, double txBps,
                                uint64_t totalRx, uint64_t totalTx) {
    netLabel->setText(QString("Download: %1/s | Upload: %2/s")
        .arg(formatBytes(rxBps), formatBytes(txBps)));

    netTotalLabel->setText(QString("Total Received: %1 | Total Sent: %2")
        .arg(formatBytes(totalRx), formatBytes(totalTx)));

    appendDualChart(netRxSeries, netTxSeries, netAxisX, netAxisY,
                    netRxHistory, netTxHistory, netTick, rxBps, txBps);
}

void MetricsTab::updateBattery(bool present, double percent,
                                const std::string &status, double watts) {
    if (!present) {
        batteryLabel->setText("No battery detected");
        batteryLabel->setStyleSheet("font-size: 14px; color: #B0BEC5;");
        batteryDetailLabel->setText("");
        return;
    }

    QString color = "#81C784";
    if (percent < 20.0) color = "#EF5350";
    else if (percent < 40.0) color = "#FFA726";

    batteryLabel->setText(QString("Battery: %1% — %2")
        .arg(percent, 0, 'f', 0).arg(QString::fromStdString(status)));
    batteryLabel->setStyleSheet(QString("font-size: 14px; color: %1;").arg(color));

    batteryDetailLabel->setText(QString("Power draw: %1 W").arg(watts, 0, 'f', 2));
}