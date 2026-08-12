set(CORE_SOURCES
    model/NavigationNodes.cpp
    model/NavigationHistory.cpp
    util/Format.cpp
    backends/SystemInfoBackend.cpp
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

set(UI_SOURCES
    ui/MainWindow.cpp
    ui/ManagementPage.cpp
    ui/DiskMapWidget.cpp
    ui/DiskDialogs.cpp
    ui/PerformanceGraph.cpp
    ui/Pages.cpp
)

add_library(aero7-compmgmt-ui STATIC ${UI_SOURCES})
target_include_directories(aero7-compmgmt-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aero7-compmgmt-ui PUBLIC aero7-compmgmt-core Qt6::Concurrent)

add_executable(aero7-compmgmt main.cpp)
target_link_libraries(aero7-compmgmt PRIVATE aero7-compmgmt-ui)

add_executable(aero7-compmgmt-helper helper/main.cpp)
target_link_libraries(aero7-compmgmt-helper PRIVATE Qt6::Core)

install(TARGETS aero7-compmgmt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS aero7-compmgmt-helper RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/aero7)
