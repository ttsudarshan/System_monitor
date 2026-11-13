cmake_minimum_required(VERSION 3.16)
project(SystemMonitor VERSION 1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)
find_package(Threads REQUIRED)

# ── sysmon-core: shared by the GUI and the daemon, no Qt ─────────────────
add_library(sysmon-core STATIC
    src/core/AppNames.cpp
    src/core/FocusDetector.cpp
    src/core/ProcScan.cpp
    src/core/SysmonDb.cpp
)
target_include_directories(sysmon-core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(sysmon-core PUBLIC cxx_std_17)
target_link_libraries(sysmon-core PUBLIC sqlite3 X11 Xss Xext)

# ── SystemMonitor GUI ────────────────────────────────────────────────────
set(GUI_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/SampleWorker.cpp
    src/monitors/CpuMonitor.cpp
    src/monitors/MemoryMonitor.cpp
    src/monitors/DiskMonitor.cpp
    src/monitors/NetworkMonitor.cpp
    src/monitors/BatteryMonitor.cpp
    src/monitors/EnergyMonitor.cpp
    src/monitors/ScreenTimeTracker.cpp
    src/widgets/MetricsTab.cpp
    src/widgets/ProcessesTab.cpp
    src/widgets/LogsTab.cpp
    src/widgets/BatteryStatsTab.cpp
    src/widgets/ScreenTimeTab.cpp
)
set(GUI_HEADERS
    src/MainWindow.h
    src/SampleWorker.h
    src/Snapshot.h
    src/monitors/CpuMonitor.h
    src/monitors/MemoryMonitor.h
    src/monitors/DiskMonitor.h
    src/monitors/NetworkMonitor.h
    src/monitors/BatteryMonitor.h
    src/monitors/EnergyMonitor.h
    src/monitors/ScreenTimeTracker.h
    src/widgets/MetricsTab.h
    src/widgets/ProcessesTab.h
    src/widgets/LogsTab.h
    src/widgets/BatteryStatsTab.h
    src/widgets/ScreenTimeTab.h
)

add_executable(${PROJECT_NAME} ${GUI_SOURCES} ${GUI_HEADERS})
set_target_properties(${PROJECT_NAME} PROPERTIES AUTOMOC ON)
target_link_libraries(${PROJECT_NAME} PRIVATE
    sysmon-core
    Qt6::Widgets
    Qt6::Charts
    Threads::Threads
)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

# ── sysmon-tracker daemon ────────────────────────────────────────────────
# Build with: cmake --build build --target sysmon-tracker
# Install to service path: cmake --install build --prefix ~/.local
add_executable(sysmon-tracker src/daemon/sysmon-tracker.cpp)
target_link_libraries(sysmon-tracker PRIVATE sysmon-core Threads::Threads)
install(TARGETS sysmon-tracker RUNTIME DESTINATION bin)
