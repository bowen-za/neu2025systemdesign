cmake_minimum_required(VERSION 3.16)
project(vfs_unix_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(vfs_unix_simulator
    main.cpp
    utils.cpp
    virtual_disk.cpp
    filesystem.cpp
)
