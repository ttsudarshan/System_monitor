#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QComboBox>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include "monitors/EnergyMonitor.h"
#include "monitors/BatteryTracker.h"
#include <deque>

class BatteryStatsTab : public QWidget {
    Q_OBJECT

public:
    explicit BatteryStatsTab(QWidget *parent = nullptr);
    void update(const EnergyMonitor &energy, const BatteryTracker &tracker);

private slots:
    void onSortChanged(int index);
    void onViewChanged(int index);

private:
    // Hardware + battery bar (compact top strip)
    QLabel *cpuInfoLabel;
    QLabel *energySourceLabel;
    QProgressBar *batteryBar;
    QLabel *batteryStatusLabel;
    QLabel *powerReadingsLabel;
    QLabel *timeRemainingLabel;

    // Small power chart
    QChartView *powerChartView;
    QLineSeries *packageSeries;
    QLineSeries *coreSeries;
    QValueAxis *powerAxisX, *powerAxisY;
    std::deque<double> packageHistory;
    std::deque<double> coreHistory;
    int powerTick = 0;
    static constexpr int MAX_POINTS = 60;

    // Main drain table (takes most space)
    QTreeWidget *drainTree;
    QComboBox *viewCombo;  // Live / Today / Week
    QComboBox *sortCombo;
    QLabel *drainSummaryLabel;
    int viewMode = 0;
    int sortMode = 0;

    void setupUI();
    void populateLive(const EnergyMonitor &energy);
    void populateHistory(const BatteryTracker &tracker, bool weekly);

    static QString formatPower(double watts);
    static QString formatEnergy(double joules);
    static QString formatWh(double wh);
    static QString makeBar(double value, double maxValue, int maxLen = 30);
};