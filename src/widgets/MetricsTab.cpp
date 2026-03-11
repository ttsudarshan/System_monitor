#include "MetricsTab.h"
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFont>

MetricsTab::MetricsTab(QWidget *parent) : QWidget(parent) {
    setupCharts();
}

void MetricsTab::setupCharts() {
    auto *layout = new QVBoxLayout(this);

    // --- CPU Section ---
    auto *cpuGroup = new QGroupBox("CPU Usage");
    cpuGroup->setStyleSheet("QGroupBox { color: white; font-weight: bold; }");
    auto *cpuLayout = new QVBoxLayout(cpuGroup);

    cpuLabel = new QLabel("CPU: 0.0%");
    cpuLabel->setStyleSheet("font-size: 14px; color: #4FC3F7;");
    cpuLayout->addWidget(cpuLabel);

    cpuSeries = new QLineSeries();
    cpuSeries->setColor(QColor(79, 195, 247));
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
    cpuSeries->attachAxis(cpuAxisX);
    cpuSeries->attachAxis(cpuAxisY);

    cpuChartView = new QChartView(cpuChart);
    cpuChartView->setRenderHint(QPainter::Antialiasing);
    cpuChartView->setMinimumHeight(180);
    cpuLayout->addWidget(cpuChartView);

    coreLabel = new QLabel("");
    coreLabel->setStyleSheet("font-size: 11px; color: #B0BEC5;");
    coreLabel->setWordWrap(true);
    cpuLayout->addWidget(coreLabel);

    layout->addWidget(cpuGroup);

    // --- Memory Section ---
    auto *memGroup = new QGroupBox("Memory Usage");
    memGroup->setStyleSheet("QGroupBox { color: white; font-weight: bold; }");
    auto *memLayout = new QVBoxLayout(memGroup);

    memLabel = new QLabel("RAM: 0 / 0 MB");
    memLabel->setStyleSheet("font-size: 14px; color: #81C784;");
    memLayout->addWidget(memLabel);

    memSeries = new QLineSeries();
    memSeries->setColor(QColor(129, 199, 132));
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
    memSeries->attachAxis(memAxisX);
    memSeries->attachAxis(memAxisY);

    memChartView = new QChartView(memChart);
    memChartView->setRenderHint(QPainter::Antialiasing);
    memChartView->setMinimumHeight(180);
    memLayout->addWidget(memChartView);

    swapLabel = new QLabel("Swap: 0 / 0 MB");
    swapLabel->setStyleSheet("font-size: 11px; color: #B0BEC5;");
    memLayout->addWidget(swapLabel);

    layout->addWidget(memGroup);
    layout->addStretch();
}

void MetricsTab::appendToChart(QLineSeries *series, QValueAxis *axisX,
                                std::deque<double> &history, int &tick, double value) {
    history.push_back(value);
    if (static_cast<int>(history.size()) > MAX_POINTS)
        history.pop_front();
    tick++;

    series->clear();
    int start = tick - static_cast<int>(history.size());
    for (int i = 0; i < static_cast<int>(history.size()); ++i) {
        series->append(start + i, history[i]);
    }
    axisX->setRange(tick - MAX_POINTS, tick);
}

void MetricsTab::updateCpu(double overall, const std::vector<double> &perCore) {
    // Color-code: red if > 90%, yellow if > 70%
    QString color = "#4FC3F7";
    if (overall > 90.0) color = "#EF5350";
    else if (overall > 70.0) color = "#FFA726";

    cpuLabel->setText(QString("CPU: %1%").arg(overall, 0, 'f', 1));
    cpuLabel->setStyleSheet(QString("font-size: 14px; color: %1;").arg(color));

    appendToChart(cpuSeries, cpuAxisX, cpuHistory, cpuTick, overall);

    // Per-core text
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