cmake_minimum_required(VERSION 3.16)
project(SystemMonitor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/monitors/CpuMonitor.cpp
    src/monitors/MemoryMonitor.cpp
    src/widgets/MetricsTab.cpp
    src/widgets/ProcessesTab.cpp
    src/widgets/LogsTab.cpp
)

set(HEADERS
    src/MainWindow.h
    src/monitors/CpuMonitor.h
    src/monitors/MemoryMonitor.h
    src/widgets/MetricsTab.h
    src/widgets/ProcessesTab.h
    src/widgets/LogsTab.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    Qt6::Charts
)