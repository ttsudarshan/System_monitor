#include "LogsTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDateTime>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>

LogsTab::LogsTab(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);

    // Alert configuration
    auto *alertGroup = new QGroupBox("Alert Thresholds");
    alertGroup->setStyleSheet("QGroupBox { color: white; font-weight: bold; }");
    auto *alertLayout = new QHBoxLayout(alertGroup);

    cpuAlertCheck = new QCheckBox("CPU >");
    cpuAlertCheck->setChecked(true);
    cpuAlertCheck->setStyleSheet("color: white;");
    cpuThreshSpin = new QSpinBox();
    cpuThreshSpin->setRange(1, 100);
    cpuThreshSpin->setValue(90);
    cpuThreshSpin->setSuffix("%");
    cpuThreshSpin->setStyleSheet("padding: 3px; background: #333; color: white; border: 1px solid #555;");
    alertLayout->addWidget(cpuAlertCheck);
    alertLayout->addWidget(cpuThreshSpin);

    alertLayout->addSpacing(12);

    memAlertCheck = new QCheckBox("Memory >");
    memAlertCheck->setChecked(true);
    memAlertCheck->setStyleSheet("color: white;");
    memThreshSpin = new QSpinBox();
    memThreshSpin->setRange(1, 100);
    memThreshSpin->setValue(90);
    memThreshSpin->setSuffix("%");
    memThreshSpin->setStyleSheet("padding: 3px; background: #333; color: white; border: 1px solid #555;");
    alertLayout->addWidget(memAlertCheck);
    alertLayout->addWidget(memThreshSpin);

    alertLayout->addSpacing(12);

    diskAlertCheck = new QCheckBox("Disk >");
    diskAlertCheck->setChecked(true);
    diskAlertCheck->setStyleSheet("color: white;");
    diskThreshSpin = new QSpinBox();
    diskThreshSpin->setRange(1, 100);
    diskThreshSpin->setValue(90);
    diskThreshSpin->setSuffix("%");
    diskThreshSpin->setStyleSheet("padding: 3px; background: #333; color: white; border: 1px solid #555;");
    alertLayout->addWidget(diskAlertCheck);
    alertLayout->addWidget(diskThreshSpin);

    alertLayout->addStretch();
    layout->addWidget(alertGroup);

    // Log view
    logView = new QTextEdit(this);
    logView->setReadOnly(true);
    logView->setStyleSheet(
        "background: #1e1e1e; color: #d4d4d4; font-family: monospace;"
        "font-size: 12px; border: 1px solid #444;");
    layout->addWidget(logView);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
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
    // CPU alert
    if (cpuAlertCheck->isChecked() && cpuPct > cpuThreshSpin->value()) {
        if (cpuAlertCooldown <= 0) {
            appendLog(QString("<span style='color:#EF5350;'>⚠ ALERT: CPU usage at %1% (threshold: %2%)</span>")
                .arg(cpuPct, 0, 'f', 1).arg(cpuThreshSpin->value()));
            cpuAlertCooldown = COOLDOWN_TICKS;
        }
    }
    if (cpuAlertCooldown > 0) cpuAlertCooldown--;

    // Memory alert
    if (memAlertCheck->isChecked() && memPct > memThreshSpin->value()) {
        if (memAlertCooldown <= 0) {
            appendLog(QString("<span style='color:#EF5350;'>⚠ ALERT: Memory usage at %1% (threshold: %2%)</span>")
                .arg(memPct, 0, 'f', 1).arg(memThreshSpin->value()));
            memAlertCooldown = COOLDOWN_TICKS;
        }
    }
    if (memAlertCooldown > 0) memAlertCooldown--;

    // Disk alert
    if (diskAlertCheck->isChecked() && diskMaxPct > diskThreshSpin->value()) {
        if (diskAlertCooldown <= 0) {
            appendLog(QString("<span style='color:#EF5350;'>⚠ ALERT: Disk usage at %1% (threshold: %2%)</span>")
                .arg(diskMaxPct, 0, 'f', 1).arg(diskThreshSpin->value()));
            diskAlertCooldown = COOLDOWN_TICKS;
        }
    }
    if (diskAlertCooldown > 0) diskAlertCooldown--;
}

void LogsTab::recordSnapshot(const SystemSnapshot &snap) {
    snapshots.push_back(snap);
}

void LogsTab::onExport() {
    if (snapshots.empty()) {
        QMessageBox::information(this, "Export", "No data collected yet. Wait a few seconds and try again.");
        return;
    }

    QString filename = QFileDialog::getSaveFileName(this, "Export Statistics",
        QDateTime::currentDateTime().toString("'sysmon_'yyyyMMdd_HHmmss'.csv'"),
        "CSV Files (*.csv)");

    if (filename.isEmpty()) return;

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Error", "Could not open file for writing.");
        return;
    }

    QTextStream out(&file);
    out << "Timestamp,CPU%,MemUsedMB,MemTotalMB,NetRxBps,NetTxBps,DiskReadBps,DiskWriteBps,ProcessCount\n";

    QString startTime = QDateTime::currentDateTime()
        .addSecs(-static_cast<int>(snapshots.size()) * 1.5)
        .toString("yyyy-MM-dd hh:mm:ss");

    for (size_t i = 0; i < snapshots.size(); ++i) {
        const auto &s = snapshots[i];
        QString ts = QDateTime::currentDateTime()
            .addSecs(-static_cast<int>((snapshots.size() - i) * 1.5))
            .toString("yyyy-MM-dd hh:mm:ss");
        out << ts << ","
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