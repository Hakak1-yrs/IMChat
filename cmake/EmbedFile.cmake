# Turns a file into a C++ source file holding its bytes, so the server needs no files next to it.
# Usage: cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DNAME=<symbol> -P EmbedFile.cmake
# Defines `const unsigned char NAME[]` (NUL-terminated; unsigned so bytes >= 0x80 are not narrowing) and `const std::size_t NAME##Size` (without the NUL).
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hexLength)
math(EXPR size "${hexLength} / 2")
# 0xNN, per byte, a line break every 16 bytes: MSVC caps the length of a single string literal, not of an array
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}.tmp"
    "// Generated from ${INPUT} by cmake/EmbedFile.cmake. Do not edit.\n"
    "#include <cstddef>\n"
    "extern const unsigned char ${NAME}[] = {\n${bytes}0x00 };\n"
    "extern const std::size_t ${NAME}Size = ${size};\n")
# Only touch the output when the bytes changed, so an unrelated rebuild does not recompile it
configure_file("${OUTPUT}.tmp" "${OUTPUT}" COPYONLY)
file(REMOVE "${OUTPUT}.tmp")
