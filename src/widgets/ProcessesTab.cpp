#include "ProcessesTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QScrollBar>
#include <QFont>
#include <QDir>
#include <QRegularExpression>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <signal.h>
#include <pwd.h>
#include <sys/sysinfo.h>
#include <sys/resource.h>
#include <algorithm>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
// X11 defines True/False/Bool macros that clash with Qt - undef them
#undef Bool
#undef True
#undef False
#undef None

// ─── X11 Window Title Lookup ────────────────────────────────────

std::map<int, QString> ProcessesTab::getWindowTitles() {
    std::map<int, QString> result;

    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return result;

    Atom netClientList = XInternAtom(dpy, "_NET_CLIENT_LIST", 1);
    Atom netWmName = XInternAtom(dpy, "_NET_WM_NAME", 1);
    Atom netWmPid = XInternAtom(dpy, "_NET_WM_PID", 1);
    Atom utf8String = XInternAtom(dpy, "UTF8_STRING", 1);

    if (netClientList == 0L) { XCloseDisplay(dpy); return result; }

    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    unsigned char *data = nullptr;

    Window root = DefaultRootWindow(dpy);
    if (XGetWindowProperty(dpy, root, netClientList, 0, 65536, 0,
                           XA_WINDOW, &actualType, &actualFormat,
                           &nItems, &bytesAfter, &data) != Success || !data) {
        XCloseDisplay(dpy);
        return result;
    }

    Window *windows = reinterpret_cast<Window*>(data);

    for (unsigned long i = 0; i < nItems; ++i) {
        Window w = windows[i];

        // Get PID
        unsigned char *pidData = nullptr;
        unsigned long pidItems;
        if (XGetWindowProperty(dpy, w, netWmPid, 0, 1, 0,
                               XA_CARDINAL, &actualType, &actualFormat,
                               &pidItems, &bytesAfter, &pidData) == Success && pidData) {
            int pid = static_cast<int>(*reinterpret_cast<unsigned long*>(pidData));
            XFree(pidData);

            // Get window title
            unsigned char *titleData = nullptr;
            unsigned long titleItems;
            if (XGetWindowProperty(dpy, w, netWmName, 0, 1024, 0,
                                   utf8String, &actualType, &actualFormat,
                                   &titleItems, &bytesAfter, &titleData) == Success && titleData) {
                QString title = QString::fromUtf8(reinterpret_cast<char*>(titleData));
                XFree(titleData);

                // Store — for browsers, main process PID owns the windows
                if (!title.isEmpty()) {
                    result[pid] = title;
                }
            }
        }
    }

    XFree(data);
    XCloseDisplay(dpy);
    return result;
}

// ─── Site Extraction from Cmdline ────────────────────────────────

QString ProcessesTab::extractSiteFromCmdline(const QString &cmdline) {
    // Chromium renderers have various flags that contain URLs/origins
    // Try multiple patterns

    // Pattern 1: --site-per-process with origin
    // Pattern 2: Direct URLs in args
    // Pattern 3: --renderer-client-id can be used with DevTools

    QRegularExpression urlRe("https?://([a-zA-Z0-9._-]+\\.[a-zA-Z]{2,})");
    auto it = urlRe.globalMatch(cmdline);

    // Collect all domains, pick the most meaningful one
    QString bestDomain;
    while (it.hasNext()) {
        auto match = it.next();
        QString domain = match.captured(1);
        domain.remove(QRegularExpression("^www\\."));

        // Skip browser-internal domains
        if (domain.contains("gstatic") || domain.contains("googleapis") ||
            domain.contains("googleusercontent") || domain.contains("chromium.org") ||
            domain.contains("brave.com/update") || domain.contains("chrome.google.com") ||
            domain.contains("safebrowsing") || domain.contains("crashpad"))
            continue;

        bestDomain = domain;
        break; // Use the first non-internal domain
    }

    if (bestDomain.isEmpty()) return "";

    // Map domains to friendly names
    QString d = bestDomain.toLower();
    if (d.contains("youtube")) return "YouTube";
    if (d.contains("github")) return "GitHub";
    if (d.contains("facebook") || d.contains("fb.com")) return "Facebook";
    if (d.contains("twitter") || d.contains("x.com")) return "X (Twitter)";
    if (d.contains("reddit")) return "Reddit";
    if (d.contains("instagram")) return "Instagram";
    if (d.contains("netflix")) return "Netflix";
    if (d.contains("spotify")) return "Spotify Web";
    if (d.contains("linkedin")) return "LinkedIn";
    if (d.contains("amazon")) return "Amazon";
    if (d.contains("mail.google")) return "Gmail";
    if (d.contains("docs.google")) return "Google Docs";
    if (d.contains("drive.google")) return "Google Drive";
    if (d.contains("calendar.google")) return "Google Calendar";
    if (d.contains("meet.google")) return "Google Meet";
    if (d.contains("google.com")) return "Google";
    if (d.contains("claude.ai") || d.contains("anthropic")) return "Claude";
    if (d.contains("chatgpt") || d.contains("openai")) return "ChatGPT";
    if (d.contains("stackoverflow")) return "Stack Overflow";
    if (d.contains("discord")) return "Discord Web";
    if (d.contains("twitch")) return "Twitch";
    if (d.contains("wikipedia")) return "Wikipedia";
    if (d.contains("brightspace") || d.contains("d2l")) return "Brightspace";
    if (d.contains("zoom")) return "Zoom";
    if (d.contains("slack")) return "Slack Web";
    if (d.contains("notion")) return "Notion";
    if (d.contains("figma")) return "Figma";
    if (d.contains("whatsapp")) return "WhatsApp Web";
    if (d.contains("outlook") || d.contains("live.com")) return "Outlook";
    if (d.contains("missouristate") || d.contains("msu")) return "Missouri State";
    if (d.contains("canva")) return "Canva";
    if (d.contains("pinterest")) return "Pinterest";
    if (d.contains("tiktok")) return "TikTok";
    if (d.contains("ebay")) return "eBay";
    if (d.contains("walmart")) return "Walmart";
    if (d.contains("hulu")) return "Hulu";
    if (d.contains("disneyplus") || d.contains("disney")) return "Disney+";
    if (d.contains("medium")) return "Medium";
    if (d.contains("quora")) return "Quora";
    if (d.contains("w3schools")) return "W3Schools";
    if (d.contains("mdn") || d.contains("developer.mozilla")) return "MDN Web Docs";
    if (d.contains("leetcode")) return "LeetCode";
    if (d.contains("hackerrank")) return "HackerRank";
    if (d.contains("codepen")) return "CodePen";
    if (d.contains("replit")) return "Replit";
    if (d.contains("vercel")) return "Vercel";
    if (d.contains("netlify")) return "Netlify";
    if (d.contains("heroku")) return "Heroku";
    if (d.contains("aws.amazon")) return "AWS Console";
    if (d.contains("azure")) return "Azure";
    if (d.contains("gitlab")) return "GitLab";
    if (d.contains("bitbucket")) return "Bitbucket";
    if (d.contains("jira")) return "Jira";
    if (d.contains("trello")) return "Trello";
    if (d.contains("asana")) return "Asana";

    // Return the cleaned domain for unknown sites
    // Capitalize first letter of each segment
    QStringList parts = bestDomain.split('.');
    if (parts.size() >= 2) {
        QString name = parts[parts.size() - 2]; // e.g. "example" from "example.com"
        name[0] = name[0].toUpper();
        return name;
    }
    return bestDomain;
}

// ─── Chrome DevTools Protocol ───────────────────────────────────
// Removed — using cmdline URL extraction + X11 window titles instead

std::map<int, QString> ProcessesTab::buildRendererTitleMap(
    const std::vector<ProcInfo> &procs,
    const std::map<int, QString> &winTitles) {

    std::map<int, QString> result;

    // Step 1: For each renderer, extract site from cmdline
    for (const auto &p : procs) {
        if (p.processType != "Tab") continue;
        QString site = extractSiteFromCmdline(p.cmdline);
        if (!site.isEmpty()) {
            result[p.pid] = site;
        }
    }

    // Step 2: Collect X11 window titles
    std::vector<QString> allTitles;
    for (auto &[pid, title] : winTitles) {
        QString clean = title;
        clean.remove(QRegularExpression(
            "\\s*[-–—]\\s*(Brave|Brave Browser|Google Chrome|Chromium|Firefox|Mozilla Firefox)\\s*$",
            QRegularExpression::CaseInsensitiveOption));
        clean = clean.trimmed();
        if (!clean.isEmpty() && clean.toLower() != "new tab" && clean.toLower() != "newtab")
            allTitles.push_back(clean);
    }

    // Step 3: Assign remaining titles to unmatched renderers by memory size
    std::vector<const ProcInfo*> unmatched;
    for (const auto &p : procs) {
        if (p.processType == "Tab" && result.find(p.pid) == result.end())
            unmatched.push_back(&p);
    }
    std::sort(unmatched.begin(), unmatched.end(),
        [](const ProcInfo *a, const ProcInfo *b) { return a->memMB > b->memMB; });

    // Remove already-used titles
    std::set<QString> usedTitles;
    for (auto &[pid, title] : result)
        usedTitles.insert(title);

    int titleIdx = 0;
    for (auto *p : unmatched) {
        while (titleIdx < static_cast<int>(allTitles.size()) &&
               usedTitles.count(allTitles[titleIdx]))
            titleIdx++;
        if (titleIdx < static_cast<int>(allTitles.size())) {
            result[p->pid] = allTitles[titleIdx];
            usedTitles.insert(allTitles[titleIdx]);
            titleIdx++;
        }
    }

    return result;
}

// ─── Constructor ────────────────────────────────────────────────

ProcessesTab::ProcessesTab(QWidget *parent) : QWidget(parent) {
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
    connect(hideSystemCheck, &QCheckBox::toggled, this, [this](bool) { refresh(); });
    row1->addWidget(hideSystemCheck);

    countLabel = new QLabel("0 apps");
    countLabel->setStyleSheet("color: #78909C; font-size: 12px; padding-left: 8px;");
    row1->addWidget(countLabel);

    layout->addLayout(row1);

    // Row 2: actions
    auto *row2 = new QHBoxLayout();
    row2->setSpacing(6);

    killBtn = new QPushButton("⏻ End Task");
    killBtn->setStyleSheet(
        "padding: 5px 12px; background: #C62828; color: white; border: none;"
        "border-radius: 4px; font-size: 12px; font-weight: bold;");
    connect(killBtn, &QPushButton::clicked, this, &ProcessesTab::onKillProcess);
    row2->addWidget(killBtn);

    killGroupBtn = new QPushButton("⏻ End App Group");
    killGroupBtn->setStyleSheet(
        "padding: 5px 12px; background: #880E4F; color: white; border: none;"
        "border-radius: 4px; font-size: 12px; font-weight: bold;");
    connect(killGroupBtn, &QPushButton::clicked, this, &ProcessesTab::onKillGroup);
    row2->addWidget(killGroupBtn);

    suspendBtn = new QPushButton("⏸ Suspend");
    suspendBtn->setStyleSheet(
        "padding: 5px 12px; background: #E65100; color: white; border: none;"
        "border-radius: 4px; font-size: 12px;");
    connect(suspendBtn, &QPushButton::clicked, this, &ProcessesTab::onSuspendProcess);
    row2->addWidget(suspendBtn);

    resumeBtn = new QPushButton("▶ Resume");
    resumeBtn->setStyleSheet(
        "padding: 5px 12px; background: #2E7D32; color: white; border: none;"
        "border-radius: 4px; font-size: 12px;");
    connect(resumeBtn, &QPushButton::clicked, this, &ProcessesTab::onResumeProcess);
    row2->addWidget(resumeBtn);

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

    reniceBtn = new QPushButton("Set Priority");
    reniceBtn->setStyleSheet(
        "padding: 5px 12px; background: #1565C0; color: white; border: none;"
        "border-radius: 4px; font-size: 12px;");
    connect(reniceBtn, &QPushButton::clicked, this, &ProcessesTab::onReniceProcess);
    row2->addWidget(reniceBtn);

    row2->addStretch();
    layout->addLayout(row2);

    // Tree widget
    tree = new QTreeWidget(this);
    tree->setColumnCount(6);
    tree->setHeaderLabels({"Name", "PID", "CPU %", "Memory", "Status", "Type"});
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setSortingEnabled(false);
    tree->setAlternatingRowColors(false);
    tree->setRootIsDecorated(true);
    tree->setAnimated(true);
    tree->setIndentation(24);
    tree->setUniformRowHeights(true);

    // Column widths: Name gets stretch, rest are fixed
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree->setColumnWidth(1, 65);
    tree->setColumnWidth(2, 65);
    tree->setColumnWidth(3, 85);
    tree->setColumnWidth(4, 80);
    tree->setColumnWidth(5, 110);

    tree->setStyleSheet(
        "QTreeWidget {"
        "  background: #1a1a2e;"
        "  border: 1px solid #2d2d44;"
        "  font-size: 12px;"
        "}"
        "QTreeWidget::item {"
        "  padding: 3px 6px;"
        "  min-height: 24px;"
        "  border-bottom: 1px solid #2d2d44;"
        "}"
        "QTreeWidget::item:selected {"
        "  background: #1565C0;"
        "}"
        "QTreeWidget::item:hover:!selected {"
        "  background: #252545;"
        "}"
        "QTreeWidget::branch:has-children:closed {"
        "  image: none;"
        "}"
        "QTreeWidget::branch:has-children:open {"
        "  image: none;"
        "}"
        "QHeaderView::section {"
        "  background: #16213e;"
        "  color: #78909C;"
        "  padding: 6px 8px;"
        "  border: none;"
        "  border-bottom: 2px solid #1565C0;"
        "  font-weight: bold;"
        "  font-size: 11px;"
        "  text-transform: uppercase;"
        "}");

    layout->addWidget(tree);
}

// ─── Cmdline Parsing ────────────────────────────────────────────

QString ProcessesTab::parseCmdline(int pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/cmdline";
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    std::string full;
    char c;
    while (f.get(c))
        full += (c == '\0') ? ' ' : c;
    return QString::fromStdString(full).trimmed();
}

QString ProcessesTab::normalizeAppName(const QString &name) {
    QString n = name.toLower();
    if (n.contains("brave")) return "Brave Browser";
    if (n.contains("firefox")) return "Firefox";
    if (n.contains("chrome") && !n.contains("chromium")) return "Google Chrome";
    if (n.contains("chromium")) return "Chromium";
    if (n == "code" || n.startsWith("code")) return "VS Code";
    if (n.contains("slack")) return "Slack";
    if (n.contains("discord")) return "Discord";
    if (n.contains("spotify")) return "Spotify";
    if (n.contains("telegram")) return "Telegram";
    if (n.contains("thunderbird")) return "Thunderbird";
    if (n.contains("libreoffice")) return "LibreOffice";
    if (n.contains("gimp")) return "GIMP";
    if (n.contains("vlc")) return "VLC";
    if (n.contains("steam")) return "Steam";
    if (n.contains("nemo")) return "Files (Nemo)";
    if (n.contains("nautilus")) return "Files (Nautilus)";
    if (n.contains("cinnamon") && !n.contains("setting")) return "Cinnamon Desktop";
    if (n.contains("gnome-shell")) return "GNOME Shell";
    if (n.contains("systemmonitor") || n == "systemmonitor") return "System Monitor";
    return name;
}

QString ProcessesTab::classifyBrowserProcess(const QString &cmdline) {
    QString cmd = cmdline.toLower();
    if (cmd.contains("--type=renderer")) {
        if (cmd.contains("--extension-process")) return "Extension";
        return "Tab";
    }
    if (cmd.contains("--type=gpu")) return "GPU Process";
    if (cmd.contains("--type=utility") && cmd.contains("network")) return "Network Service";
    if (cmd.contains("--type=utility") && cmd.contains("storage")) return "Storage Service";
    if (cmd.contains("--type=utility") && cmd.contains("audio")) return "Audio Service";
    if (cmd.contains("--type=utility") && cmd.contains("data_decoder")) return "Data Decoder";
    if (cmd.contains("--type=utility") && cmd.contains("video")) return "Video Service";
    if (cmd.contains("--type=utility")) return "Utility Process";
    if (cmd.contains("--type=broker")) return "Broker";
    if (cmd.contains("--type=zygote")) return "Process Manager";
    if (cmd.contains("crashpad") || cmd.contains("--type=crashpad")) return "Crash Reporter";
    if (!cmd.contains("--type=")) return "Main Process";
    return "Helper";
}

QString ProcessesTab::classifyCodeProcess(const QString &cmdline) {
    QString cmd = cmdline.toLower();
    if (cmd.contains("extensionhost")) return "Extension Host";
    if (cmd.contains("watcherservice") || cmd.contains("file-watcher")) return "File Watcher";
    if (cmd.contains("ptyhost")) return "Terminal";
    if (cmd.contains("gpu-process") || cmd.contains("--type=gpu")) return "GPU Process";
    if (cmd.contains("--type=utility")) return "Utility";
    if (cmd.contains("--type=renderer")) return "Renderer";
    if (!cmd.contains("--type=")) return "Main Process";
    return "Helper";
}

// ─── Read Processes ─────────────────────────────────────────────

uint64_t ProcessesTab::readTotalCpuTime() {
    std::ifstream f("/proc/stat");
    if (!f.is_open()) return 0;
    std::string line;
    std::getline(f, line);
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

    uint64_t totalCpuNow = readTotalCpuTime();
    std::unordered_map<int, PrevCpuData> newCpuMap;

    QDir procDir("/proc");
    for (const QString &entry : procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok;
        int pid = entry.toInt(&ok);
        if (!ok) continue;

        ProcInfo p;
        p.pid = pid;

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

        uint64_t ppid;
        rest >> ppid;
        p.ppid = static_cast<int>(ppid);

        uint64_t dummy;
        for (int i = 0; i < 9; ++i) rest >> dummy;

        uint64_t utime, stime;
        rest >> utime >> stime;

        uint64_t cutime, cstime, priority;
        int64_t niceVal;
        rest >> cutime >> cstime >> priority >> niceVal;
        p.nice = static_cast<int>(niceVal);

        PrevCpuData cur;
        cur.utime = utime;
        cur.stime = stime;
        cur.totalSys = totalCpuNow;

        auto it = prevCpuMap.find(pid);
        if (it != prevCpuMap.end()) {
            uint64_t procDelta = (utime + stime) - (it->second.utime + it->second.stime);
            uint64_t sysDelta = totalCpuNow - it->second.totalSys;
            if (sysDelta > 0) {
                p.cpuPct = (static_cast<double>(procDelta) / sysDelta) * 100.0 * numCpus;
                if (p.cpuPct > 100.0 * numCpus) p.cpuPct = 100.0 * numCpus;
                if (p.cpuPct < 0.0) p.cpuPct = 0.0;
            } else {
                p.cpuPct = 0.0;
            }
        } else {
            p.cpuPct = 0.0;
        }
        newCpuMap[pid] = cur;

        // Memory
        std::string statmPath = "/proc/" + std::to_string(pid) + "/statm";
        std::ifstream statmF(statmPath);
        p.memMB = 0;
        if (statmF.is_open()) {
            uint64_t size, rss;
            statmF >> size >> rss;
            double rssKB = rss * pageSize / 1024.0;
            p.memPct = (totalMemKB > 0) ? (rssKB / totalMemKB) * 100.0 : 0.0;
            p.memMB = rssKB / 1024.0;
        } else {
            p.memPct = 0.0;
        }

        // User
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

        // Cmdline & classify
        p.cmdline = parseCmdline(pid);
        QString nl = p.name.toLower();
        if (nl.contains("brave") || nl.contains("chrome") || nl.contains("chromium") || nl.contains("firefox"))
            p.processType = classifyBrowserProcess(p.cmdline);
        else if (nl == "code" || nl.startsWith("code"))
            p.processType = classifyCodeProcess(p.cmdline);
        else
            p.processType = "";

        procs.push_back(p);
    }

    prevCpuMap = std::move(newCpuMap);
    return procs;
}

// ─── Grouped View ───────────────────────────────────────────────

void ProcessesTab::populateGrouped(const std::vector<ProcInfo> &procs) {
    // Get X11 window titles and build renderer->title map
    auto winTitles = getWindowTitles();
    auto rendererTitles = buildRendererTitleMap(procs, winTitles);

    // Group by app name
    std::map<QString, std::vector<ProcInfo>> groups;
    std::map<QString, double> groupCpu;
    std::map<QString, double> groupMem;

    for (const auto &p : procs) {
        QString appName = normalizeAppName(p.name);
        groups[appName].push_back(p);
        groupCpu[appName] += p.cpuPct;
        groupMem[appName] += p.memMB;
    }

    // Sort by CPU desc
    std::vector<std::pair<QString, std::vector<ProcInfo>>> sorted(groups.begin(), groups.end());
    std::sort(sorted.begin(), sorted.end(),
        [&](const auto &a, const auto &b) { return groupCpu[a.first] > groupCpu[b.first]; });

    int appCount = 0;

    for (auto &[appName, appProcs] : sorted) {
        appCount++;

        // Format memory string
        QString memStr;
        if (groupMem[appName] >= 1024)
            memStr = QString("%1 GB").arg(groupMem[appName] / 1024.0, 0, 'f', 1);
        else
            memStr = QString("%1 MB").arg(groupMem[appName], 0, 'f', 1);

        if (appProcs.size() == 1) {
            // Single process app — no expand needed
            auto *item = new QTreeWidgetItem();
            auto &p = appProcs[0];
            item->setText(0, appName);
            item->setData(1, Qt::DisplayRole, p.pid);
            item->setData(2, Qt::DisplayRole, QString::number(p.cpuPct, 'f', 1));
            item->setText(3, QString("%1 MB").arg(p.memMB, 0, 'f', 1));
            item->setText(4, p.status);
            item->setText(5, p.processType);

            item->setForeground(0, QColor(220, 220, 220));
            for (int c = 1; c < 6; ++c) item->setForeground(c, QColor(160, 160, 160));

            tree->addTopLevelItem(item);
            continue;
        }

        // Multi-process app — create collapsible group
        auto *groupItem = new QTreeWidgetItem();
        groupItem->setText(0, QString("%1  (%2)").arg(appName).arg(appProcs.size()));
        groupItem->setData(1, Qt::DisplayRole, "");
        groupItem->setData(2, Qt::DisplayRole, QString::number(groupCpu[appName], 'f', 1));
        groupItem->setText(3, memStr);
        groupItem->setText(4, "");
        groupItem->setText(5, "");

        QFont bold;
        bold.setBold(true);
        bold.setPointSize(bold.pointSize());
        groupItem->setFont(0, bold);
        groupItem->setForeground(0, QColor(255, 255, 255));

        // Color by CPU
        QColor cpuColor(100, 181, 246); // Default blue
        if (groupCpu[appName] > 80.0) cpuColor = QColor(239, 83, 80);
        else if (groupCpu[appName] > 40.0) cpuColor = QColor(255, 167, 38);
        else if (groupCpu[appName] > 10.0) cpuColor = QColor(255, 241, 118);

        groupItem->setForeground(2, cpuColor);
        groupItem->setForeground(3, QColor(130, 177, 255));

        // Sort children
        std::sort(appProcs.begin(), appProcs.end(), [](const ProcInfo &a, const ProcInfo &b) {
            // Main first, then by CPU
            if (a.processType == "Main Process" && b.processType != "Main Process") return true;
            if (a.processType != "Main Process" && b.processType == "Main Process") return false;
            return a.cpuPct > b.cpuPct;
        });

        for (const auto &p : appProcs) {
            auto *child = new QTreeWidgetItem();

            // Determine display name
            QString displayName;
            if (p.processType == "Main Process") {
                displayName = "Main Process";
            } else if (p.processType == "Tab") {
                // Try renderer title map first
                auto titleIt = rendererTitles.find(p.pid);
                if (titleIt != rendererTitles.end() && !titleIt->second.isEmpty()) {
                    displayName = QString("Tab: %1").arg(titleIt->second);
                } else {
                    // Show memory as hint for unidentified tabs
                    displayName = QString("Tab (Web Page) — %1 MB").arg(p.memMB, 0, 'f', 0);
                }
            } else if (p.processType == "Extension") {
                displayName = "Extension";
            } else {
                displayName = p.processType.isEmpty() ? p.name : p.processType;
            }

            child->setText(0, displayName);
            child->setData(1, Qt::DisplayRole, p.pid);
            child->setData(2, Qt::DisplayRole, QString::number(p.cpuPct, 'f', 1));
            child->setText(3, QString("%1 MB").arg(p.memMB, 0, 'f', 1));
            child->setText(4, p.status);
            child->setText(5, p.processType);

            // Style child rows
            QColor childText(170, 170, 170);
            if (p.cpuPct > 50.0) childText = QColor(239, 83, 80);
            else if (p.cpuPct > 20.0) childText = QColor(255, 167, 38);

            child->setForeground(0, childText);
            for (int c = 1; c < 6; ++c) child->setForeground(c, QColor(130, 130, 130));
            child->setForeground(2, p.cpuPct > 5.0 ? cpuColor : QColor(130, 130, 130));

            // Color-code process type
            if (p.processType == "Main Process")
                child->setForeground(5, QColor(100, 181, 246));
            else if (p.processType == "Tab")
                child->setForeground(5, QColor(129, 199, 132));
            else if (p.processType == "Extension")
                child->setForeground(5, QColor(206, 147, 216));
            else if (p.processType == "GPU Process")
                child->setForeground(5, QColor(255, 183, 77));
            else if (p.processType.contains("Service"))
                child->setForeground(5, QColor(128, 222, 234));

            groupItem->addChild(child);
        }

        tree->addTopLevelItem(groupItem);
    }

    countLabel->setText(QString("%1 apps, %2 processes")
        .arg(appCount).arg(procs.size()));
}

// ─── State Management ───────────────────────────────────────────

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
        if (top->isExpanded())
            expandedApps.insert(top->text(0));
    }
}

void ProcessesTab::restoreState() {
    tree->blockSignals(true);

    // Restore expanded groups
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto *top = tree->topLevelItem(i);
        if (expandedApps.count(top->text(0)))
            top->setExpanded(true);
    }

    // Restore selection — try PID first, then text match
    bool found = false;

    if (selectedPid >= 0) {
        std::function<bool(QTreeWidgetItem*)> findByPid;
        findByPid = [&](QTreeWidgetItem *item) -> bool {
            QVariant v = item->data(1, Qt::DisplayRole);
            if (!v.toString().isEmpty() && v.toInt() == selectedPid) {
                tree->setCurrentItem(item);
                tree->scrollToItem(item);
                return true;
            }
            for (int i = 0; i < item->childCount(); ++i) {
                if (findByPid(item->child(i))) return true;
            }
            return false;
        };
        for (int i = 0; i < tree->topLevelItemCount() && !found; ++i)
            found = findByPid(tree->topLevelItem(i));
    }

    // Fallback: match by row text (for group headers that have no PID)
    if (!found && !selectedText.isEmpty()) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto *top = tree->topLevelItem(i);
            if (top->text(0) == selectedText) {
                tree->setCurrentItem(top);
                tree->scrollToItem(top);
                found = true;
                break;
            }
        }
    }

    // Restore scroll only if we didn't scroll to an item
    if (!found && tree->verticalScrollBar())
        tree->verticalScrollBar()->setValue(scrollPos);

    tree->blockSignals(false);
}

// ─── Refresh ────────────────────────────────────────────────────

void ProcessesTab::refresh() {
    saveState();

    auto procs = readProcesses();

    // Filter
    if (!currentFilter.isEmpty()) {
        std::vector<ProcInfo> filtered;
        for (const auto &p : procs) {
            if (p.name.contains(currentFilter, Qt::CaseInsensitive) ||
                QString::number(p.pid).contains(currentFilter) ||
                p.processType.contains(currentFilter, Qt::CaseInsensitive) ||
                normalizeAppName(p.name).contains(currentFilter, Qt::CaseInsensitive))
                filtered.push_back(p);
        }
        procs = filtered;
    }

    // Hide system processes
    if (hideSystemCheck->isChecked()) {
        QString currentUser = QString::fromUtf8(getenv("USER"));
        std::vector<ProcInfo> userProcs;
        for (const auto &p : procs) {
            if (p.user == currentUser)
                userProcs.push_back(p);
        }
        procs = userProcs;
    }

    tree->clear();
    populateGrouped(procs);
    restoreState();
}

// ─── Actions ────────────────────────────────────────────────────

int ProcessesTab::getSelectedPid() {
    auto *item = tree->currentItem();
    if (!item) return -1;
    QVariant v = item->data(1, Qt::DisplayRole);
    if (v.toString().isEmpty()) return -1;
    return v.toInt();
}

QString ProcessesTab::getSelectedName() {
    auto *item = tree->currentItem();
    if (!item) return "";
    return item->text(0);
}

void ProcessesTab::onKillProcess() {
    int pid = getSelectedPid();
    if (pid <= 0) {
        QMessageBox::warning(this, "No Selection", "Select a specific process, not a group header.");
        return;
    }
    auto reply = QMessageBox::question(this, "End Task",
        QString("End %1 (PID %2)?").arg(getSelectedName()).arg(pid),
        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        if (kill(pid, SIGTERM) != 0)
            QMessageBox::warning(this, "Error",
                QString("Failed to end PID %1. Permission denied?").arg(pid));
    }
}

void ProcessesTab::onKillGroup() {
    auto *item = tree->currentItem();
    if (!item) {
        QMessageBox::warning(this, "No Selection", "Select an app or process first.");
        return;
    }

    // If a child is selected, go to parent group
    QTreeWidgetItem *groupItem = item;
    if (item->parent())
        groupItem = item->parent();

    // Collect all PIDs in this group
    std::vector<int> pids;
    if (groupItem->childCount() > 0) {
        for (int i = 0; i < groupItem->childCount(); ++i) {
            QVariant v = groupItem->child(i)->data(1, Qt::DisplayRole);
            if (!v.toString().isEmpty()) pids.push_back(v.toInt());
        }
    } else {
        // Single-process app
        QVariant v = groupItem->data(1, Qt::DisplayRole);
        if (!v.toString().isEmpty()) pids.push_back(v.toInt());
    }

    if (pids.empty()) {
        QMessageBox::warning(this, "Error", "No processes found in this group.");
        return;
    }

    QString appName = groupItem->text(0);
    auto reply = QMessageBox::question(this, "End App Group",
        QString("End all %1 processes (%2 total)?\n\nThis will close the entire application.")
            .arg(appName).arg(pids.size()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        int failed = 0;
        for (int pid : pids) {
            if (kill(pid, SIGTERM) != 0) failed++;
        }
        if (failed > 0) {
            QMessageBox::warning(this, "Partial Failure",
                QString("%1 of %2 processes could not be terminated. Permission denied?")
                    .arg(failed).arg(pids.size()));
        }
    }
}

void ProcessesTab::onSuspendProcess() {
    int pid = getSelectedPid();
    if (pid <= 0) {
        QMessageBox::warning(this, "No Selection", "Select a specific process.");
        return;
    }
    auto reply = QMessageBox::question(this, "Suspend",
        QString("Suspend %1 (PID %2)?").arg(getSelectedName()).arg(pid),
        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        if (kill(pid, SIGSTOP) != 0)
            QMessageBox::warning(this, "Error",
                QString("Failed to suspend PID %1.").arg(pid));
    }
}

void ProcessesTab::onResumeProcess() {
    int pid = getSelectedPid();
    if (pid <= 0) {
        QMessageBox::warning(this, "No Selection", "Select a specific process.");
        return;
    }
    if (kill(pid, SIGCONT) != 0)
        QMessageBox::warning(this, "Error",
            QString("Failed to resume PID %1.").arg(pid));
}

void ProcessesTab::onReniceProcess() {
    int pid = getSelectedPid();
    if (pid <= 0) {
        QMessageBox::warning(this, "No Selection", "Select a specific process.");
        return;
    }
    int niceVal = niceSpin->value();
    auto reply = QMessageBox::question(this, "Change Priority",
        QString("Set priority of %1 (PID %2) to %3?")
            .arg(getSelectedName()).arg(pid).arg(niceVal),
        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        errno = 0;
        if (setpriority(PRIO_PROCESS, pid, niceVal) == -1 && errno != 0) {
            QMessageBox::warning(this, "Error",
                QString("Failed to change priority for PID %1. %2").arg(pid)
                    .arg(errno == EACCES ? "Need root for negative values." : "Failed."));
        }
    }
}

void ProcessesTab::onFilterChanged(const QString &text) {
    currentFilter = text;
    refresh();
}