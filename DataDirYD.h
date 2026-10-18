#pragma once
#include <string>

// 数据目录查找：控制台程序和图形界面程序共用。
// 先按当前工作目录找，兼容在 VS 中调试运行（工作目录为项目目录）和直接双击 exe 运行；
// 再按 exe 所在目录找，从其他目录启动时也能用上同一份数据，而不是悄悄新建一份空数据
class DataDirYD {
public:
    static std::string find();                               // 返回以 \ 结尾的目录
    static std::string absolute(const std::string& dir);     // 完整路径（用于显示）
    static std::string toNarrow(const std::wstring& text);   // 宽字符路径 → 程序内统一使用的 UTF-8 字符串
};
