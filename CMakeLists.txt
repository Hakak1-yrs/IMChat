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
target_link_libraries(IMChatClient PRIVATE imchat_net d2d1 dwrite comctl32 dwmapi shell32)

# ---- tests ----
enable_testing()
add_executable(IMChatTests tests/TestMain.cpp)
target_link_libraries(IMChatTests PRIVATE imchat_core)
add_test(NAME IMChatTests COMMAND IMChatTests WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT IMChatClient)
