#pragma once

#include <QWidget>
#include <QLabel>
#include <QTableWidget>
#include <QScrollArea>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <vector>
#include <deque>
#include "monitors/DiskMonitor.h"

class MetricsTab : public QWidget {
    Q_OBJECT

public:
    explicit MetricsTab(QWidget *parent = nullptr);

    void updateCpu(double overall, const std::vector<double> &perCore);
    void updateMemory(double usedMB, double totalMB, double swapUsedMB, double swapTotalMB);
    void updateDisk(const std::vector<DiskMonitor::DiskInfo> &disks,
                    double readBps, double writeBps);
    void updateNetwork(double rxBps, double txBps, uint64_t totalRx, uint64_t totalTx);
    void updateBattery(bool present, double percent, const std::string &status, double watts);

private:
    static constexpr int MAX_POINTS = 60;

    // CPU
    QChartView *cpuChartView;
    QLineSeries *cpuSeries;
    QValueAxis *cpuAxisX, *cpuAxisY;
    QLabel *cpuLabel;
    QLabel *coreLabel;
    std::deque<double> cpuHistory;
    int cpuTick = 0;

    // Memory
    QChartView *memChartView;
    QLineSeries *memSeries;
    QValueAxis *memAxisX, *memAxisY;
    QLabel *memLabel;
    QLabel *swapLabel;
    std::deque<double> memHistory;
    int memTick = 0;

    // Disk
    QTableWidget *diskTable;
    QChartView *diskIOChartView;
    QLineSeries *diskReadSeries;
    QLineSeries *diskWriteSeries;
    QValueAxis *diskIOAxisX, *diskIOAxisY;
    QLabel *diskIOLabel;
    std::deque<double> diskReadHistory;
    std::deque<double> diskWriteHistory;
    int diskIOTick = 0;

    // Network
    QChartView *netChartView;
    QLineSeries *netRxSeries;
    QLineSeries *netTxSeries;
    QValueAxis *netAxisX, *netAxisY;
    QLabel *netLabel;
    QLabel *netTotalLabel;
    std::deque<double> netRxHistory;
    std::deque<double> netTxHistory;
    int netTick = 0;

    // Battery
    QLabel *batteryLabel;
    QLabel *batteryDetailLabel;

    void setupUI();
    void appendToChart(QLineSeries *series, QValueAxis *axisX,
                       std::deque<double> &history, int &tick, double value);
    void appendDualChart(QLineSeries *s1, QLineSeries *s2,
                         QValueAxis *axisX, QValueAxis *axisY,
                         std::deque<double> &h1, std::deque<double> &h2,
                         int &tick, double v1, double v2);
    static QString formatBytes(double bytes);
};