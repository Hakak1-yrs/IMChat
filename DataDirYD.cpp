#include "DataDirYD.h"
#include <windows.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

std::string DataDirYD::toNarrow(const std::wstring& text) {
    int length = WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return "";
    std::string result(length - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, &result[0], length, nullptr, nullptr);
    return result;
}

std::string DataDirYD::find() {
    const char* candidates[] = { "data\\", "..\\data\\", "..\\..\\data\\", "..\\..\\..\\data\\" };
    for (const char* dir : candidates) {
        std::ifstream test(std::string(dir) + "accounts.txt");
        if (test) return dir;
    }
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        fs::path exeDir = fs::path(exePath).parent_path();
        for (const wchar_t* dir : { L"data", L"..\\data", L"..\\..\\data", L"..\\..\\..\\data" }) {
            fs::path candidate = (exeDir / dir).lexically_normal();
            std::error_code ec;
            if (fs::exists(candidate / L"accounts.txt", ec)) return toNarrow(candidate.wstring()) + "\\";
        }
    }
    return "data\\";
}

std::string DataDirYD::absolute(const std::string& dir) {
    std::error_code ec;
    return toNarrow(fs::absolute(fs::path(dir), ec).wstring());
}
