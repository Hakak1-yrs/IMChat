cmake_minimum_required(VERSION 3.20)
project(IMChat VERSION 0.1.0 LANGUAGES C CXX)   # C: the SQLite amalgamation

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The server, the client libraries, the command-line client, the benchmark and the tests build on
# Windows (MSVC or MinGW) and on Linux. The GUI, the console client and the old blocking network code are Windows-only.
find_package(Threads REQUIRED)
if(CYGWIN)
    # Cygwin / MSYS2 hide POSIX functions (pread, fileno, ...) in strict -std=c++17 mode; glibc g++ defines this already.
    add_compile_definitions(_GNU_SOURCE)
endif()

# All executables land in build/bin/<Config>/, so the programs find ../../../data from there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Shared compiler settings: UTF-8 source and execution charset, high warning level.
add_library(imchat_options INTERFACE)
target_compile_options(imchat_options INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W4 /permissive- /MP>)
target_compile_options(imchat_options INTERFACE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>)
# Portable code uses the standard sscanf/fopen family; MSVC would otherwise ask for its *_s variants.
target_compile_definitions(imchat_options INTERFACE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
option(IMCHAT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if(IMCHAT_WARNINGS_AS_ERRORS)
    target_compile_options(imchat_options INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/WX> $<$<CXX_COMPILER_ID:GNU,Clang>:-Werror>)
endif()
if(MINGW)
    # MinGW's headers default to an older Windows; BCryptHash pseudo-handles and per-monitor DPI need Windows 10.
    target_compile_definitions(imchat_options INTERFACE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000C)
endif()
# GCC / Clang sanitizers, e.g. -DIMCHAT_SANITIZE=address,undefined or -DIMCHAT_SANITIZE=thread (used by CI on Linux).
set(IMCHAT_SANITIZE "" CACHE STRING "Comma-separated sanitizers to enable (GCC/Clang; MSVC supports only 'address')")
if(IMCHAT_SANITIZE AND MSVC)
    if(NOT IMCHAT_SANITIZE STREQUAL "address")
        message(FATAL_ERROR "MSVC supports only IMCHAT_SANITIZE=address")
    endif()
    # MSVC's container annotations must match across all objects, GoogleTest included: apply globally
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/INCREMENTAL:NO /DEBUG)
elseif(IMCHAT_SANITIZE)
    target_compile_options(imchat_options INTERFACE -fsanitize=${IMCHAT_SANITIZE} -fno-omit-frame-pointer)
    target_link_options(imchat_options INTERFACE -fsanitize=${IMCHAT_SANITIZE})
endif()

# ---- SQLite (amalgamation, pinned by hash): the optional database message store ----
include(FetchContent)
FetchContent_Declare(sqlite
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3460100.zip
    URL_HASH SHA256=77823cb110929c2bcb0f5d48e4833b5c59a8a6e40cdea3936b99e199dbbe5784
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sqlite)
add_library(imchat_sqlite STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(imchat_sqlite SYSTEM PUBLIC ${sqlite_SOURCE_DIR})
target_compile_definitions(imchat_sqlite PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)
target_link_libraries(imchat_sqlite PUBLIC Threads::Threads)

# ---- core: domain model, persistence, message log ----
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)
add_library(imchat_core STATIC ${CORE_SOURCES})
target_include_directories(imchat_core PUBLIC src/core)
target_link_libraries(imchat_core PUBLIC imchat_options imchat_sqlite Threads::Threads)
if(WIN32)
    target_link_libraries(imchat_core PUBLIC bcrypt)
else()
    find_package(Iconv REQUIRED)   # converting legacy GBK data files
    target_link_libraries(imchat_core PUBLIC Iconv::Iconv)
endif()

if(WIN32)
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
    target_link_libraries(IMChatClient PRIVATE imchat_net imchat_client d2d1 dwrite comctl32 dwmapi shell32 dbghelp)
    if(MINGW)
        target_link_options(IMChatClient PRIVATE -municode)   # the GUI's entry point is wWinMain
    endif()
endif()

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
target_link_libraries(imchat_reactor PUBLIC imchat_options Threads::Threads)
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
add_executable(IMChatTests tests/TestMain.cpp)   # the course's original business tests
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
    # The same server and client integration tests again, with the server running 3 I/O threads.
    add_test(NAME IMChatUnitTests.MultiThreadedServer
             COMMAND IMChatUnitTests --gtest_filter=ChatServerTest.*:ChatClientTest.*
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties(IMChatUnitTests.MultiThreadedServer PROPERTIES ENVIRONMENT IMCHAT_TEST_IO_THREADS=3)
    # ...and once more with messages stored in SQLite instead of the text log.
    add_test(NAME IMChatUnitTests.SqliteStorage
             COMMAND IMChatUnitTests --gtest_filter=ChatServerTest.*:ChatClientTest.*
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties(IMChatUnitTests.SqliteStorage PROPERTIES ENVIRONMENT IMCHAT_TEST_STORAGE=sqlite)
endif()

if(WIN32)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT IMChatClient)
endif()
