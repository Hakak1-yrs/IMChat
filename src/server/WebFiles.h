#pragma once
#include <cstddef>
#include <string>

// 网页客户端（web/ 目录下的页面、样式和脚本）：构建时由 cmake/EmbedWebFiles.cmake 生成 WebFiles.cpp，
// 嵌进服务端程序里，部署时只需要一个可执行文件。改了网页重新构建即可
struct WebFile {
    const char* path;          // "/index.html"、"/js/app.js"……
    const char* contentType;
    const unsigned char* data;
    std::size_t size;
};

extern const WebFile kWebFiles[];
extern const std::size_t kWebFileCount;

// 按请求路径找文件，"/" 就是 "/index.html"；没有时返回空
inline const WebFile* findWebFile(const std::string& path) {
    const std::string wanted = path == "/" ? "/index.html" : path;
    for (std::size_t i = 0; i < kWebFileCount; ++i) {
        if (wanted == kWebFiles[i].path) return &kWebFiles[i];
    }
    return nullptr;
}
