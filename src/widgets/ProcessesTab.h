#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QHeaderView>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <unordered_map>
#include <set>
#include <map>

class ProcessesTab : public QWidget {
    Q_OBJECT

public:
    explicit ProcessesTab(QWidget *parent = nullptr);
    void refresh();

private slots:
    void onKillProcess();
    void onKillGroup();
    void onSuspendProcess();
    void onResumeProcess();
    void onReniceProcess();
    void onFilterChanged(const QString &text);

private:
    QTreeWidget *tree;
    QLineEdit *filterEdit;
    QPushButton *killBtn;
    QPushButton *killGroupBtn;
    QPushButton *suspendBtn;
    QPushButton *resumeBtn;
    QPushButton *reniceBtn;
    QSpinBox *niceSpin;
    QCheckBox *hideSystemCheck;
    QLabel *countLabel;
    QString currentFilter;

    struct ProcInfo {
        int pid;
        int ppid;
        QString name;
        QString user;
        double cpuPct;
        double memPct;
        double memMB;
        QString status;
        int nice;
        QString cmdline;
        QString processType;
        QString displayLabel;  // Final label shown in tree
    };

    struct PrevCpuData {
        uint64_t utime = 0;
        uint64_t stime = 0;
        uint64_t totalSys = 0;
    };
    std::unordered_map<int, PrevCpuData> prevCpuMap;

    // Persist UI state across refreshes
    std::set<QString> expandedApps;
    int selectedPid = -1;
    QString selectedText;  // text of selected row for fallback matching
    int scrollPos = 0;

    uint64_t readTotalCpuTime();
    std::vector<ProcInfo> readProcesses();
    void populateGrouped(const std::vector<ProcInfo> &procs);

    void saveState();
    void restoreState();

    int getSelectedPid();
    QString getSelectedName();

    // Classification helpers
    static QString parseCmdline(int pid);
    static QString classifyBrowserProcess(const QString &cmdline);
    static QString classifyCodeProcess(const QString &cmdline);
    static QString normalizeAppName(const QString &name);

    // X11 window title lookup: pid -> window title
    static std::map<int, QString> getWindowTitles();

    // Read Brave/Chrome's internal title from /proc/[pid]/cmdline --site flags
    static QString extractSiteFromCmdline(const QString &cmdline);

    // Read all open tab titles and build renderer pid -> title map
    static std::map<int, QString> buildRendererTitleMap(
        const std::vector<ProcInfo> &procs,
        const std::map<int, QString> &winTitles);
};