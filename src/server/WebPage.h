#pragma once
#include <cstddef>

// 网页客户端 web/index.html 的内容：构建时由 cmake/EmbedFile.cmake 生成 WebPage.cpp，嵌进服务端程序里，
// 部署时只需要一个可执行文件。改了网页重新构建即可
extern const unsigned char kWebPage[];
extern const std::size_t kWebPageSize;
