cmake_minimum_required(VERSION 3.16)
project(vfs_unix_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTORCC ON)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    set(QT_LIB_TARGET Qt6::Widgets)
elseif(NOT Qt6_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(QT_LIB_TARGET Qt5::Widgets)
endif()

add_executable(vfs_unix_simulator
    qt_main.cpp
    qt_mainwindow.cpp
    utils.cpp
    virtual_disk.cpp
    filesystem_system.cpp
    filesystem_user.cpp
    filesystem_directory.cpp
    filesystem_file.cpp
    filesystem_api.cpp
    resources.qrc
)

target_link_libraries(vfs_unix_simulator PRIVATE ${QT_LIB_TARGET})
