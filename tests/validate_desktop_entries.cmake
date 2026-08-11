set(expected_entries
    "aero7-task-scheduler.desktop|task-scheduler|appointment-new"
    "aero7-event-viewer.desktop|event-viewer|view-list-details"
    "aero7-shared-folders.desktop|shared-folders|folder-network"
    "aero7-local-users-groups.desktop|local-users-groups|system-users"
    "aero7-users.desktop|users|user-identity"
    "aero7-groups.desktop|groups|system-users"
    "aero7-performance-monitor.desktop|performance|utilities-system-monitor"
    "aero7-device-manager.desktop|device-manager|preferences-system-devices"
    "aero7-disk-management.desktop|disk-management|drive-harddisk"
    "aero7-services.desktop|services|preferences-system-services")

foreach(entry IN LISTS expected_entries)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 file_name)
    list(GET fields 1 page_id)
    list(GET fields 2 icon_name)
    set(path "${SOURCE_DIR}/packaging/applications/${file_name}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing module desktop entry: ${file_name}")
    endif()
    file(READ "${path}" contents)
    if(NOT contents MATCHES "Exec=aero7-compmgmt --open ${page_id}")
        message(FATAL_ERROR "${file_name} does not open ${page_id}")
    endif()
    if(NOT contents MATCHES "Icon=${icon_name}")
        message(FATAL_ERROR "${file_name} does not use ${icon_name}")
    endif()
endforeach()
