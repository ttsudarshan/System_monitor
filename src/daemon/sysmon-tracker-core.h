#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>
#include <X11/Xlib.h>

std::string normalize(const std::string &name);
std::string executeCommand(const std::string &cmd);
std::string getWaylandFocusedApp();
bool isScreenBlanked(Display *dpy);
std::string getFocusedApp(Display *dpy);
std::string getWindowTitle(Display *dpy);
std::string extractTabName(const std::string &app, const std::string &windowTitle);
std::string getDbPath();
sqlite3* openDb();
void recordTime(sqlite3 *db, const std::string &app, const std::string &tab,
                const std::string &date, int seconds);
std::string getCurrentDate();
