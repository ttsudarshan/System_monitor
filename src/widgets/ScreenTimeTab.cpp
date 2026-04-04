#include "ScreenTimeTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollArea>
#include <QDateTime>
#include <QDate>
#include <QFont>
#include <QFrame>
#include <algorithm>

ScreenTimeTab::ScreenTimeTab(QWidget *parent) : QWidget(parent) {
    setupUI();
}

QString ScreenTimeTab::fmtTime(int sec) {
    if (sec < 60) return QString("%1s").arg(sec);
    int m = sec / 60, h = m / 60;
    m %= 60;
    if (h > 0) return QString("%1h %2m").arg(h).arg(m, 2, 10, QChar('0'));
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

    // ── "Screen Time" label ──
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

    // Chart
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

    // Separator
    auto *sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color:#333;");
    cardLay->addWidget(sep);
    cardLay->addSpacing(6);

    // Total row
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

    updatedLabel = new QLabel("");
    updatedLabel->setStyleSheet("color:#555; font-size:11px;");
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
    appList->setRootIsDecorated(false);
    appList->setIndentation(28);
    appList->setUniformRowHeights(false);

    appList->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    appList->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    appList->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    appList->setColumnWidth(0, 170);
    appList->setColumnWidth(2, 75);

    appList->setStyleSheet(
        "QTreeWidget{"
        "  background:#1c1c1e; border:none; border-radius:14px; font-size:14px;"
        "}"
        "QTreeWidget::item{"
        "  padding:9px 14px; min-height:30px;"
        "  border-bottom:1px solid #2c2c2e;"
        "}"
        "QTreeWidget::item:last{ border-bottom:none; }"
        "QTreeWidget::item:selected{ background:#2c2c2e; }"
        "QTreeWidget::item:hover:!selected{ background:#232326; }"
        // Branch indicators (show/hide toggle arrows) — styled as chevrons
        "QTreeWidget::branch:has-children:!has-siblings:closed,"
        "QTreeWidget::branch:closed:has-children:has-siblings{"
        "  border-image:none; image:none;"
        "}"
        "QTreeWidget::branch:open:has-children:!has-siblings,"
        "QTreeWidget::branch:open:has-children:has-siblings{"
        "  border-image:none; image:none;"
        "}");

    // Toggle browser tab rows when clicked
    connect(appList, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int /*col*/) {
        // Only act on top-level browser items that have children
        if (item->parent() != nullptr) return;
        if (item->childCount() == 0 && !item->data(0, Qt::UserRole+1).toBool()) return;

        QString browserName = item->data(0, Qt::UserRole).toString();
        if (browserName.isEmpty()) return;

        bool wasExpanded = m_expandedBrowsers.contains(browserName);
        if (wasExpanded) {
            m_expandedBrowsers.remove(browserName);
        } else {
            m_expandedBrowsers.insert(browserName);
        }
        buildAppList();
    });

    lay->addWidget(appList, 1);

    scroll->setWidget(page);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    // Initial toggle style
    onWeekClicked();
}

void ScreenTimeTab::onWeekClicked() {
    showingWeek = true;
    weekBtn->setStyleSheet(
        "QPushButton{background:#555;color:#fff;border:none;border-radius:14px;"
        "font-size:13px;font-weight:bold;padding:0 16px;}");
    dayBtn->setStyleSheet(
        "QPushButton{background:transparent;color:#999;border:none;border-radius:14px;"
        "font-size:13px;padding:0 16px;}");
    refresh();
}

void ScreenTimeTab::onDayClicked() {
    showingWeek = false;
    dayBtn->setStyleSheet(
        "QPushButton{background:#555;color:#fff;border:none;border-radius:14px;"
        "font-size:13px;font-weight:bold;padding:0 16px;}");
    weekBtn->setStyleSheet(
        "QPushButton{background:transparent;color:#999;border:none;border-radius:14px;"
        "font-size:13px;padding:0 16px;}");
    refresh();
}

void ScreenTimeTab::buildChart() {
    chart->removeAllSeries();
    for (auto *a : chart->axes()) chart->removeAxis(a);
    if (cachedLast7.size() != 7) return;

    auto *barSet = new QBarSet("");
    barSet->setColor(QColor(0, 175, 255));
    barSet->setBorderColor(Qt::transparent);

    QStringList cats;
    int maxSec = 1;

    // Always 7 bars
    for (int i = 0; i < 7; ++i) {
        double hrs = cachedLast7[i].seconds / 3600.0;
        *barSet << hrs;
        cats << QString::fromStdString(cachedLast7[i].dayLabel);
        if (cachedLast7[i].seconds > maxSec) maxSec = cachedLast7[i].seconds;
    }

    auto *series = new QStackedBarSeries();
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

    // Ensure Y axis always shows at least 1h even if no data
    double maxH = maxSec / 3600.0;
    if (maxH < 1.0) maxH = 1.0;
    maxH *= 1.25;
    auto *yAxis = new QValueAxis();
    yAxis->setRange(0, maxH);
    yAxis->setLabelsColor(QColor(100, 100, 100));
    yAxis->setLabelFormat("%.0fh");
    yAxis->setGridLineColor(QColor(50, 50, 52));
    yAxis->setLineVisible(false);
    yAxis->setLabelsFont(QFont("", 9));
    yAxis->setTickCount(4);
    chart->addAxis(yAxis, Qt::AlignRight);
    series->attachAxis(yAxis);

    // Avg dashed line in red/orange
    if (cachedDailyAvg > 60) {
        double avgH = cachedDailyAvg / 3600.0;
        auto *line = new QLineSeries();
        line->setPen(QPen(QColor(255, 69, 58), 1, Qt::DashDotLine));
        line->append(-0.5, avgH);
        line->append(6.5, avgH);
        chart->addSeries(line);
        line->attachAxis(xAxis);
        line->attachAxis(yAxis);
    }
}

void ScreenTimeTab::buildAppList() {
    appList->clear();

    const auto &data = showingWeek ? cachedWeekly : cachedToday;
    if (data.empty()) return;

    const auto &browserTabs = showingWeek ? cachedBrowserTabsWeekly : cachedBrowserTabsToday;

    int maxSec = 0;
    for (auto &s : data) {
        int t = showingWeek ? s.weekSeconds : s.todaySeconds;
        if (t > maxSec) maxSec = t;
    }
    if (maxSec <= 0) maxSec = 1;

    // Browser name set for quick lookup
    auto isBrowserApp = [](const std::string &n) {
        return n == "google-chrome" || n == "chromium" ||
               n == "brave"         || n == "firefox"  ||
               n == "microsoft-edge";
    };

    for (auto &s : data) {
        int sec = showingWeek ? s.weekSeconds : s.todaySeconds;
        if (sec <= 0) continue;

        int totalRef = showingWeek ? cachedWeekSec : cachedTodaySec;
        double sharePct = (totalRef > 0) ? (static_cast<double>(sec) / totalRef) * 100.0 : 0;

        QString qName = QString::fromStdString(s.appName);
        bool hasTabs = isBrowserApp(s.appName) &&
                       browserTabs.count(s.appName) && !browserTabs.at(s.appName).empty();
        bool expanded = hasTabs && m_expandedBrowsers.contains(qName);

        auto *item = new QTreeWidgetItem();

        // ── Name column ──
        QString displayName = qName;
        if (hasTabs) {
            displayName = (expanded ? "▼  " : "▶  ") + qName;
            // Store clean browser name for click handler
            item->setData(0, Qt::UserRole,    qName);
            item->setData(0, Qt::UserRole + 1, true);  // isBrowserItem flag
        }
        item->setText(0, displayName);
        item->setForeground(0, QColor(230, 230, 230));
        QFont f;
        if (appList->topLevelItemCount() < 2) f.setBold(true);
        item->setFont(0, f);

        // ── Bar column ──
        int barLen = static_cast<int>((static_cast<double>(sec) / maxSec) * 24);
        if (barLen < 1) barLen = 1;
        item->setText(1, QString(barLen, QChar(0x2588)));

        // Color by usage share
        QColor bc;
        if (sharePct > 35)      { bc = QColor(255, 69, 58);  item->setForeground(0, QColor(255, 120, 110)); }
        else if (sharePct > 25) { bc = QColor(255, 159, 10); item->setForeground(0, QColor(255, 200, 100)); }
        else if (sharePct > 15)   bc = QColor(0, 175, 255);
        else if (sharePct > 8)    bc = QColor(48, 209, 88);
        else                      bc = QColor(72, 72, 74);
        item->setForeground(1, bc);

        // ── Time column ──
        item->setText(2, fmtTime(sec));
        item->setForeground(2, bc.lighter(130));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);

        appList->addTopLevelItem(item);

        // ── Browser sub-tabs ──
        if (hasTabs && expanded) {
            const auto &tabs = browserTabs.at(s.appName);
            int tabMaxSec = 1;
            for (auto &tab : tabs) {
                int ts = showingWeek ? tab.weekSeconds : tab.todaySeconds;
                if (ts > tabMaxSec) tabMaxSec = ts;
            }

            for (auto &tab : tabs) {
                int tabSec = showingWeek ? tab.weekSeconds : tab.todaySeconds;
                if (tabSec <= 0) continue;

                auto *child = new QTreeWidgetItem(item);

                // Site name with a bullet
                child->setText(0, "· " + QString::fromStdString(tab.site));
                child->setForeground(0, QColor(180, 180, 185));
                QFont sf;
                sf.setPointSize(12);
                child->setFont(0, sf);

                // Smaller bar
                int cBarLen = static_cast<int>((static_cast<double>(tabSec) / tabMaxSec) * 18);
                if (cBarLen < 1) cBarLen = 1;
                child->setText(1, QString(cBarLen, QChar(0x2588)));
                child->setForeground(1, bc.darker(110));

                child->setText(2, fmtTime(tabSec));
                child->setForeground(2, QColor(150, 150, 155));
                child->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);

                child->setData(0, Qt::UserRole + 1, false); // not a browser toggle item
            }

            item->setExpanded(true);
        }
    }
}

void ScreenTimeTab::refresh() {
    if (showingWeek) {
        periodLabel->setText("Daily Average");
        bigTimeLabel->setText(fmtTime(cachedDailyAvg));
        totalTimeLabel->setText(fmtTime(cachedWeekSec));

        QDate start = QDate::currentDate().addDays(-6);
        QDate end = QDate::currentDate();
        changeLabel->setText(QString("%1 – %2")
            .arg(start.toString("MMM d"))
            .arg(end.toString("MMM d")));
        changeLabel->setStyleSheet("color:#30d158; font-size:12px;");

        // Color big time by daily average level
        if (cachedDailyAvg > 6 * 3600)
            bigTimeLabel->setStyleSheet("color:#ff453a; font-size:34px; font-weight:bold;");
        else if (cachedDailyAvg > 4 * 3600)
            bigTimeLabel->setStyleSheet("color:#ff9f0a; font-size:34px; font-weight:bold;");
        else if (cachedDailyAvg > 2 * 3600)
            bigTimeLabel->setStyleSheet("color:#fff; font-size:34px; font-weight:bold;");
        else
            bigTimeLabel->setStyleSheet("color:#30d158; font-size:34px; font-weight:bold;");

    } else {
        periodLabel->setText(QDate::currentDate().toString("'Today,' MMMM d"));
        bigTimeLabel->setText(fmtTime(cachedTodaySec));
        totalTimeLabel->setText(fmtTime(cachedTodaySec));

        // Color big time by today's level
        if (cachedTodaySec > 8 * 3600)
            bigTimeLabel->setStyleSheet("color:#ff453a; font-size:34px; font-weight:bold;");
        else if (cachedTodaySec > 5 * 3600)
            bigTimeLabel->setStyleSheet("color:#ff9f0a; font-size:34px; font-weight:bold;");
        else if (cachedTodaySec > 2 * 3600)
            bigTimeLabel->setStyleSheet("color:#fff; font-size:34px; font-weight:bold;");
        else
            bigTimeLabel->setStyleSheet("color:#30d158; font-size:34px; font-weight:bold;");

        if (cachedDailyAvg > 0) {
            int diff = cachedTodaySec - cachedDailyAvg;
            if (diff > 0) {
                changeLabel->setText(QString("↑ %1 above avg").arg(fmtTime(diff)));
                changeLabel->setStyleSheet("color:#ff453a; font-size:12px;");
            } else if (diff < 0) {
                changeLabel->setText(QString("↓ %1 below avg").arg(fmtTime(-diff)));
                changeLabel->setStyleSheet("color:#30d158; font-size:12px;");
            } else {
                changeLabel->setText("= daily average");
                changeLabel->setStyleSheet("color:#8e8e93; font-size:12px;");
            }
        } else {
            changeLabel->setText("");
        }
    }

    updatedLabel->setText(QString("Updated %1")
        .arg(QDateTime::currentDateTime().toString("'today at' h:mm AP")));

    buildChart();
    buildAppList();
}

void ScreenTimeTab::updateStats(
    const std::vector<ScreenTimeTracker::AppScreenTime> &today,
    const std::vector<ScreenTimeTracker::AppScreenTime> &weekly,
    int totalTodaySec, int totalWeekSec,
    const std::string &currentApp,
    const std::vector<ScreenTimeTracker::DailyTotal> &last7,
    int dailyAvgSec,
    const std::map<std::string, std::vector<ScreenTimeTracker::BrowserTabTime>> &browserTabsToday,
    const std::map<std::string, std::vector<ScreenTimeTracker::BrowserTabTime>> &browserTabsWeekly)
{
    cachedToday            = today;
    cachedWeekly           = weekly;
    cachedTodaySec         = totalTodaySec;
    cachedWeekSec          = totalWeekSec;
    cachedLast7            = last7;
    cachedDailyAvg         = dailyAvgSec;
    cachedBrowserTabsToday   = browserTabsToday;
    cachedBrowserTabsWeekly  = browserTabsWeekly;
    (void)currentApp;
    refresh();
}