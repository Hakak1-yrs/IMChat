#pragma once
#include <filesystem>
#include <string>

// 单个数据文件的读写管理。
//
// 1. 记住“上次读入/写出时的内容”和“当时的修改时间”；
// 2. 内容没有变化就不写文件——只用来聊天的第二个窗口退出时不会动数据文件；
// 3. 能判断文件在本窗口读入之后是否被别的窗口改过，由 PlatformYD 决定是否改为另存冲突副本；
// 4. 写文件分两步：prepare() 先把内容完整写入临时文件并刷到磁盘（FlushFileBuffers），
//    commit() 再用临时文件一次性替换原文件——中途断电也不会留下写了一半的文件；
// 5. 文件被其他编辑器另存为 UTF-8（带或不带 BOM）时，读入时自动转换为程序使用的 GBK，下次保存写回 GBK。
class DataFileYD {
public:
    DataFileYD(const std::string& dataDir, const std::string& fileName);

    bool read(std::string& text);                     // 读入整个文件并记住快照；文件不存在返回 false
    bool convertedFromUtf8() const;                   // 读入时是否做过 UTF-8 → GBK 转换

    bool hasChanges(const std::string& text) const;   // 与上次读写时的内容不同（或需要写回 GBK）
    bool modifiedOutside() const;                     // 上次读写之后文件被其他程序修改、删除或新建
    // 文件虽被其他窗口改过，但现在的内容正好就是 text（两个窗口做了同样的修改）：接受它，返回 true
    bool adoptIfSame(const std::string& text);
    bool prepare(const std::string& text);            // 第一步：写临时文件并刷到磁盘
    bool commit();                                    // 第二步：用临时文件替换正式文件
    void discard();                                   // 放弃本次保存：删除临时文件
    bool saveCopy(const std::string& dir, const std::string& text) const;   // 把 text 另存到 dir（冲突副本）
    bool copyTo(const std::string& dir) const;        // 把当前文件原样复制到 dir（用于备份）

    const std::string& fileName() const;

private:
    std::filesystem::path path_;
    std::filesystem::path tempPath_;
    std::string fileName_;
    std::string snapshot_;                     // 上次读入或写出的内容
    std::string prepared_;                     // 已写入临时文件、等待 commit() 的内容
    std::filesystem::file_time_type stamp_{};  // 上次读入或写出后的修改时间
    bool existed_ = false;                     // 上次读入或写出时文件是否存在
    bool convertedFromUtf8_ = false;           // 读入时做过编码转换，下次保存要写回 GBK
};
