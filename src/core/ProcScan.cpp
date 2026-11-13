#include "ProcScan.h"
#include "AppNames.h"

#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

ProcScanner::ProcScanner() {
    pageSizeKB_ = sysconf(_SC_PAGESIZE) / 1024;
    if (pageSizeKB_ <= 0) pageSizeKB_ = 4;
}

uint64_t ProcScanner::readTotalCpuTicks() {
    std::ifstream f("/proc/stat");
    std::string line;
    if (!std::getline(f, line)) return 0;
    std::istringstream ss(line);
    std::string label;
    ss >> label;
    uint64_t total = 0, v;
    while (ss >> v) total += v;
    return total;
}

ProcScanResult ProcScanner::scan() {
    ProcScanResult result;
    result.numCpus = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    if (result.numCpus < 1) result.numCpus = 1;

    uint64_t totalNow = readTotalCpuTicks();
    result.sysDelta = (prevTotal_ > 0 && totalNow > prevTotal_) ? totalNow - prevTotal_ : 0;
    std::unordered_map<int, Prev> next;

    DIR *dir = opendir("/proc");
    if (!dir) return result;
    struct dirent *ent;
    std::string line;
    while ((ent = readdir(dir))) {
        char *endp = nullptr;
        long pidL = strtol(ent->d_name, &endp, 10);
        if (*endp != '\0' || pidL <= 0) continue;
        int pid = static_cast<int>(pidL);
        std::string base = "/proc/" + std::string(ent->d_name);

        std::ifstream statF(base + "/stat");
        if (!std::getline(statF, line)) continue;
        size_t lp = line.find('('), rp = line.rfind(')');
        if (lp == std::string::npos || rp == std::string::npos || rp < lp) continue;

        ProcInfo p;
        p.pid = pid;
        p.comm = line.substr(lp + 1, rp - lp - 1);
        p.app = AppNames::canonical(p.comm);

        // Fields after the comm: state ppid pgrp session tty tpgid flags minflt
        // cminflt majflt cmajflt utime stime cutime cstime priority nice threads
        // itrealvalue starttime
        std::istringstream rest(line.substr(rp + 2));
        std::string state;
        uint64_t ppid = 0, dummy = 0, utime = 0, stime = 0, cutime = 0, cstime = 0, prio = 0,
                 threads = 0, itreal = 0, startTime = 0;
        long long niceVal = 0;
        rest >> state >> ppid;
        for (int i = 0; i < 9; ++i) rest >> dummy;
        rest >> utime >> stime >> cutime >> cstime >> prio >> niceVal >> threads >> itreal >> startTime;
        p.state = state.empty() ? '?' : state[0];
        p.ppid = static_cast<int>(ppid);
        p.nice = static_cast<int>(niceVal);

        uint64_t ticks = utime + stime;
        auto it = prev_.find(pid);
        if (it != prev_.end() && it->second.startTime == startTime && ticks >= it->second.ticks
            && result.sysDelta > 0) {
            p.cpuDelta = ticks - it->second.ticks;
            p.cpuPct = static_cast<double>(p.cpuDelta) / result.sysDelta * 100.0 * result.numCpus;
            if (p.cpuPct > 100.0 * result.numCpus) p.cpuPct = 100.0 * result.numCpus;
        }
        next[pid] = {ticks, startTime};

        // Memory
        std::ifstream statmF(base + "/statm");
        uint64_t size = 0, rss = 0;
        if (statmF >> size >> rss) p.memMB = static_cast<double>(rss) * pageSizeKB_ / 1024.0;

        // Owner: /proc/[pid] is owned by the process's effective uid.
        struct stat st;
        if (stat(base.c_str(), &st) == 0) {
            p.uid = st.st_uid;
            auto uit = userCache_.find(p.uid);
            if (uit == userCache_.end()) {
                struct passwd *pw = getpwuid(p.uid);
                uit = userCache_.emplace(p.uid, pw ? pw->pw_name : std::to_string(p.uid)).first;
            }
            p.user = uit->second;
        }

        // Command line (NUL separated)
        std::ifstream cmdF(base + "/cmdline", std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(cmdF)), std::istreambuf_iterator<char>());
        for (char &c : raw) if (c == '\0') c = ' ';
        while (!raw.empty() && raw.back() == ' ') raw.pop_back();
        p.cmdline = std::move(raw);

        if (p.cpuDelta > 0 && result.sysDelta > 0)
            result.appCpuShare[p.app] += static_cast<double>(p.cpuDelta) / result.sysDelta;

        result.procs.push_back(std::move(p));
    }
    closedir(dir);

    prev_ = std::move(next);
    prevTotal_ = totalNow;
    return result;
}
