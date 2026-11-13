#pragma once

#include "Snapshot.h"

#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QTreeWidget>
#include <QWidget>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <deque>

class BatteryStatsTab : public QWidget {
    Q_OBJECT

public:
    explicit BatteryStatsTab(QWidget *parent = nullptr);
    void update(const Snapshot &s);

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

    // Main drain table
    QTreeWidget *drainTree;
    QComboBox *viewCombo;  // Live / Today / Week
    QComboBox *sortCombo;  // By Power / By Energy
    QLabel *drainSummaryLabel;
    QLabel *noteLabel;

    void setupUI();
    void populateLive(const Snapshot &s);
    void populateHistory(const std::vector<AppEnergyTotal> &data, bool weekly);

    static QString formatPower(double watts);
    static QString formatEnergy(double joules);
    static QString formatWh(double wh);
    static QString makeBar(double value, double maxValue, int maxLen = 30);
};
