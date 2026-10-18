#include "DataDir.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <filesystem>
#include <fstream>
#include "Util.h"

namespace fs = std::filesystem;

namespace {

// 程序文件所在的目录（从别的目录启动时，也能找到程序旁边的 data）
fs::path exeDirectory() {
#ifdef _WIN32
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return {};
    return fs::path(exePath).parent_path();
#else
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);   // Linux
    return ec ? fs::path() : exe.parent_path();
#endif
}

}  // namespace

std::string DataDir::toNarrow(const std::wstring& text) { return Util::narrow(text); }

std::string DataDir::find() {
    // 正斜杠在 Windows 上同样可用
    const char* candidates[] = { "data/", "../data/", "../../data/", "../../../data/" };
    for (const char* dir : candidates) {
        std::ifstream test(std::string(dir) + "accounts.txt");
        if (test) return dir;
    }
    const fs::path exeDir = exeDirectory();
    if (!exeDir.empty()) {
        for (const char* dir : { "data", "../data", "../../data", "../../../data" }) {
            fs::path candidate = (exeDir / dir).lexically_normal();
            std::error_code ec;
            if (fs::exists(candidate / "accounts.txt", ec)) return candidate.u8string() + "/";
        }
    }
    return "data/";
}

std::string DataDir::absolute(const std::string& dir) {
    std::error_code ec;
    return fs::absolute(Util::pathOf(dir), ec).u8string();
}
