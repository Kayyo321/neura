# The server host on Windows (docs/design/Windows030.md, W14 and W27; package
# WP10).
#
# The HTTP libraries, the listeners and neura-server-host are built from the
# same sources as on POSIX. Their Windows code is in _WIN32 branches over the
# platform layer: Winsock 2.2 sockets, WSAPoll, AF_UNIX socket pairs for the
# worker channels and the wake-ups, and platform::Process for the workers,
# which start with exec only (the zygote needs Linux). With these targets
# defined, CMakeLists.txt registers the server tests that test for them.

# After the server libraries' POSIX block: neura_server_http and
# neura_server_listeners, which reach Winsock through neura_platform.
macro(neura_windows_server_libraries)
    add_library(neura_server_http STATIC EXCLUDE_FROM_ALL
        "${PROJECT_SOURCE_DIR}/src/server/http_wire.cpp"
        "${PROJECT_SOURCE_DIR}/src/server/http_connection.cpp"
        "${PROJECT_SOURCE_DIR}/src/server/http_tcp.cpp")
    target_include_directories(neura_server_http PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(neura_server_http PUBLIC neura_platform ws2_32)
    neura_target_defaults(neura_server_http)

    add_library(neura_server_listeners STATIC EXCLUDE_FROM_ALL "${PROJECT_SOURCE_DIR}/src/server/http_listener.cpp")
    target_include_directories(neura_server_listeners PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(neura_server_listeners PRIVATE neura_server_http PUBLIC neura_platform ws2_32)
    neura_target_defaults(neura_server_listeners)
endmacro()

# After the server host's POSIX block: the request-worker objects and the
# tool. The tool's code goes into neura-core-<version>.dll with the core
# archives it calls (W04), and neura-server-host.exe is its stub, which the
# host also starts as each worker (--worker).
macro(neura_windows_server_tools)
    find_package(Threads REQUIRED)
    add_library(neura_server_worker STATIC
        "${PROJECT_SOURCE_DIR}/src/server/worker_messages.cpp"
        "${PROJECT_SOURCE_DIR}/src/server/request_worker.cpp"
        "${PROJECT_SOURCE_DIR}/src/server/worker_zygote.cpp"
        "${PROJECT_SOURCE_DIR}/src/server/sql_channel.cpp"
        "${PROJECT_SOURCE_DIR}/src/server/sql_host.cpp")
    target_include_directories(neura_server_worker PUBLIC "${PROJECT_SOURCE_DIR}/src"
        PRIVATE "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(neura_server_worker PUBLIC neura_server_worker_wire neura_platform ws2_32)
    if(TARGET neura_sql)
        target_compile_definitions(neura_server_worker PUBLIC NEURA_HAS_SQL)
    endif()
    neura_target_defaults(neura_server_worker)
    neura_add_tool(neura-server-host core src/server/server_host.cpp neura_server_worker neura_server_listeners
        neura_server_http neura_core_library Threads::Threads)
    add_dependencies(neura neura-server-host)
endmacro()
