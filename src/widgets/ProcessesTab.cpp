#include "ProcessesTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDir>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <signal.h>
#include <pwd.h>
#include <sys/sysinfo.h>
#include <algorithm>

ProcessesTab::ProcessesTab(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);

    // Filter bar
    auto *toolbar = new QHBoxLayout();
    filterEdit = new QLineEdit();
    filterEdit->setPlaceholderText("Filter by name or PID...");
    filterEdit->setStyleSheet(
        "padding: 6px; border: 1px solid #555; border-radius: 4px;"
        "color: white; background: #333;");
    connect(filterEdit, &QLineEdit::textChanged, this, &ProcessesTab::onFilterChanged);
    toolbar->addWidget(filterEdit);

    killBtn = new QPushButton("Kill Process");
    killBtn->setStyleSheet(
        "padding: 6px 16px; background: #EF5350; color: white;"
        "border: none; border-radius: 4px;");
    connect(killBtn, &QPushButton::clicked, this, &ProcessesTab::onKillProcess);
    toolbar->addWidget(killBtn);
    layout->addLayout(toolbar);

    // Process table
    table = new QTableWidget(this);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({"PID", "Name", "User", "CPU%", "MEM%", "Status"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSortingEnabled(true);
    table->setAlternatingRowColors(true);
    table->setStyleSheet(
        "QTableWidget { gridline-color: #444; }"
        "QHeaderView::section { background: #333; color: white;"
        "padding: 4px; border: 1px solid #444; }");
    layout->addWidget(table);
}

uint64_t ProcessesTab::readTotalCpuTime() {
    std::ifstream f("/proc/stat");
    if (!f.is_open()) return 0;

    std::string line;
    std::getline(f, line);

    // "cpu  user nice system idle iowait irq softirq steal ..."
    std::istringstream ss(line);
    std::string label;
    ss >> label;

    uint64_t total = 0, val;
    while (ss >> val) total += val;
    return total;
}

std::vector<ProcessesTab::ProcInfo> ProcessesTab::readProcesses() {
    std::vector<ProcInfo> procs;

    struct sysinfo si;
    sysinfo(&si);
    long totalMemKB = si.totalram * si.mem_unit / 1024;
    long pageSize = sysconf(_SC_PAGESIZE);
    int numCpus = sysconf(_SC_NPROCESSORS_ONLN);

    // Current total system CPU time
    uint64_t totalCpuNow = readTotalCpuTime();

    // New map for this cycle
    std::unordered_map<int, PrevCpuData> newCpuMap;

    QDir procDir("/proc");
    for (const QString &entry : procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok;
        int pid = entry.toInt(&ok);
        if (!ok) continue;

        ProcInfo p;
        p.pid = pid;

        // --- Read /proc/[pid]/stat ---
        std::string statPath = "/proc/" + std::to_string(pid) + "/stat";
        std::ifstream statF(statPath);
        if (!statF.is_open()) continue;

        std::string statLine;
        std::getline(statF, statLine);

        auto lp = statLine.find('(');
        auto rp = statLine.rfind(')');
        if (lp == std::string::npos || rp == std::string::npos) continue;

        p.name = QString::fromStdString(statLine.substr(lp + 1, rp - lp - 1));

        std::istringstream rest(statLine.substr(rp + 2));
        std::string state;
        rest >> state;

        switch (state[0]) {
            case 'R': p.status = "Running"; break;
            case 'S': p.status = "Sleeping"; break;
            case 'D': p.status = "Disk Sleep"; break;
            case 'Z': p.status = "Zombie"; break;
            case 'T': p.status = "Stopped"; break;
            case 'I': p.status = "Idle"; break;
            default:  p.status = QString::fromStdString(state); break;
        }

        // Skip fields: ppid, pgrp, session, tty_nr, tpgid, flags,
        //              minflt, cminflt, majflt, cmajflt = 10 fields
        uint64_t dummy;
        for (int i = 0; i < 10; ++i) rest >> dummy;

        uint64_t utime, stime;
        rest >> utime >> stime;

        // --- Compute CPU% using delta ---
        PrevCpuData cur;
        cur.utime = utime;
        cur.stime = stime;
        cur.totalSys = totalCpuNow;

        auto it = prevCpuMap.find(pid);
        if (it != prevCpuMap.end()) {
            uint64_t procDelta = (utime + stime) - (it->second.utime + it->second.stime);
            uint64_t sysDelta = totalCpuNow - it->second.totalSys;

            if (sysDelta > 0) {
                // Multiply by numCpus so a process using one full core = ~100%
                p.cpuPct = (static_cast<double>(procDelta) / sysDelta) * 100.0 * numCpus;
                // Clamp to reasonable range
                if (p.cpuPct > 100.0 * numCpus) p.cpuPct = 100.0 * numCpus;
                if (p.cpuPct < 0.0) p.cpuPct = 0.0;
            } else {
                p.cpuPct = 0.0;
            }
        } else {
            p.cpuPct = 0.0; // First reading, no delta yet
        }

        newCpuMap[pid] = cur;

        // --- Memory % from /proc/[pid]/statm ---
        std::string statmPath = "/proc/" + std::to_string(pid) + "/statm";
        std::ifstream statmF(statmPath);
        if (statmF.is_open()) {
            uint64_t size, rss;
            statmF >> size >> rss;
            double rssKB = rss * pageSize / 1024.0;
            p.memPct = (totalMemKB > 0) ? (rssKB / totalMemKB) * 100.0 : 0.0;
        } else {
            p.memPct = 0.0;
        }

        // --- User from /proc/[pid]/status ---
        std::string statusPath = "/proc/" + std::to_string(pid) + "/status";
        std::ifstream statusF(statusPath);
        p.user = "?";
        if (statusF.is_open()) {
            std::string sLine;
            while (std::getline(statusF, sLine)) {
                if (sLine.substr(0, 4) == "Uid:") {
                    std::istringstream us(sLine.substr(5));
                    uid_t uid;
                    us >> uid;
                    struct passwd *pw = getpwuid(uid);
                    if (pw) p.user = QString(pw->pw_name);
                    break;
                }
            }
        }

        procs.push_back(p);
    }

    // Replace old map with new
    prevCpuMap = std::move(newCpuMap);

    // Sort by CPU% descending by default
    std::sort(procs.begin(), procs.end(), [](const ProcInfo &a, const ProcInfo &b) {
        return a.cpuPct > b.cpuPct;
    });

    return procs;
}

void ProcessesTab::populateTable(const std::vector<ProcInfo> &procs) {
    table->setSortingEnabled(false);
    table->setRowCount(static_cast<int>(procs.size()));

    for (int i = 0; i < static_cast<int>(procs.size()); ++i) {
        const auto &p = procs[i];

        auto *pidItem = new QTableWidgetItem();
        pidItem->setData(Qt::DisplayRole, p.pid);
        table->setItem(i, 0, pidItem);

        table->setItem(i, 1, new QTableWidgetItem(p.name));
        table->setItem(i, 2, new QTableWidgetItem(p.user));

        auto *cpuItem = new QTableWidgetItem();
        cpuItem->setData(Qt::DisplayRole, QString::number(p.cpuPct, 'f', 1));
        table->setItem(i, 3, cpuItem);

        auto *memItem = new QTableWidgetItem();
        memItem->setData(Qt::DisplayRole, QString::number(p.memPct, 'f', 1));
        table->setItem(i, 4, memItem);

        table->setItem(i, 5, new QTableWidgetItem(p.status));
    }
    table->setSortingEnabled(true);
}

void ProcessesTab::refresh() {
    auto procs = readProcesses();

    if (!currentFilter.isEmpty()) {
        std::vector<ProcInfo> filtered;
        for (const auto &p : procs) {
            if (p.name.contains(currentFilter, Qt::CaseInsensitive) ||
                QString::number(p.pid).contains(currentFilter))
                filtered.push_back(p);
        }
        populateTable(filtered);
    } else {
        populateTable(procs);
    }
}

void ProcessesTab::onKillProcess() {
    int row = table->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "No Selection", "Please select a process to kill.");
        return;
    }

    int pid = table->item(row, 0)->data(Qt::DisplayRole).toInt();
    QString name = table->item(row, 1)->text();

    auto reply = QMessageBox::question(this, "Kill Process",
        QString("Terminate %1 (PID %2)?").arg(name).arg(pid),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (kill(pid, SIGTERM) != 0) {
            QMessageBox::warning(this, "Error",
                QString("Failed to kill PID %1. Permission denied?").arg(pid));
        }
    }
}

void ProcessesTab::onFilterChanged(const QString &text) {
    currentFilter = text;
    refresh();
}