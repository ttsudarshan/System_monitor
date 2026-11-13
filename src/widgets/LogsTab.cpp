#include "LogsTab.h"

#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTextStream>
#include <QVBoxLayout>

LogsTab::LogsTab(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);

    auto *alertGroup = new QGroupBox("Alert Thresholds");
    alertGroup->setStyleSheet("QGroupBox { color: white; font-weight: bold; }");
    auto *alertLayout = new QHBoxLayout(alertGroup);

    auto addAlert = [&](const QString &label, QCheckBox *&check, QSpinBox *&spin) {
        check = new QCheckBox(label);
        check->setChecked(true);
        check->setStyleSheet("color: white;");
        spin = new QSpinBox();
        spin->setRange(1, 100);
        spin->setValue(90);
        spin->setSuffix("%");
        spin->setStyleSheet("padding: 3px; background: #333; color: white; border: 1px solid #555;");
        alertLayout->addWidget(check);
        alertLayout->addWidget(spin);
        alertLayout->addSpacing(12);
    };
    addAlert("CPU >", cpuAlertCheck, cpuThreshSpin);
    addAlert("Memory >", memAlertCheck, memThreshSpin);
    addAlert("Disk >", diskAlertCheck, diskThreshSpin);
    alertLayout->addStretch();
    layout->addWidget(alertGroup);

    logView = new QTextEdit(this);
    logView->setReadOnly(true);
    logView->setStyleSheet(
        "background: #1e1e1e; color: #d4d4d4; font-family: monospace;"
        "font-size: 12px; border: 1px solid #444;");
    layout->addWidget(logView);

    auto *btnLayout = new QHBoxLayout();
    snapshotCountLabel = new QLabel("");
    snapshotCountLabel->setStyleSheet("color: #78909C; font-size: 11px;");
    btnLayout->addWidget(snapshotCountLabel);
    btnLayout->addStretch();

    exportBtn = new QPushButton("Export Stats (CSV)");
    exportBtn->setStyleSheet("padding: 6px 16px; background: #42A5F5; color: white; border: none; border-radius: 4px;");
    connect(exportBtn, &QPushButton::clicked, this, &LogsTab::onExport);
    btnLayout->addWidget(exportBtn);

    clearBtn = new QPushButton("Clear Logs");
    clearBtn->setStyleSheet("padding: 6px 16px; background: #555; color: white; border: none; border-radius: 4px;");
    connect(clearBtn, &QPushButton::clicked, this, &LogsTab::onClear);
    btnLayout->addWidget(clearBtn);
    layout->addLayout(btnLayout);

    appendLog("System Monitor started.");
}

void LogsTab::appendLog(const QString &msg) {
    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    logView->append(QString("[%1] %2").arg(ts, msg));
}

void LogsTab::checkAlerts(double cpuPct, double memPct, double diskMaxPct) {
    auto check = [this](QCheckBox *enabled, QSpinBox *thresh, double value, int &cooldown, const char *what) {
        if (enabled->isChecked() && value > thresh->value() && cooldown <= 0) {
            appendLog(QString("<span style='color:#EF5350;'>⚠ ALERT: %1 usage at %2% (threshold: %3%)</span>")
                .arg(what).arg(value, 0, 'f', 1).arg(thresh->value()));
            cooldown = COOLDOWN_TICKS;
        }
        if (cooldown > 0) cooldown--;
    };
    check(cpuAlertCheck, cpuThreshSpin, cpuPct, cpuAlertCooldown, "CPU");
    check(memAlertCheck, memThreshSpin, memPct, memAlertCooldown, "Memory");
    check(diskAlertCheck, diskThreshSpin, diskMaxPct, diskAlertCooldown, "Disk");
}

void LogsTab::recordSnapshot(const SystemSnapshot &snap) {
    snapshots.push_back(snap);
    while (snapshots.size() > MAX_SNAPSHOTS) snapshots.pop_front();
    if (snapshots.size() % 10 == 1)
        snapshotCountLabel->setText(QString("%1 samples since %2")
            .arg(snapshots.size()).arg(snapshots.front().time.toString("hh:mm:ss")));
}

void LogsTab::onExport() {
    if (snapshots.empty()) {
        QMessageBox::information(this, "Export", "No data collected yet. Wait a few seconds and try again.");
        return;
    }
    QString filename = QFileDialog::getSaveFileName(this, "Export Statistics",
        QDateTime::currentDateTime().toString("'sysmon_'yyyyMMdd_HHmmss'.csv'"), "CSV Files (*.csv)");
    if (filename.isEmpty()) return;

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Error", "Could not open file for writing.");
        return;
    }
    QTextStream out(&file);
    out << "Timestamp,CPU%,MemUsedMB,MemTotalMB,NetRxBps,NetTxBps,DiskReadBps,DiskWriteBps,ProcessCount\n";
    for (const auto &s : snapshots) {
        out << s.time.toString("yyyy-MM-dd hh:mm:ss") << ","
            << QString::number(s.cpuPct, 'f', 1) << ","
            << QString::number(s.memUsedMB, 'f', 0) << ","
            << QString::number(s.memTotalMB, 'f', 0) << ","
            << QString::number(s.netRxBps, 'f', 0) << ","
            << QString::number(s.netTxBps, 'f', 0) << ","
            << QString::number(s.diskReadBps, 'f', 0) << ","
            << QString::number(s.diskWriteBps, 'f', 0) << ","
            << s.processCount << "\n";
    }
    file.close();
    appendLog(QString("Exported %1 snapshots to %2").arg(snapshots.size()).arg(filename));
    QMessageBox::information(this, "Export Complete",
        QString("Exported %1 data points to:\n%2").arg(snapshots.size()).arg(filename));
}

void LogsTab::onClear() {
    logView->clear();
}
