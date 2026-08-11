set(CORE_SOURCES
    model/NavigationNodes.cpp
    model/NavigationHistory.cpp
    util/Format.cpp
    backends/AccountsBackend.cpp
    backends/SystemdBackend.cpp
    backends/JournalBackend.cpp
    backends/SambaBackend.cpp
    backends/PerformanceBackend.cpp
    backends/UDisksBackend.cpp
)

add_library(aero7-compmgmt-core STATIC ${CORE_SOURCES})
target_include_directories(aero7-compmgmt-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aero7-compmgmt-core PUBLIC Qt6::Widgets Qt6::DBus)

add_executable(aero7-compmgmt
    main.cpp
    ui/MainWindow.cpp
    ui/ManagementPage.cpp
    ui/Pages.cpp
)
target_link_libraries(aero7-compmgmt PRIVATE aero7-compmgmt-core)

install(TARGETS aero7-compmgmt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

