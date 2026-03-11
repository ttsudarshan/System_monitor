#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QHeaderView>
#include <unordered_map>

class ProcessesTab : public QWidget {
    Q_OBJECT

public:
    explicit ProcessesTab(QWidget *parent = nullptr);
    void refresh();

private slots:
    void onKillProcess();
    void onFilterChanged(const QString &text);

private:
    QTableWidget *table;
    QLineEdit *filterEdit;
    QPushButton *killBtn;
    QString currentFilter;

    struct ProcInfo {
        int pid;
        QString name;
        QString user;
        double cpuPct;
        double memPct;
        QString status;
    };

    // Tracks previous CPU ticks per PID for delta calculation
    struct PrevCpuData {
        uint64_t utime = 0;
        uint64_t stime = 0;
        uint64_t totalSys = 0; // total system CPU time at that snapshot
    };
    std::unordered_map<int, PrevCpuData> prevCpuMap;

    // Read total system CPU time from /proc/stat
    uint64_t readTotalCpuTime();

    std::vector<ProcInfo> readProcesses();
    void populateTable(const std::vector<ProcInfo> &procs);
};