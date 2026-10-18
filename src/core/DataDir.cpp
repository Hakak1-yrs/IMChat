#include "DataDir.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include "Util.h"

namespace fs = std::filesystem;

std::string DataDir::toNarrow(const std::wstring& text) { return Util::narrow(text); }

std::string DataDir::find() {
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

std::string DataDir::absolute(const std::string& dir) {
    std::error_code ec;
    return toNarrow(fs::absolute(Util::pathOf(dir), ec).wstring());
}
