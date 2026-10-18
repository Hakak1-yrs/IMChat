cmake_minimum_required(VERSION 3.20)
project(IMChat VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "The current clients are Windows-only (Win32 / Direct2D).")
endif()

# All executables land in build/bin/<Config>/, so the programs find ../../../data from there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Shared compiler settings: UTF-8 source and execution charset, high warning level.
add_library(imchat_options INTERFACE)
target_compile_options(imchat_options INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4 /permissive- /MP>)
option(IMCHAT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if(IMCHAT_WARNINGS_AS_ERRORS)
    target_compile_options(imchat_options INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/WX>)
endif()

# ---- core: domain model, persistence, message log ----
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)
add_library(imchat_core STATIC ${CORE_SOURCES})
target_include_directories(imchat_core PUBLIC src/core)
target_link_libraries(imchat_core PUBLIC imchat_options bcrypt)

# ---- net: sockets helpers and the group chat room server ----
file(GLOB NET_SOURCES CONFIGURE_DEPENDS src/net/*.cpp src/net/*.h)
add_library(imchat_net STATIC ${NET_SOURCES})
target_include_directories(imchat_net PUBLIC src/net)
target_link_libraries(imchat_net PUBLIC imchat_core ws2_32)

# ---- console client ----
file(GLOB CONSOLE_SOURCES CONFIGURE_DEPENDS src/console/*.cpp src/console/*.h)
add_executable(IMChatConsole ${CONSOLE_SOURCES})
target_link_libraries(IMChatConsole PRIVATE imchat_net)

# ---- GUI client (Win32 + Direct2D) ----
file(GLOB GUI_SOURCES CONFIGURE_DEPENDS src/gui/*.cpp src/gui/*.h)
add_executable(IMChatClient WIN32 ${GUI_SOURCES})
target_compile_definitions(IMChatClient PRIVATE NOMINMAX)
target_link_libraries(IMChatClient PRIVATE imchat_net imchat_client d2d1 dwrite comctl32 dwmapi shell32)

# ---- protocol: frame codec and JSON packets (portable) ----
file(GLOB PROTOCOL_SOURCES CONFIGURE_DEPENDS src/protocol/*.cpp src/protocol/*.h)
add_library(imchat_protocol STATIC ${PROTOCOL_SOURCES})
target_include_directories(imchat_protocol PUBLIC src/protocol)
target_include_directories(imchat_protocol SYSTEM PUBLIC third_party)
target_link_libraries(imchat_protocol PUBLIC imchat_options)

# ---- reactor: non-blocking TCP networking with an event loop (portable) ----
file(GLOB REACTOR_SOURCES CONFIGURE_DEPENDS src/reactor/*.cpp src/reactor/*.h)
add_library(imchat_reactor STATIC ${REACTOR_SOURCES})
target_include_directories(imchat_reactor PUBLIC src/reactor)
target_link_libraries(imchat_reactor PUBLIC imchat_options)
if(WIN32)
    # WIN32_LEAN_AND_MEAN keeps windows.h from pulling in the old winsock.h, which clashes with winsock2.h
    target_compile_definitions(imchat_reactor PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(imchat_reactor PUBLIC ws2_32)
endif()

# ---- server ----
add_library(imchat_server STATIC src/server/ChatServer.cpp src/server/ChatServer.h src/server/LoginToken.cpp src/server/LoginToken.h)
target_include_directories(imchat_server PUBLIC src/server)
target_link_libraries(imchat_server PUBLIC imchat_reactor imchat_protocol imchat_core)
add_executable(IMChatServer src/server/main.cpp)
target_link_libraries(IMChatServer PRIVATE imchat_server)

# ---- network client library and the command-line client ----
add_library(imchat_client STATIC src/client/ChatClient.cpp src/client/ChatClient.h)
target_include_directories(imchat_client PUBLIC src/client)
target_link_libraries(imchat_client PUBLIC imchat_reactor imchat_protocol)
add_executable(IMChatCli src/cli/main.cpp)
target_link_libraries(IMChatCli PRIVATE imchat_client)

# ---- load-testing tool ----
add_executable(IMChatBench src/bench/main.cpp)
target_link_libraries(IMChatBench PRIVATE imchat_reactor imchat_protocol imchat_core)

# ---- tests ----
enable_testing()
add_executable(IMChatTests tests/TestMain.cpp)
target_link_libraries(IMChatTests PRIVATE imchat_core)
add_test(NAME IMChatTests COMMAND IMChatTests WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

option(IMCHAT_BUILD_UNIT_TESTS "Build the GoogleTest unit tests (downloads GoogleTest)" ON)
if(IMCHAT_BUILD_UNIT_TESTS)
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        URL_HASH SHA256=1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)   # match the default /MD runtime
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    file(GLOB UNIT_TEST_SOURCES CONFIGURE_DEPENDS tests/unit/*.cpp tests/unit/*.h)
    add_executable(IMChatUnitTests ${UNIT_TEST_SOURCES})
    target_link_libraries(IMChatUnitTests PRIVATE imchat_server imchat_client GTest::gtest_main)
    target_compile_definitions(IMChatUnitTests PRIVATE IMCHAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    include(GoogleTest)
    gtest_discover_tests(IMChatUnitTests WORKING_DIRECTORY ${CMAKE_BINARY_DIR} DISCOVERY_MODE PRE_TEST)
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT IMChatClient)
