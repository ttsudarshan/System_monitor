#pragma once

#include <QCheckBox>
#include <QDateTime>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QWidget>
#include <deque>

class LogsTab : public QWidget {
    Q_OBJECT

public:
    explicit LogsTab(QWidget *parent = nullptr);

    void appendLog(const QString &msg);

    // Alert checking: call from MainWindow on each refresh
    void checkAlerts(double cpuPct, double memPct, double diskMaxPct);

    struct SystemSnapshot {
        QDateTime time;
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
    QLabel *snapshotCountLabel;

    QCheckBox *cpuAlertCheck;
    QSpinBox *cpuThreshSpin;
    QCheckBox *memAlertCheck;
    QSpinBox *memThreshSpin;
    QCheckBox *diskAlertCheck;
    QSpinBox *diskThreshSpin;

    int cpuAlertCooldown = 0;
    int memAlertCooldown = 0;
    int diskAlertCooldown = 0;
    static constexpr int COOLDOWN_TICKS = 10;  // ~15 seconds at 1.5s refresh

    // Bounded history for export: 24 h at 1.5 s per sample.
    static constexpr size_t MAX_SNAPSHOTS = 24 * 3600 * 2 / 3;
    std::deque<SystemSnapshot> snapshots;
};
