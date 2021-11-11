cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(monitor-sync)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)
find_package(Threads REQUIRED)
find_package(X11 REQUIRED)
CPMAddPackage("gh:chriskohlhoff/asio#asio-1-20-0@1.20.0")

if (asio_ADDED)
    add_library(asio INTERFACE)
    target_include_directories(asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
    target_link_libraries(asio INTERFACE Threads::Threads)
endif ()

include_directories(include)
add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} asio X11 Xext)
target_precompile_headers(${PROJECT_NAME} PUBLIC include/args.hxx)

target_compile_options(${PROJECT_NAME}
        PUBLIC
        -Wall
        -Wextra
        -Wcast-align
        -Wpedantic
        -Werror=return-type
        -Wno-unused-parameter
        -Wno-ignored-attributes
        )

set(SANITISE OFF)

if (SANITISE)
    set(DEBUG_OPTIONS -O2 -fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined)
    target_compile_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:DEBUG>:${DEBUG_OPTIONS}>")
    target_link_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:DEBUG>:${DEBUG_OPTIONS}>")
endif ()


install(TARGETS ${PROJECT_NAME}
        CONFIGURATIONS Release)

if (NOT AUTOSTART_DIR)
    set(XDG_AUTOSTART_DIR $ENV{HOME}/.config/autostart)
    if (EXISTS ${XDG_AUTOSTART_DIR})
        set(AUTOSTART_DIR ${XDG_AUTOSTART_DIR})
    endif ()
endif ()

if (AUTOSTART_DIR)
    message(STATUS "AUTOSTART_DIR is set to ${AUTOSTART_DIR}")

    configure_file(monitor-sync-client.desktop.in monitor-sync-client.desktop @ONLY)
    configure_file(monitor-sync-server.desktop.in monitor-sync-server.desktop @ONLY)

    add_custom_target(install-autostart-server
            DEPENDS ${PROJECT_NAME}
            COMMENT "Copying .desktop file to ${AUTOSTART_DIR}..."
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/monitor-sync-server.desktop ${AUTOSTART_DIR}/.
            )

    add_custom_target(install-autostart-client
            DEPENDS ${PROJECT_NAME}
            COMMENT "Copying .desktop file to ${AUTOSTART_DIR}..."
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/monitor-sync-client.desktop ${AUTOSTART_DIR}/.
            )
else ()
    message(STATUS "AUTOSTART_DIR not set, the install targets won't install XDG autostart files")
endif ()


