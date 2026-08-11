add_executable(test-navigation test_navigation.cpp)
target_link_libraries(test-navigation PRIVATE aero7-compmgmt-core)
add_test(NAME navigation COMMAND test-navigation)

add_executable(test-backends test_backends.cpp)
target_link_libraries(test-backends PRIVATE aero7-compmgmt-core)
add_test(NAME backend-parsers COMMAND test-backends)

add_executable(test-ui-layout test_ui_layout.cpp)
target_link_libraries(test-ui-layout PRIVATE aero7-compmgmt-ui)
add_test(NAME ui-layout COMMAND test-ui-layout)
set_tests_properties(ui-layout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME cli-help COMMAND aero7-compmgmt --help)
add_test(NAME cli-settings-json COMMAND aero7-compmgmt --list-settings-json)
set_tests_properties(cli-settings-json PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME cli-invalid-node COMMAND aero7-compmgmt --open not-a-real-node)
set_tests_properties(cli-invalid-node PROPERTIES WILL_FAIL TRUE
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(
    NAME desktop-entries
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/validate_desktop_entries.cmake)
