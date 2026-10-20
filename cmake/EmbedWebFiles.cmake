# Compiles the files of the web client into a C++ source file, so the server needs no files next to it.
# Usage: cmake -DROOT=<web dir> -DOUTPUT=<file.cpp> -P EmbedWebFiles.cmake
# Every file under ROOT becomes an entry of kWebFiles (see src/server/WebFiles.h), served at "/<path relative to ROOT>".
file(GLOB_RECURSE files RELATIVE "${ROOT}" "${ROOT}/*")
list(SORT files)
set(arrays "")
set(entries "")
set(index 0)
foreach(path IN LISTS files)
    file(READ "${ROOT}/${path}" hex HEX)
    string(LENGTH "${hex}" hexLength)
    math(EXPR size "${hexLength} / 2")
    # 0xNN per byte, a line break every 16 bytes: MSVC caps the length of one string literal, not of an array.
    # unsigned char, so that bytes >= 0x80 are not narrowing conversions
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n" bytes "${bytes}")
    string(APPEND arrays "const unsigned char file${index}[] = {\n${bytes}0x00 };\n")
    get_filename_component(extension "${path}" LAST_EXT)
    string(TOLOWER "${extension}" extension)
    set(type "application/octet-stream")
    if(extension STREQUAL ".html")
        set(type "text/html; charset=utf-8")
    elseif(extension STREQUAL ".js")
        set(type "text/javascript; charset=utf-8")
    elseif(extension STREQUAL ".css")
        set(type "text/css; charset=utf-8")
    elseif(extension STREQUAL ".svg")
        set(type "image/svg+xml")
    elseif(extension STREQUAL ".png")
        set(type "image/png")
    elseif(extension STREQUAL ".json")
        set(type "application/json")
    endif()
    string(APPEND entries "    { \"/${path}\", \"${type}\", file${index}, ${size} },\n")
    math(EXPR index "${index} + 1")
endforeach()
file(WRITE "${OUTPUT}.tmp"
    "// Generated from web/ by cmake/EmbedWebFiles.cmake. Do not edit.\n"
    "#include \"WebFiles.h\"\n"
    "namespace {\n${arrays}}  // namespace\n"
    "extern const WebFile kWebFiles[] = {\n${entries}};\n"
    "extern const std::size_t kWebFileCount = ${index};\n")
# Only touch the output when something changed, so an unrelated rebuild does not recompile it
configure_file("${OUTPUT}.tmp" "${OUTPUT}" COPYONLY)
file(REMOVE "${OUTPUT}.tmp")
