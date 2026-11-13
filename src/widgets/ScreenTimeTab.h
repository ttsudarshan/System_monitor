#pragma once

#include "monitors/ScreenTimeTracker.h"

#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QWidget>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>

class ScreenTimeTab : public QWidget {
    Q_OBJECT

public:
    explicit ScreenTimeTab(QWidget *parent = nullptr);
    void updateStats(const ScreenTimeStats &stats);

private slots:
    void onWeekClicked();
    void onDayClicked();

private:
    bool showingWeek = true;

    QPushButton *weekBtn;
    QPushButton *dayBtn;

    QLabel *periodLabel;
    QLabel *bigTimeLabel;
    QLabel *changeLabel;

    QChartView *chartView;
    QChart *chart;

    QLabel *totalLabel;
    QLabel *totalTimeLabel;
    QLabel *nowLabel;
    QLabel *updatedLabel;

    QLabel *mostUsedTitle;
    QTreeWidget *appList;

    ScreenTimeStats cached;
    bool haveStats = false;

    void setupUI();
    void refresh();
    void buildWeekChart();
    void buildDayChart();
    void buildAppList();
    static QString fmtTime(int sec);
};
