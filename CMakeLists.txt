cmake_minimum_required(VERSION 3.24)
project(aero7-computer-management VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets DBus Concurrent)

include(GNUInstallDirs)
enable_testing()

add_subdirectory(src)
add_subdirectory(tests)

install(FILES packaging/aero7-computer-management.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(DIRECTORY packaging/applications/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        FILES_MATCHING PATTERN "*.desktop")

install(FILES packaging/com.aero7.computermanagement.policy
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
