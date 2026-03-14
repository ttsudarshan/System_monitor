#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include <QCheckBox>

class LogsTab : public QWidget {
    Q_OBJECT

public:
    explicit LogsTab(QWidget *parent = nullptr);

    void appendLog(const QString &msg);

    // Alert checking — call from MainWindow on each refresh
    void checkAlerts(double cpuPct, double memPct, double diskMaxPct);

    // Export stats
    struct SystemSnapshot {
        double cpuPct;
        double memUsedMB;
        double memTotalMB;
        double netRxBps;
        double netTxBps;
        double diskReadBps;
        double diskWriteBps;
        int processCount;
    };
    void recordSnapshot(const SystemSnapshot &snap);

private slots:
    void onClear();
    void onExport();

private:
    QTextEdit *logView;
    QPushButton *clearBtn;
    QPushButton *exportBtn;

    // Alert settings
    QCheckBox *cpuAlertCheck;
    QSpinBox *cpuThreshSpin;
    QCheckBox *memAlertCheck;
    QSpinBox *memThreshSpin;
    QCheckBox *diskAlertCheck;
    QSpinBox *diskThreshSpin;

    // Cooldown to avoid spamming alerts
    int cpuAlertCooldown = 0;
    int memAlertCooldown = 0;
    int diskAlertCooldown = 0;
    static constexpr int COOLDOWN_TICKS = 10; // ~15 seconds at 1.5s refresh

    // Snapshot history for export
    std::vector<SystemSnapshot> snapshots;
};