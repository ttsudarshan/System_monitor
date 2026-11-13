#pragma once

#include <string>

// One place that decides how a raw process name / WM_CLASS maps to an app.
// Every component (daemon, GUI tabs, energy attribution) must go through
// this so that database rows and on-screen names agree.
namespace AppNames {

// Lower-case canonical key used in the database, e.g. "brave", "vscode", "terminal".
std::string canonical(const std::string &raw);

// Human-readable name for a canonical key, e.g. "brave" -> "Brave Browser".
std::string display(const std::string &canonical);

bool isBrowser(const std::string &canonical);

// Desktop shells, tray applets, session daemons... Things that are never the
// app the user is "using", used to filter the Wayland CPU heuristic.
bool isDesktopNoise(const std::string &comm);

} // namespace AppNames
