#include "ScreenTimeTab.h"
#include "core/AppNames.h"

#include <QDate>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <algorithm>

ScreenTimeTab::ScreenTimeTab(QWidget *parent) : QWidget(parent) {
    setupUI();
}

QString ScreenTimeTab::fmtTime(int sec) {
    if (sec < 60) return QString("%1s").arg(sec);
    int m = sec / 60, h = m / 60;
    m %= 60;
    int s = sec % 60;
    if (h > 0) return QString("%1h %2m").arg(h).arg(m, 2, 10, QChar('0'));
    if (s > 0) return QString("%1m %2s").arg(m).arg(s, 2, 10, QChar('0'));
    return QString("%1m").arg(m);
}

void ScreenTimeTab::setupUI() {
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea{background:#000;border:none;}");

    auto *page = new QWidget();
    page->setStyleSheet("background:#000;");
    auto *lay = new QVBoxLayout(page);
    lay->setSpacing(0);
    lay->setContentsMargins(18, 14, 18, 18);

    // ── Toggle: Week | Day ──
    auto *toggleRow = new QHBoxLayout();
    toggleRow->addStretch();
    auto *pill = new QWidget();
    pill->setFixedSize(200, 34);
    pill->setStyleSheet("background:#333; border-radius:17px;");
    auto *pillLay = new QHBoxLayout(pill);
    pillLay->setContentsMargins(3, 3, 3, 3);
    pillLay->setSpacing(2);
    weekBtn = new QPushButton("Week");
    weekBtn->setFixedHeight(28);
    weekBtn->setCursor(Qt::PointingHandCursor);
    connect(weekBtn, &QPushButton::clicked, this, &ScreenTimeTab::onWeekClicked);
    pillLay->addWidget(weekBtn);
    dayBtn = new QPushButton("Day");
    dayBtn->setFixedHeight(28);
    dayBtn->setCursor(Qt::PointingHandCursor);
    connect(dayBtn, &QPushButton::clicked, this, &ScreenTimeTab::onDayClicked);
    pillLay->addWidget(dayBtn);
    toggleRow->addWidget(pill);
    toggleRow->addStretch();
    lay->addLayout(toggleRow);
    lay->addSpacing(18);

    auto *stLabel = new QLabel("Screen Time");
    stLabel->setStyleSheet("color:#4cc9f0; font-size:15px; font-weight:bold;");
    lay->addWidget(stLabel);
    lay->addSpacing(10);

    // ── Card ──
    auto *card = new QWidget();
    card->setStyleSheet("background:#1c1c1e; border-radius:14px;");
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(18, 16, 18, 14);
    cardLay->setSpacing(2);

    periodLabel = new QLabel("Daily Average");
    periodLabel->setStyleSheet("color:#8e8e93; font-size:13px;");
    cardLay->addWidget(periodLabel);

    auto *bigRow = new QHBoxLayout();
    bigTimeLabel = new QLabel("0h 00m");
    bigTimeLabel->setStyleSheet("color:#fff; font-size:34px; font-weight:bold;");
    bigRow->addWidget(bigTimeLabel);
    bigRow->addSpacing(10);
    changeLabel = new QLabel("");
    changeLabel->setStyleSheet("color:#30d158; font-size:12px;");
    bigRow->addWidget(changeLabel);
    bigRow->addStretch();
    cardLay->addLayout(bigRow);
    cardLay->addSpacing(6);

    chart = new QChart();
    chart->setBackgroundBrush(Qt::transparent);
    chart->legend()->hide();
    chart->setMargins(QMargins(0, 0, 0, 0));
    chart->setAnimationOptions(QChart::NoAnimation);
    chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setFixedHeight(150);
    chartView->setStyleSheet("background:transparent; border:none;");
    cardLay->addWidget(chartView);

    auto *sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color:#333;");
    cardLay->addWidget(sep);
    cardLay->addSpacing(6);

    auto *totalRow = new QHBoxLayout();
    totalLabel = new QLabel("Total Screen Time");
    totalLabel->setStyleSheet("color:#ccc; font-size:14px;");
    totalRow->addWidget(totalLabel);
    totalRow->addStretch();
    totalTimeLabel = new QLabel("0h 00m");
    totalTimeLabel->setStyleSheet("color:#ccc; font-size:14px; font-weight:bold;");
    totalRow->addWidget(totalTimeLabel);
    cardLay->addLayout(totalRow);
    cardLay->addSpacing(4);

    nowLabel = new QLabel("");
    nowLabel->setStyleSheet("color:#8e8e93; font-size:12px;");
    cardLay->addWidget(nowLabel);

    updatedLabel = new QLabel("");
    updatedLabel->setStyleSheet("color:#555; font-size:11px;");
    updatedLabel->setWordWrap(true);
    cardLay->addWidget(updatedLabel);

    lay->addWidget(card);
    lay->addSpacing(22);

    // ── Most Used ──
    mostUsedTitle = new QLabel("Most Used");
    mostUsedTitle->setStyleSheet("color:#ccc; font-size:17px; font-weight:bold;");
    lay->addWidget(mostUsedTitle);
    lay->addSpacing(8);

    appList = new QTreeWidget();
    appList->setColumnCount(3);
    appList->setHeaderHidden(true);
    appList->setRootIsDecorated(true);
    appList->setIndentation(18);
    appList->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    appList->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    appList->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    appList->setColumnWidth(0, 180);
    appList->setColumnWidth(2, 75);
    appList->setStyleSheet(
        "QTreeWidget{ background:#1c1c1e; border:none; border-radius:14px; font-size:14px; }"
        "QTreeWidget::item{ padding:10px 16px; min-height:32px; border-bottom:1px solid #2c2c2e; }"
        "QTreeWidget::item:selected{ background:#2c2c2e; }"
        "QTreeWidget::item:hover:!selected{ background:#232326; }");
    lay->addWidget(appList, 1);

    scroll->setWidget(page);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    onWeekClicked();
}

void ScreenTimeTab::onWeekClicked() {
    showingWeek = true;
    weekBtn->setStyleSheet("QPushButton{background:#555;color:#fff;border:none;border-radius:14px;"
                           "font-size:13px;font-weight:bold;padding:0 16px;}");
    dayBtn->setStyleSheet("QPushButton{background:transparent;color:#999;border:none;border-radius:14px;"
                          "font-size:13px;padding:0 16px;}");
    refresh();
}

void ScreenTimeTab::onDayClicked() {
    showingWeek = false;
    dayBtn->setStyleSheet("QPushButton{background:#555;color:#fff;border:none;border-radius:14px;"
                          "font-size:13px;font-weight:bold;padding:0 16px;}");
    weekBtn->setStyleSheet("QPushButton{background:transparent;color:#999;border:none;border-radius:14px;"
                           "font-size:13px;padding:0 16px;}");
    refresh();
}

// Week view: one bar per day for the last 7 days, dashed line at the daily average.
void ScreenTimeTab::buildWeekChart() {
    chart->removeAllSeries();
    for (auto *a : chart->axes()) chart->removeAxis(a);
    if (cached.last7.size() != 7) return;

    auto *barSet = new QBarSet("");
    barSet->setColor(QColor(0, 175, 255));
    barSet->setBorderColor(Qt::transparent);
    QStringList cats;
    int maxSec = 1;
    for (const auto &d : cached.last7) {
        *barSet << d.seconds / 3600.0;
        cats << QString::fromStdString(d.label);
        maxSec = std::max(maxSec, d.seconds);
    }

    auto *series = new QBarSeries();
    series->append(barSet);
    series->setBarWidth(0.55);
    chart->addSeries(series);

    auto *xAxis = new QBarCategoryAxis();
    xAxis->append(cats);
    xAxis->setLabelsColor(QColor(142, 142, 147));
    xAxis->setLabelsFont(QFont("", 10));
    xAxis->setGridLineVisible(false);
    xAxis->setLineVisible(false);
    chart->addAxis(xAxis, Qt::AlignBottom);
    series->attachAxis(xAxis);

    double maxH = std::max(1.0, maxSec / 3600.0) * 1.25;
    auto *yAxis = new QValueAxis();
    yAxis->setRange(0, maxH);
    yAxis->setLabelsColor(QColor(100, 100, 100));
    yAxis->setLabelFormat(maxH < 3.0 ? "%.1fh" : "%.0fh");
    yAxis->setTruncateLabels(false);
    yAxis->setGridLineColor(QColor(50, 50, 52));
    yAxis->setLineVisible(false);
    yAxis->setLabelsFont(QFont("", 9));
    yAxis->setTickCount(4);
    chart->addAxis(yAxis, Qt::AlignRight);
    series->attachAxis(yAxis);

    if (cached.dailyAverageSeconds > 60) {
        double avgH = cached.dailyAverageSeconds / 3600.0;
        auto *line = new QLineSeries();
        line->setPen(QPen(QColor(255, 69, 58), 1, Qt::DashDotLine));
        line->append(-0.5, avgH);
        line->append(6.5, avgH);
        chart->addSeries(line);
        line->attachAxis(xAxis);
        line->attachAxis(yAxis);
    }
}

// Day view: 24 bars, one per hour of today (minutes), current hour highlighted.
void ScreenTimeTab::buildDayChart() {
    chart->removeAllSeries();
    for (auto *a : chart->axes()) chart->removeAxis(a);
    if (cached.hourly.size() != 24) return;

    int nowHour = QDateTime::currentDateTime().time().hour();
    auto *past = new QBarSet("");
    past->setColor(QColor(0, 175, 255));
    past->setBorderColor(Qt::transparent);
    auto *current = new QBarSet("");
    current->setColor(QColor(48, 209, 88));
    current->setBorderColor(Qt::transparent);

    QStringList cats;
    int maxSec = 1;
    for (int h = 0; h < 24; ++h) {
        double mins = cached.hourly[h] / 60.0;
        *past << (h == nowHour ? 0.0 : mins);
        *current << (h == nowHour ? mins : 0.0);
        maxSec = std::max(maxSec, cached.hourly[h]);
        // Sparse labels like a phone: 12am, 6am, 12pm, 6pm
        QString label;
        if (h % 6 == 0) label = QString("%1%2").arg(h % 12 == 0 ? 12 : h % 12).arg(h < 12 ? "am" : "pm");
        else label = QString(h, ' ');  // categories must be unique; spaces are invisible
        cats << label;
    }

    auto *series = new QBarSeries();
    series->append(past);
    series->append(current);
    series->setBarWidth(0.9);
    chart->addSeries(series);

    auto *xAxis = new QBarCategoryAxis();
    xAxis->append(cats);
    xAxis->setLabelsColor(QColor(142, 142, 147));
    xAxis->setLabelsFont(QFont("", 9));
    xAxis->setGridLineVisible(false);
    xAxis->setLineVisible(false);
    xAxis->setTruncateLabels(false);
    chart->addAxis(xAxis, Qt::AlignBottom);
    series->attachAxis(xAxis);

    double maxMin = std::max(10.0, maxSec / 60.0);
    if (maxMin > 60.0) maxMin = 60.0;
    auto *yAxis = new QValueAxis();
    yAxis->setRange(0, maxMin * 1.15);
    yAxis->setLabelsColor(QColor(100, 100, 100));
    yAxis->setLabelFormat("%.0fm");
    yAxis->setTruncateLabels(false);
    yAxis->setGridLineColor(QColor(50, 50, 52));
    yAxis->setLineVisible(false);
    yAxis->setLabelsFont(QFont("", 9));
    yAxis->setTickCount(4);
    chart->addAxis(yAxis, Qt::AlignRight);
    series->attachAxis(yAxis);
}

void ScreenTimeTab::buildAppList() {
    QSet<QString> expandedApps;
    for (int i = 0; i < appList->topLevelItemCount(); ++i) {
        auto *it = appList->topLevelItem(i);
        if (it->isExpanded()) expandedApps.insert(it->text(0));
    }

    appList->setUpdatesEnabled(false);
    appList->clear();

    const auto &data = showingWeek ? cached.week : cached.today;
    int maxSec = 1;
    for (const auto &s : data) maxSec = std::max(maxSec, s.seconds);
    int totalRef = showingWeek ? cached.weekSeconds : cached.todaySeconds;

    for (const auto &s : data) {
        if (s.seconds <= 0) continue;
        auto *item = new QTreeWidgetItem();
        item->setText(0, QString::fromStdString(AppNames::display(s.app)));
        item->setForeground(0, QColor(230, 230, 230));
        QFont f;
        if (appList->topLevelItemCount() < 2) f.setBold(true);
        item->setFont(0, f);

        int barLen = std::max(1, static_cast<int>((static_cast<double>(s.seconds) / maxSec) * 28));
        item->setText(1, QString(barLen, QChar(0x2588)));

        QColor bc;
        double sharePct = (totalRef > 0) ? (static_cast<double>(s.seconds) / totalRef) * 100.0 : 0;
        if (sharePct > 35) { bc = QColor(255, 69, 58); item->setForeground(0, QColor(255, 120, 110)); }
        else if (sharePct > 25) { bc = QColor(255, 159, 10); item->setForeground(0, QColor(255, 200, 100)); }
        else if (sharePct > 15) bc = QColor(0, 175, 255);
        else if (sharePct > 8) bc = QColor(48, 209, 88);
        else bc = QColor(72, 72, 74);
        item->setForeground(1, bc);
        item->setText(2, fmtTime(s.seconds));
        item->setForeground(2, bc.lighter(130));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        appList->addTopLevelItem(item);

        for (const auto &tab : s.tabs) {
            if (tab.seconds <= 0) continue;
            auto *child = new QTreeWidgetItem(item);
            child->setText(0, QString::fromStdString(tab.name));
            child->setForeground(0, QColor(160, 160, 170));
            int tabBar = std::max(1, static_cast<int>((static_cast<double>(tab.seconds) / maxSec) * 20));
            child->setText(1, QString(tabBar, QChar(0x2588)));
            child->setForeground(1, QColor(70, 110, 160));
            child->setText(2, fmtTime(tab.seconds));
            child->setForeground(2, QColor(120, 120, 135));
            child->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        }
    }

    for (int i = 0; i < appList->topLevelItemCount(); ++i) {
        auto *it = appList->topLevelItem(i);
        if (expandedApps.contains(it->text(0))) it->setExpanded(true);
    }
    appList->setUpdatesEnabled(true);
}

void ScreenTimeTab::refresh() {
    if (!haveStats) return;

    auto colorFor = [](int sec, int red, int orange, int white) {
        if (sec > red) return "#ff453a";
        if (sec > orange) return "#ff9f0a";
        if (sec > white) return "#fff";
        return "#30d158";
    };

    if (showingWeek) {
        periodLabel->setText("Daily Average");
        bigTimeLabel->setText(fmtTime(cached.dailyAverageSeconds));
        totalLabel->setText("Total Screen Time (7 days)");
        totalTimeLabel->setText(fmtTime(cached.weekSeconds));
        QDate start = QDate::currentDate().addDays(-6), end = QDate::currentDate();
        changeLabel->setText(QString("%1 – %2").arg(start.toString("MMM d"), end.toString("MMM d")));
        changeLabel->setStyleSheet("color:#8e8e93; font-size:12px;");
        bigTimeLabel->setStyleSheet(QString("color:%1; font-size:34px; font-weight:bold;")
            .arg(colorFor(cached.dailyAverageSeconds, 6 * 3600, 4 * 3600, 2 * 3600)));
        mostUsedTitle->setText("Most Used This Week");
        buildWeekChart();
    } else {
        periodLabel->setText(QDate::currentDate().toString("'Today,' MMMM d"));
        bigTimeLabel->setText(fmtTime(cached.todaySeconds));
        totalLabel->setText("Total Screen Time Today");
        totalTimeLabel->setText(fmtTime(cached.todaySeconds));
        bigTimeLabel->setStyleSheet(QString("color:%1; font-size:34px; font-weight:bold;")
            .arg(colorFor(cached.todaySeconds, 8 * 3600, 5 * 3600, 2 * 3600)));

        if (cached.dailyAverageSeconds > 0) {
            int diff = cached.todaySeconds - cached.dailyAverageSeconds;
            if (diff > 0) {
                changeLabel->setText(QString("↑ %1 above daily avg").arg(fmtTime(diff)));
                changeLabel->setStyleSheet("color:#ff453a; font-size:12px;");
            } else if (diff < 0) {
                changeLabel->setText(QString("↓ %1 below daily avg").arg(fmtTime(-diff)));
                changeLabel->setStyleSheet("color:#30d158; font-size:12px;");
            } else {
                changeLabel->setText("= daily average");
                changeLabel->setStyleSheet("color:#8e8e93; font-size:12px;");
            }
        } else {
            changeLabel->setText("");
        }
        mostUsedTitle->setText("Most Used Today");
        buildDayChart();
    }

    if (cached.screenOff) {
        nowLabel->setText("Now: screen off / locked (paused)");
    } else if (cached.currentApp.empty()) {
        nowLabel->setText("Now: nothing focused");
    } else {
        QString now = "Now: " + QString::fromStdString(AppNames::display(cached.currentApp));
        if (!cached.currentTab.empty()) now += " — " + QString::fromStdString(cached.currentTab);
        nowLabel->setText(now);
    }

    QString status = cached.daemonRunning
        ? QString("Recorded by the background tracker (%1)").arg(QString::fromStdString(cached.source))
        : QString("Background tracker not running — time is only recorded while this window is open (%1)")
              .arg(QString::fromStdString(cached.source));
    updatedLabel->setText(QString("Updated %1  ·  %2")
        .arg(QDateTime::currentDateTime().toString("h:mm AP"), status));
    updatedLabel->setStyleSheet(cached.daemonRunning ? "color:#555; font-size:11px;" : "color:#ff9f0a; font-size:11px;");

    buildAppList();
}

void ScreenTimeTab::updateStats(const ScreenTimeStats &stats) {
    cached = stats;
    haveStats = true;
    refresh();
}
