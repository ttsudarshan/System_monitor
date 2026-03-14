#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QLabel>
#include <QPushButton>
#include <QtCharts/QChartView>
#include <QtCharts/QStackedBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLineSeries>
#include "monitors/ScreenTimeTracker.h"

class ScreenTimeTab : public QWidget {
    Q_OBJECT

public:
    explicit ScreenTimeTab(QWidget *parent = nullptr);

    void updateStats(const std::vector<ScreenTimeTracker::AppScreenTime> &today,
                     const std::vector<ScreenTimeTracker::AppScreenTime> &weekly,
                     int totalTodaySec, int totalWeekSec,
                     const std::string &currentApp,
                     const std::vector<ScreenTimeTracker::DailyTotal> &last7,
                     int dailyAvgSec);

private slots:
    void onWeekClicked();
    void onDayClicked();

private:
    bool showingWeek = true;

    // Toggle
    QPushButton *weekBtn;
    QPushButton *dayBtn;

    // Summary
    QLabel *periodLabel;
    QLabel *bigTimeLabel;
    QLabel *changeLabel;

    // Chart
    QChartView *chartView;
    QChart *chart;

    // Total
    QLabel *totalLabel;
    QLabel *totalTimeLabel;
    QLabel *updatedLabel;

    // Most Used
    QLabel *mostUsedTitle;
    QTreeWidget *appList;

    // Cache
    std::vector<ScreenTimeTracker::AppScreenTime> cachedToday;
    std::vector<ScreenTimeTracker::AppScreenTime> cachedWeekly;
    std::vector<ScreenTimeTracker::DailyTotal> cachedLast7;
    int cachedTodaySec = 0;
    int cachedWeekSec = 0;
    int cachedDailyAvg = 0;

    void setupUI();
    void refresh();
    void buildChart();
    void buildAppList();
    static QString fmtTime(int sec);
};