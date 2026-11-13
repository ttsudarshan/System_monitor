#pragma once

#include "core/ProcScan.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QWidget>
#include <set>
#include <vector>

class ProcessesTab : public QWidget {
    Q_OBJECT

public:
    explicit ProcessesTab(QWidget *parent = nullptr);

    // Called with the latest /proc scan (produced on the worker thread).
    void refresh(const std::vector<ProcInfo> &procs);

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

    struct Row {
        const ProcInfo *proc = nullptr;
        QString displayName;   // group name or process label
        QString processType;   // "Tab", "GPU Process", ...
        QString status;
    };

    std::vector<ProcInfo> lastProcs;   // kept so a re-filter does not need a new scan
    unsigned myUid;

    // Persist UI state across refreshes
    std::set<QString> expandedApps;
    int selectedPid = -1;
    QString selectedText;
    int scrollPos = 0;

    void rebuild();
    void populateGrouped(const std::vector<const ProcInfo *> &procs);
    void saveState();
    void restoreState();
    int getSelectedPid();
    QString getSelectedName();

    static QString statusText(char state);
    static QString classifyBrowserProcess(const QString &cmdline);
    static QString classifyCodeProcess(const QString &cmdline);
    static QString processType(const ProcInfo &p);
    static QString extractSiteFromCmdline(const QString &cmdline);
};
