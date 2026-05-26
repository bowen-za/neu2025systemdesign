cmake_minimum_required(VERSION 3.16)
project(vfs_unix_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(vfs_unix_simulator
    main.cpp
    utils.cpp
    virtual_disk.cpp
    filesystem_system.cpp
    filesystem_user.cpp
    filesystem_directory.cpp
    filesystem_file.cpp
    filesystem_api.cpp
)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets QUIET)
if(QT_FOUND)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets QUIET)
endif()
if(QT_FOUND AND Qt${QT_VERSION_MAJOR}_FOUND)
    add_executable(vfs_unix_simulator_qt
        qt_main.cpp
        qt_mainwindow.cpp
        utils.cpp
        virtual_disk.cpp
        filesystem_system.cpp
        filesystem_user.cpp
        filesystem_directory.cpp
        filesystem_file.cpp
        filesystem_api.cpp
    )
    target_link_libraries(vfs_unix_simulator_qt PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
endif()
