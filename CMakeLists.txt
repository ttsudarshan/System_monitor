cmake_minimum_required(VERSION 3.16)
project(SystemMonitor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Sql)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED dbus-1)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/monitors/CpuMonitor.cpp
    src/monitors/MemoryMonitor.cpp
    src/monitors/DiskMonitor.cpp
    src/monitors/NetworkMonitor.cpp
    src/monitors/BatteryMonitor.cpp
    src/monitors/BatteryTracker.cpp
    src/monitors/EnergyMonitor.cpp
    src/monitors/ScreenTimeTracker.cpp
    src/widgets/MetricsTab.cpp
    src/widgets/ProcessesTab.cpp
    src/widgets/LogsTab.cpp
    src/widgets/BatteryStatsTab.cpp
    src/widgets/ScreenTimeTab.cpp
)

set(HEADERS
    src/MainWindow.h
    src/monitors/CpuMonitor.h
    src/monitors/MemoryMonitor.h
    src/monitors/DiskMonitor.h
    src/monitors/NetworkMonitor.h
    src/monitors/BatteryMonitor.h
    src/monitors/BatteryTracker.h
    src/monitors/EnergyMonitor.h
    src/monitors/ScreenTimeTracker.h
    src/widgets/MetricsTab.h
    src/widgets/ProcessesTab.h
    src/widgets/LogsTab.h
    src/widgets/BatteryStatsTab.h
    src/widgets/ScreenTimeTab.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${DBUS_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Charts
    Qt6::Sql
    X11
    Xss
    ${DBUS_LIBRARIES}
)