// 模拟即时通信系统 —— 面向对象程序课程设计
// 作者：yd
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include "DataDirYD.h"
#include "MenuYD.h"
#include "PlatformYD.h"

static PlatformYD* g_platform = nullptr;

// 直接点击窗口 × 或按 Ctrl+C 时，main 中的对象来不及析构，因此在这里先保存数据
static BOOL WINAPI onConsoleClose(DWORD type) {
    if (g_platform && (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT)) {
        g_platform->saveOnExit();
    }
    return FALSE;   // 交给系统默认处理（结束程序）
}

int main() {
    // 源码中的中文按 GBK 编译，控制台和数据文件也统一使用 GBK，避免乱码
    SetConsoleOutputCP(936);
    SetConsoleCP(936);

    const std::string dataDir = DataDirYD::find();
    std::cout << "[系统] 数据目录：" << DataDirYD::absolute(dataDir) << "\n";
    PlatformYD platform(dataDir);   // 构造时从文件加载数据（并检查数据是否前后一致）
    g_platform = &platform;
    SetConsoleCtrlHandler(onConsoleClose, TRUE);
    MenuYD menu(platform);
    try {
        menu.run();
    } catch (const std::exception& e) {
        std::cout << "\n程序结束：" << e.what() << "\n";
    }
    SetConsoleCtrlHandler(onConsoleClose, FALSE);
    g_platform = nullptr;
    return 0;   // platform 析构时自动把数据写回文件
}
