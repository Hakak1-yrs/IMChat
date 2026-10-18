#include "NetUtilYD.h"

#pragma comment(lib, "ws2_32.lib")   // 链接 Winsock 静态库

bool NetUtilYD::sendAll(SOCKET sock, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int n = send(sock, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n == SOCKET_ERROR) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool NetUtilYD::takeLine(std::string& buffer, std::string& line) {
    size_t pos = buffer.find('\n');
    if (pos == std::string::npos) return false;
    line = buffer.substr(0, pos);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    buffer.erase(0, pos + 1);
    return true;
}
