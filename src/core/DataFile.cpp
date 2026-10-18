#include "DataFile.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include "Util.h"

namespace fs = std::filesystem;

namespace {

// GBK 中文几乎不可能恰好构成合法的 UTF-8 序列，所以“不是合法 UTF-8”就可以认定是旧版本的 GBK 文件
bool isValidUtf8(const std::string& text) {
    return text.empty() ||
           MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0;
}

std::string gbkToUtf8(const std::string& text) {
    int wideLength = MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(wideLength, L'\0');
    MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), &wide[0], wideLength);
    int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, &result[0], length, nullptr, nullptr);
    return result;
}

// 读入整个文件并统一成程序内部的形式；文件不存在（或打不开）返回 false
bool readNormalized(const fs::path& path, std::string& text, bool& convertedFromGbk) {
    convertedFromGbk = false;
    text.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    // 记事本等编辑器保存时可能加上 BOM，去掉即可；旧版本程序写的 GBK 文件转换成 UTF-8
    bool bom = text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
               static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF;
    if (bom) text.erase(0, 3);
    if (!isValidUtf8(text)) {
        text = gbkToUtf8(text);
        convertedFromGbk = true;
    }
    // 统一换行符：记事本或旧版本程序写出的 \r\n 与程序生成的 \n 视为相同内容，
    // 避免“内容其实没变”却被判定为已修改
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return true;
}

}  // namespace

DataFile::DataFile(const std::string& dataDir, const std::string& fileName)
    : path_(Util::pathOf(dataDir) / Util::pathOf(fileName)),
      tempPath_(Util::pathOf(dataDir) / Util::pathOf(fileName + ".tmp")),
      fileName_(fileName) {}

bool DataFile::read(std::string& text) {
    existed_ = readNormalized(path_, text, convertedFromGbk_);
    snapshot_ = text;
    return existed_;
}

bool DataFile::convertedFromGbk() const { return convertedFromGbk_; }

bool DataFile::hasChanges(const std::string& text) const {
    return !existed_ || convertedFromGbk_ || text != snapshot_;
}

bool DataFile::modifiedOutside() const {
    // 比较内容而不是修改时间：修改时间的精度因平台而异（MinGW 的 libstdc++ 在 Windows 上只精确到秒），
    // 两个窗口在同一秒内先后保存时，只看修改时间会漏掉冲突。数据文件都不大，重读一遍的代价可以忽略
    std::string disk;
    bool converted = false;
    const bool existsNow = readNormalized(path_, disk, converted);
    if (existsNow != existed_) return true;   // 被删除或新建（读不到时也按“被改过”处理，宁可另存也不覆盖）
    return existsNow && disk != snapshot_;
}

bool DataFile::adoptIfSame(const std::string& text) {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string disk = buffer.str();
    disk.erase(std::remove(disk.begin(), disk.end(), '\r'), disk.end());
    if (disk != text) return false;
    snapshot_ = text;   // 文件里已经是本窗口要写的内容（另一个窗口做了同样的修改），记下来，不必再写
    existed_ = true;
    convertedFromGbk_ = false;
    return true;
}

bool DataFile::prepare(const std::string& text) {
    std::error_code ec;
    if (!path_.parent_path().empty()) fs::create_directories(path_.parent_path(), ec);   // data 目录不存在时自动创建
    HANDLE file = CreateFileW(tempPath_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    // FlushFileBuffers 要求系统把数据真正写到磁盘，而不是只停留在内存缓存中
    bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
              written == text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) {
        fs::remove(tempPath_, ec);
        return false;
    }
    prepared_ = text;
    return true;
}

bool DataFile::commit() {
    // 同一磁盘上的替换是一次原子操作：要么还是旧文件，要么已是完整的新文件
    if (!MoveFileExW(tempPath_.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        discard();
        return false;
    }
    snapshot_ = prepared_;
    existed_ = true;
    convertedFromGbk_ = false;
    return true;
}

void DataFile::discard() {
    std::error_code ec;
    fs::remove(tempPath_, ec);
}

bool DataFile::saveCopy(const std::string& dir, const std::string& text) const {
    std::error_code ec;
    fs::create_directories(Util::pathOf(dir), ec);
    std::ofstream copy(Util::pathOf(dir) / Util::pathOf(fileName_), std::ios::binary | std::ios::trunc);
    copy << text;
    copy.flush();
    return static_cast<bool>(copy);
}

bool DataFile::copyTo(const std::string& dir) const {
    std::error_code ec;
    if (!fs::exists(path_, ec)) return true;
    fs::create_directories(Util::pathOf(dir), ec);
    return fs::copy_file(path_, Util::pathOf(dir) / Util::pathOf(fileName_), fs::copy_options::overwrite_existing, ec);
}

const std::string& DataFile::fileName() const { return fileName_; }
