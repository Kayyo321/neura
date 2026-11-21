# The runtime service on Windows (docs/design/Windows030.md, W29; package WP09).
#
# neurad, the client of `neura run --runtime=...` and the `neura runtime`
# commands run natively: an AF_UNIX socket in an owner-only run directory
# under %LOCALAPPDATA%/Neura/run, the peer's user SID and integrity level for
# identity, handles passed with DuplicateHandle, images in a section that only
# the supervisor can write, and a kill-on-close job per worker. With
# NEURA_WINDOWS_SERVICE set, CMakeLists.txt builds the neurad tool (into the
# core DLL, as every core tool) and service-unit-environment; the hooks below
# add the sources and the end-to-end suites.
set(NEURA_WINDOWS_SERVICE ON)

# The service's sources join the execution core, as on POSIX.
macro(neura_windows_service_sources)
    target_sources(neura_cli_core PRIVATE
        src/service/client.cpp
        src/service/connect.cpp
        src/service/control.cpp
        src/service/launch.cpp
        src/service/login.cpp
        src/service/supervisor.cpp
        src/service/system.cpp
        src/service/worker.cpp
    )
    target_link_libraries(neura_cli_core PUBLIC neura_service_protocol neura_platform ws2_32 advapi32 bcrypt)
    target_compile_definitions(neura_cli_core PUBLIC NEURA_HAS_SERVICE=1)
endmacro()

# The worker driver and the end-to-end suites of tests/service/service.py,
# registered under the same names as on Linux.
macro(neura_windows_service_tests)
    add_executable(neura_service_worker_driver tests/service/worker_driver.cpp)
    target_link_libraries(neura_service_worker_driver PRIVATE neura_cli_core)
    target_include_directories(neura_service_worker_driver PRIVATE src)
    neura_target_defaults(neura_service_worker_driver)
    add_test(NAME service-worker COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/service/service.py" worker "$<TARGET_FILE_DIR:neura>"
        "$<TARGET_FILE:neura_service_worker_driver>")
    set_tests_properties(service-worker PROPERTIES TIMEOUT 600)
    foreach(neura_service_suite IN ITEMS cli-options off-default protocol run parity control login isolation)
        add_test(NAME service-${neura_service_suite} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/service/service.py" ${neura_service_suite} "$<TARGET_FILE_DIR:neura>")
        set_tests_properties(service-${neura_service_suite} PROPERTIES TIMEOUT 600)
        if(NEURA_HAS_GPU)
            set_tests_properties(service-${neura_service_suite} PROPERTIES ENVIRONMENT "NEURA_TEST_GPU_BUILD=1")
        endif()
    endforeach()
endmacro()
