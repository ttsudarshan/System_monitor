#pragma once

#include <QWidget>
#include <QLabel>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <vector>
#include <deque>

class MetricsTab : public QWidget {
    Q_OBJECT

public:
    explicit MetricsTab(QWidget *parent = nullptr);

    void updateCpu(double overall, const std::vector<double> &perCore);
    void updateMemory(double usedMB, double totalMB, double swapUsedMB, double swapTotalMB);

private:
    static constexpr int MAX_POINTS = 60; // 60 data points in graph

    // CPU
    QChartView *cpuChartView;
    QLineSeries *cpuSeries;
    QValueAxis *cpuAxisX, *cpuAxisY;
    QLabel *cpuLabel;
    std::deque<double> cpuHistory;
    int cpuTick = 0;

    // Per-core labels
    QLabel *coreLabel;

    // Memory
    QChartView *memChartView;
    QLineSeries *memSeries;
    QValueAxis *memAxisX, *memAxisY;
    QLabel *memLabel;
    std::deque<double> memHistory;
    int memTick = 0;

    // Swap
    QLabel *swapLabel;

    void setupCharts();
    void appendToChart(QLineSeries *series, QValueAxis *axisX,
                       std::deque<double> &history, int &tick, double value);
};