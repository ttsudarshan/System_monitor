#include "ProcessesTab.h"
#include "core/AppNames.h"

#include <QFont>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollBar>
#include <QVBoxLayout>
#include <algorithm>
#include <errno.h>
#include <functional>
#include <map>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

// ─── Site extraction from a Chromium renderer command line ──────

QString ProcessesTab::extractSiteFromCmdline(const QString &cmdline) {
    QRegularExpression urlRe("https?://([a-zA-Z0-9._-]+\\.[a-zA-Z]{2,})");
    auto it = urlRe.globalMatch(cmdline);

    QString domain;
    while (it.hasNext()) {
        QString d = it.next().captured(1).toLower();
        d.remove(QRegularExpression("^www\\."));
        // Skip browser-internal services
        if (d.contains("gstatic") || d.contains("googleapis") || d.contains("googleusercontent")
            || d.contains("chromium.org") || d.contains("safebrowsing") || d.contains("crashpad")
            || d.endsWith("brave.com") || d.endsWith("chrome.google.com"))
            continue;
        domain = d;
        break;
    }
    if (domain.isEmpty()) return "";

    // Match on the registrable domain (exact or as a subdomain), never on a
    // substring: "x.com" must not match netflix.com or dropbox.com.
    auto isDomain = [&](const char *site) {
        QString s = QString::fromLatin1(site);
        return domain == s || domain.endsWith("." + s);
    };
    static const struct { const char *domain; const char *name; } SITES[] = {
        {"youtube.com", "YouTube"}, {"youtu.be", "YouTube"}, {"github.com", "GitHub"},
        {"gitlab.com", "GitLab"}, {"bitbucket.org", "Bitbucket"}, {"facebook.com", "Facebook"},
        {"fb.com", "Facebook"}, {"twitter.com", "X (Twitter)"}, {"x.com", "X (Twitter)"},
        {"reddit.com", "Reddit"}, {"instagram.com", "Instagram"}, {"netflix.com", "Netflix"},
        {"spotify.com", "Spotify Web"}, {"linkedin.com", "LinkedIn"}, {"amazon.com", "Amazon"},
        {"mail.google.com", "Gmail"}, {"docs.google.com", "Google Docs"},
        {"drive.google.com", "Google Drive"}, {"calendar.google.com", "Google Calendar"},
        {"meet.google.com", "Google Meet"}, {"google.com", "Google"}, {"claude.ai", "Claude"},
        {"anthropic.com", "Claude"}, {"chatgpt.com", "ChatGPT"}, {"openai.com", "ChatGPT"},
        {"stackoverflow.com", "Stack Overflow"}, {"discord.com", "Discord Web"},
        {"twitch.tv", "Twitch"}, {"wikipedia.org", "Wikipedia"}, {"brightspace.com", "Brightspace"},
        {"zoom.us", "Zoom"}, {"slack.com", "Slack Web"}, {"notion.so", "Notion"},
        {"figma.com", "Figma"}, {"whatsapp.com", "WhatsApp Web"}, {"outlook.com", "Outlook"},
        {"live.com", "Outlook"}, {"office.com", "Microsoft 365"}, {"missouristate.edu", "Missouri State"},
        {"canva.com", "Canva"}, {"pinterest.com", "Pinterest"}, {"tiktok.com", "TikTok"},
        {"ebay.com", "eBay"}, {"walmart.com", "Walmart"}, {"hulu.com", "Hulu"},
        {"disneyplus.com", "Disney+"}, {"medium.com", "Medium"}, {"quora.com", "Quora"},
        {"w3schools.com", "W3Schools"}, {"developer.mozilla.org", "MDN Web Docs"},
        {"leetcode.com", "LeetCode"}, {"hackerrank.com", "HackerRank"}, {"codepen.io", "CodePen"},
        {"replit.com", "Replit"}, {"vercel.com", "Vercel"}, {"netlify.com", "Netlify"},
        {"heroku.com", "Heroku"}, {"aws.amazon.com", "AWS Console"}, {"azure.com", "Azure"},
        {"atlassian.net", "Jira"}, {"trello.com", "Trello"}, {"asana.com", "Asana"},
        {nullptr, nullptr}};
    for (int i = 0; SITES[i].domain; ++i)
        if (isDomain(SITES[i].domain)) return SITES[i].name;

    // Unknown site: capitalise the second-level label ("example.com" -> "Example")
    QStringList parts = domain.split('.');
    if (parts.size() >= 2) {
        QString name = parts[parts.size() - 2];
        if (!name.isEmpty()) name[0] = name[0].toUpper();
        return name;
    }
    return domain;
}

// ─── Constructor ────────────────────────────────────────────────

ProcessesTab::ProcessesTab(QWidget *parent) : QWidget(parent), myUid(getuid()) {
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(4);
    layout->setContentsMargins(6, 6, 6, 6);

    // Row 1: filter + hide system
    auto *row1 = new QHBoxLayout();
    filterEdit = new QLineEdit();
    filterEdit->setPlaceholderText("🔍 Filter by name, PID, or type...");
    filterEdit->setStyleSheet(
        "padding: 7px 10px; border: 1px solid #455A64; border-radius: 6px;"
        "color: white; background: #263238; font-size: 13px;");
    connect(filterEdit, &QLineEdit::textChanged, this, &ProcessesTab::onFilterChanged);
    row1->addWidget(filterEdit, 1);

    hideSystemCheck = new QCheckBox("My processes only");
    hideSystemCheck->setChecked(true);
    hideSystemCheck->setStyleSheet("color: #B0BEC5; font-size: 12px;");
    connect(hideSystemCheck, &QCheckBox::toggled, this, [this](bool) { rebuild(); });
    row1->addWidget(hideSystemCheck);

    countLabel = new QLabel("0 apps");
    countLabel->setStyleSheet("color: #78909C; font-size: 12px; padding-left: 8px;");
    row1->addWidget(countLabel);
    layout->addLayout(row1);

    // Row 2: actions
    auto *row2 = new QHBoxLayout();
    row2->setSpacing(6);

    auto makeBtn = [&](const QString &text, const QString &bg, bool bold) {
        auto *b = new QPushButton(text);
        b->setStyleSheet(QString("padding: 5px 12px; background: %1; color: white; border: none;"
                                 "border-radius: 4px; font-size: 12px;%2")
                             .arg(bg, bold ? " font-weight: bold;" : ""));
        row2->addWidget(b);
        return b;
    };
    killBtn = makeBtn("⏻ End Task", "#C62828", true);
    connect(killBtn, &QPushButton::clicked, this, &ProcessesTab::onKillProcess);
    killGroupBtn = makeBtn("⏻ End App Group", "#880E4F", true);
    connect(killGroupBtn, &QPushButton::clicked, this, &ProcessesTab::onKillGroup);
    suspendBtn = makeBtn("⏸ Suspend", "#E65100", false);
    connect(suspendBtn, &QPushButton::clicked, this, &ProcessesTab::onSuspendProcess);
    resumeBtn = makeBtn("▶ Resume", "#2E7D32", false);
    connect(resumeBtn, &QPushButton::clicked, this, &ProcessesTab::onResumeProcess);

    row2->addSpacing(12);
    auto *niceLabel = new QLabel("Priority:");
    niceLabel->setStyleSheet("color: #B0BEC5; font-size: 12px;");
    row2->addWidget(niceLabel);
    niceSpin = new QSpinBox();
    niceSpin->setRange(-20, 19);
    niceSpin->setValue(0);
    niceSpin->setStyleSheet(
        "padding: 3px 6px; background: #263238; color: white;"
        "border: 1px solid #455A64; border-radius: 4px; font-size: 12px;");
    row2->addWidget(niceSpin);
    reniceBtn = makeBtn("Set Priority", "#1565C0", false);
    connect(reniceBtn, &QPushButton::clicked, this, &ProcessesTab::onReniceProcess);

    row2->addStretch();
    layout->addLayout(row2);

    // Tree widget
    tree = new QTreeWidget(this);
    tree->setColumnCount(6);
    tree->setHeaderLabels({"Name", "PID", "CPU %", "Memory", "Status", "Type"});
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setSortingEnabled(false);
    tree->setRootIsDecorated(true);
    tree->setAnimated(false);
    tree->setIndentation(24);
    tree->setUniformRowHeights(true);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->setColumnWidth(1, 65);
    tree->setColumnWidth(2, 65);
    tree->setColumnWidth(3, 85);
    tree->setColumnWidth(4, 80);
    tree->setColumnWidth(5, 110);
    tree->setStyleSheet(
        "QTreeWidget { background: #1a1a2e; border: 1px solid #2d2d44; font-size: 12px; }"
        "QTreeWidget::item { padding: 3px 6px; min-height: 24px; border-bottom: 1px solid #2d2d44; }"
        "QTreeWidget::item:selected { background: #1565C0; }"
        "QTreeWidget::item:hover:!selected { background: #252545; }"
        "QTreeWidget::branch:has-children:closed { image: none; }"
        "QTreeWidget::branch:has-children:open { image: none; }"
        "QHeaderView::section { background: #16213e; color: #78909C; padding: 6px 8px; border: none;"
        "  border-bottom: 2px solid #1565C0; font-weight: bold; font-size: 11px; text-transform: uppercase; }");
    layout->addWidget(tree);
}

// ─── Classification ─────────────────────────────────────────────

QString ProcessesTab::statusText(char state) {
    switch (state) {
        case 'R': return "Running";
        case 'S': return "Sleeping";
        case 'D': return "Disk Sleep";
        case 'Z': return "Zombie";
        case 'T': case 't': return "Stopped";
        case 'I': return "Idle";
        default: return QString(QChar(state));
    }
}

QString ProcessesTab::classifyBrowserProcess(const QString &cmdline) {
    QString cmd = cmdline.toLower();
    if (cmd.contains("--type=renderer")) return cmd.contains("--extension-process") ? "Extension" : "Tab";
    if (cmd.contains("--type=gpu")) return "GPU Process";
    if (cmd.contains("--type=utility")) {
        if (cmd.contains("network")) return "Network Service";
        if (cmd.contains("storage")) return "Storage Service";
        if (cmd.contains("audio")) return "Audio Service";
        if (cmd.contains("data_decoder")) return "Data Decoder";
        if (cmd.contains("video")) return "Video Service";
        return "Utility Process";
    }
    if (cmd.contains("--type=broker")) return "Broker";
    if (cmd.contains("--type=zygote")) return "Process Manager";
    if (cmd.contains("crashpad")) return "Crash Reporter";
    if (!cmd.contains("--type=")) return "Main Process";
    return "Helper";
}

QString ProcessesTab::classifyCodeProcess(const QString &cmdline) {
    QString cmd = cmdline.toLower();
    if (cmd.contains("extensionhost")) return "Extension Host";
    if (cmd.contains("watcherservice") || cmd.contains("file-watcher")) return "File Watcher";
    if (cmd.contains("ptyhost")) return "Terminal";
    if (cmd.contains("--type=gpu")) return "GPU Process";
    if (cmd.contains("--type=utility")) return "Utility";
    if (cmd.contains("--type=renderer")) return "Renderer";
    if (!cmd.contains("--type=")) return "Main Process";
    return "Helper";
}

QString ProcessesTab::processType(const ProcInfo &p) {
    const std::string &app = p.app;
    if (AppNames::isBrowser(app)) return classifyBrowserProcess(QString::fromStdString(p.cmdline));
    if (app == "vscode") return classifyCodeProcess(QString::fromStdString(p.cmdline));
    return "";
}

// ─── Refresh ────────────────────────────────────────────────────

void ProcessesTab::refresh(const std::vector<ProcInfo> &procs) {
    lastProcs = procs;
    rebuild();
}

void ProcessesTab::rebuild() {
    saveState();

    std::vector<const ProcInfo *> shown;
    shown.reserve(lastProcs.size());
    bool mineOnly = hideSystemCheck->isChecked();
    for (const auto &p : lastProcs) {
        if (mineOnly && p.uid != myUid) continue;
        if (!currentFilter.isEmpty()) {
            QString name = QString::fromStdString(p.comm);
            QString app = QString::fromStdString(AppNames::display(p.app));
            if (!name.contains(currentFilter, Qt::CaseInsensitive)
                && !QString::number(p.pid).contains(currentFilter)
                && !app.contains(currentFilter, Qt::CaseInsensitive)
                && !processType(p).contains(currentFilter, Qt::CaseInsensitive))
                continue;
        }
        shown.push_back(&p);
    }

    tree->setUpdatesEnabled(false);
    tree->clear();
    populateGrouped(shown);
    restoreState();
    tree->setUpdatesEnabled(true);
}

void ProcessesTab::populateGrouped(const std::vector<const ProcInfo *> &procs) {
    std::map<QString, std::vector<const ProcInfo *>> groups;
    std::map<QString, double> groupCpu, groupMem;

    for (const auto *p : procs) {
        QString appName = QString::fromStdString(AppNames::display(p->app));
        groups[appName].push_back(p);
        groupCpu[appName] += p->cpuPct;
        groupMem[appName] += p->memMB;
    }

    std::vector<std::pair<QString, std::vector<const ProcInfo *>>> sorted(groups.begin(), groups.end());
    std::sort(sorted.begin(), sorted.end(), [&](const auto &a, const auto &b) {
        if (groupCpu[a.first] != groupCpu[b.first]) return groupCpu[a.first] > groupCpu[b.first];
        return groupMem[a.first] > groupMem[b.first];
    });

    auto cpuColor = [](double pct) {
        if (pct > 80.0) return QColor(239, 83, 80);
        if (pct > 40.0) return QColor(255, 167, 38);
        if (pct > 10.0) return QColor(255, 241, 118);
        return QColor(100, 181, 246);
    };
    auto memStr = [](double mb) {
        return mb >= 1024 ? QString("%1 GB").arg(mb / 1024.0, 0, 'f', 1) : QString("%1 MB").arg(mb, 0, 'f', 1);
    };

    int appCount = 0;
    for (auto &[appName, appProcs] : sorted) {
        appCount++;

        if (appProcs.size() == 1) {
            const auto &p = *appProcs[0];
            auto *item = new QTreeWidgetItem();
            item->setText(0, appName);
            item->setData(1, Qt::DisplayRole, p.pid);
            item->setText(2, QString::number(p.cpuPct, 'f', 1));
            item->setText(3, memStr(p.memMB));
            item->setText(4, statusText(p.state));
            item->setText(5, processType(p));
            item->setForeground(0, QColor(220, 220, 220));
            for (int c = 1; c < 6; ++c) item->setForeground(c, QColor(160, 160, 160));
            item->setForeground(2, p.cpuPct > 5.0 ? cpuColor(p.cpuPct) : QColor(160, 160, 160));
            tree->addTopLevelItem(item);
            continue;
        }

        auto *groupItem = new QTreeWidgetItem();
        groupItem->setText(0, QString("%1  (%2)").arg(appName).arg(appProcs.size()));
        groupItem->setText(1, "");
        groupItem->setText(2, QString::number(groupCpu[appName], 'f', 1));
        groupItem->setText(3, memStr(groupMem[appName]));
        QFont bold;
        bold.setBold(true);
        groupItem->setFont(0, bold);
        groupItem->setForeground(0, QColor(255, 255, 255));
        groupItem->setForeground(2, cpuColor(groupCpu[appName]));
        groupItem->setForeground(3, QColor(130, 177, 255));

        std::sort(appProcs.begin(), appProcs.end(), [](const ProcInfo *a, const ProcInfo *b) {
            QString ta = processType(*a), tb = processType(*b);
            if (ta == "Main Process" && tb != "Main Process") return true;
            if (ta != "Main Process" && tb == "Main Process") return false;
            return a->cpuPct > b->cpuPct;
        });

        for (const auto *p : appProcs) {
            QString type = processType(*p);
            QString displayName;
            if (type == "Tab") {
                QString site = extractSiteFromCmdline(QString::fromStdString(p->cmdline));
                displayName = site.isEmpty() ? QString("Tab — %1").arg(memStr(p->memMB))
                                             : QString("Tab: %1").arg(site);
            } else if (!type.isEmpty()) {
                displayName = type;
            } else {
                displayName = QString::fromStdString(p->comm);
            }

            auto *child = new QTreeWidgetItem();
            child->setText(0, displayName);
            child->setData(1, Qt::DisplayRole, p->pid);
            child->setText(2, QString::number(p->cpuPct, 'f', 1));
            child->setText(3, memStr(p->memMB));
            child->setText(4, statusText(p->state));
            child->setText(5, type);

            QColor childText(170, 170, 170);
            if (p->cpuPct > 50.0) childText = QColor(239, 83, 80);
            else if (p->cpuPct > 20.0) childText = QColor(255, 167, 38);
            child->setForeground(0, childText);
            for (int c = 1; c < 6; ++c) child->setForeground(c, QColor(130, 130, 130));
            child->setForeground(2, p->cpuPct > 5.0 ? cpuColor(p->cpuPct) : QColor(130, 130, 130));

            if (type == "Main Process") child->setForeground(5, QColor(100, 181, 246));
            else if (type == "Tab") child->setForeground(5, QColor(129, 199, 132));
            else if (type == "Extension") child->setForeground(5, QColor(206, 147, 216));
            else if (type == "GPU Process") child->setForeground(5, QColor(255, 183, 77));
            else if (type.contains("Service")) child->setForeground(5, QColor(128, 222, 234));

            groupItem->addChild(child);
        }
        tree->addTopLevelItem(groupItem);
    }

    countLabel->setText(QString("%1 apps, %2 processes").arg(appCount).arg(procs.size()));
}

// ─── State management ───────────────────────────────────────────

void ProcessesTab::saveState() {
    auto *item = tree->currentItem();
    if (item) {
        QVariant v = item->data(1, Qt::DisplayRole);
        selectedPid = v.toString().isEmpty() ? -1 : v.toInt();
        selectedText = item->text(0);
    } else {
        selectedPid = -1;
        selectedText.clear();
    }
    scrollPos = tree->verticalScrollBar() ? tree->verticalScrollBar()->value() : 0;

    expandedApps.clear();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *top = tree->topLevelItem(i);
        if (top->isExpanded()) expandedApps.insert(top->text(0));
    }
}

void ProcessesTab::restoreState() {
    tree->blockSignals(true);
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *top = tree->topLevelItem(i);
        if (expandedApps.count(top->text(0))) top->setExpanded(true);
    }

    bool found = false;
    if (selectedPid >= 0) {
        std::function<bool(QTreeWidgetItem *)> findByPid = [&](QTreeWidgetItem *item) -> bool {
            QVariant v = item->data(1, Qt::DisplayRole);
            if (!v.toString().isEmpty() && v.toInt() == selectedPid) {
                tree->setCurrentItem(item);
                return true;
            }
            for (int i = 0; i < item->childCount(); ++i)
                if (findByPid(item->child(i))) return true;
            return false;
        };
        for (int i = 0; i < tree->topLevelItemCount() && !found; ++i)
            found = findByPid(tree->topLevelItem(i));
    }
    if (!found && !selectedText.isEmpty()) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto *top = tree->topLevelItem(i);
            if (top->text(0) == selectedText) {
                tree->setCurrentItem(top);
                found = true;
                break;
            }
        }
    }
    // Keep the user's scroll position; jumping to the selection every 1.5 s
    // makes the list impossible to browse.
    if (tree->verticalScrollBar()) tree->verticalScrollBar()->setValue(scrollPos);
    tree->blockSignals(false);
}

// ─── Actions ────────────────────────────────────────────────────

int ProcessesTab::getSelectedPid() {
    auto *item = tree->currentItem();
    if (!item) return -1;
    QVariant v = item->data(1, Qt::DisplayRole);
    return v.toString().isEmpty() ? -1 : v.toInt();
}

QString ProcessesTab::getSelectedName() {
    auto *item = tree->currentItem();
    return item ? item->text(0) : QString();
}

void ProcessesTab::onKillProcess() {
    int pid = getSelectedPid();
    if (pid <= 0) {
        QMessageBox::warning(this, "No Selection", "Select a specific process, not a group header.");
        return;
    }
    auto reply = QMessageBox::question(this, "End Task",
        QString("End %1 (PID %2)?").arg(getSelectedName()).arg(pid), QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes && kill(pid, SIGTERM) != 0)
        QMessageBox::warning(this, "Error", QString("Failed to end PID %1. Permission denied?").arg(pid));
}

void ProcessesTab::onKillGroup() {
    auto *item = tree->currentItem();
    if (!item) {
        QMessageBox::warning(this, "No Selection", "Select an app or process first.");
        return;
    }
    QTreeWidgetItem *groupItem = item->parent() ? item->parent() : item;

    std::vector<int> pids;
    if (groupItem->childCount() > 0) {
        for (int i = 0; i < groupItem->childCount(); ++i) {
            QVariant v = groupItem->child(i)->data(1, Qt::DisplayRole);
            if (!v.toString().isEmpty()) pids.push_back(v.toInt());
        }
    } else {
        QVariant v = groupItem->data(1, Qt::DisplayRole);
        if (!v.toString().isEmpty()) pids.push_back(v.toInt());
    }
    if (pids.empty()) {
        QMessageBox::warning(this, "Error", "No processes found in this group.");
        return;
    }

    auto reply = QMessageBox::question(this, "End App Group",
        QString("End all %1 processes (%2 total)?\n\nThis will close the entire application.")
            .arg(groupItem->text(0)).arg(pids.size()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    int failed = 0;
    for (int pid : pids)
        if (kill(pid, SIGTERM) != 0) failed++;
    if (failed > 0)
        QMessageBox::warning(this, "Partial Failure",
            QString("%1 of %2 processes could not be terminated. Permission denied?").arg(failed).arg(pids.size()));
}

void ProcessesTab::onSuspendProcess() {
    int pid = getSelectedPid();
    if (pid <= 0) {
        QMessageBox::warning(this, "No Selection", "Select a specific process.");
        return;
    }
    auto reply = QMessageBox::question(this, "Suspend",
        QString("Suspend %1 (PID %2)?").arg(getSelectedName()).arg(pid), QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes && kill(pid, SIGSTOP) != 0)
        QMessageBox::warning(this, "Error", QString("Failed to suspend PID %1.").arg(pid));
}

void ProcessesTab::onResumeProcess() {
    int pid = getSelectedPid();
    if (pid <= 0) {
        QMessageBox::warning(this, "No Selection", "Select a specific process.");
        return;
    }
    if (kill(pid, SIGCONT) != 0)
        QMessageBox::warning(this, "Error", QString("Failed to resume PID %1.").arg(pid));
}

void ProcessesTab::onReniceProcess() {
    int pid = getSelectedPid();
    if (pid <= 0) {
        QMessageBox::warning(this, "No Selection", "Select a specific process.");
        return;
    }
    int niceVal = niceSpin->value();
    auto reply = QMessageBox::question(this, "Change Priority",
        QString("Set priority of %1 (PID %2) to %3?").arg(getSelectedName()).arg(pid).arg(niceVal),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    errno = 0;
    if (setpriority(PRIO_PROCESS, pid, niceVal) == -1 && errno != 0) {
        QMessageBox::warning(this, "Error",
            QString("Failed to change priority for PID %1. %2").arg(pid)
                .arg(errno == EACCES ? "Need root for negative values." : "Failed."));
    }
}

void ProcessesTab::onFilterChanged(const QString &text) {
    currentFilter = text;
    rebuild();
}
