#include "DataFileYD.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// 含有非 ASCII 字节、并且整体是合法 UTF-8 的文本，视为 UTF-8。
// GBK 中文几乎不可能恰好构成合法的 UTF-8 序列，因此不会把正常的 GBK 文件误判为 UTF-8
bool looksLikeUtf8(const std::string& text) {
    bool hasNonAscii = std::any_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x80; });
    if (!hasNonAscii) return false;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0;
}

std::string utf8ToGbk(const std::string& text) {
    int wideLength = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(wideLength, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &wide[0], wideLength);
    int length = WideCharToMultiByte(936, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(936, 0, wide.data(), wideLength, &result[0], length, nullptr, nullptr);
    return result;
}

}  // namespace

DataFileYD::DataFileYD(const std::string& dataDir, const std::string& fileName)
    : path_(fs::path(dataDir) / fileName), tempPath_(fs::path(dataDir) / (fileName + ".tmp")), fileName_(fileName) {}

bool DataFileYD::read(std::string& text) {
    convertedFromUtf8_ = false;
    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        existed_ = false;
        snapshot_.clear();
        text.clear();
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    // 记事本、VS Code 等编辑器可能把文件另存为 UTF-8：去掉 BOM 并转换为 GBK，避免中文变成乱码
    bool bom = text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
               static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF;
    if (bom) text.erase(0, 3);
    if (bom || looksLikeUtf8(text)) {
        text = utf8ToGbk(text);
        convertedFromUtf8_ = true;
    }
    // 统一换行符：记事本或旧版本程序写出的 \r\n 与程序生成的 \n 视为相同内容，
    // 避免“内容其实没变”却被判定为已修改
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    snapshot_ = text;
    existed_ = true;
    std::error_code ec;
    stamp_ = fs::last_write_time(path_, ec);
    return true;
}

bool DataFileYD::convertedFromUtf8() const { return convertedFromUtf8_; }

bool DataFileYD::hasChanges(const std::string& text) const {
    return !existed_ || convertedFromUtf8_ || text != snapshot_;
}

bool DataFileYD::modifiedOutside() const {
    std::error_code ec;
    bool existsNow = fs::exists(path_, ec);
    if (existsNow != existed_) return true;
    if (!existsNow) return false;
    fs::file_time_type now = fs::last_write_time(path_, ec);
    return ec || now != stamp_;   // 读不到修改时间时按“被改过”处理，宁可另存也不覆盖
}

bool DataFileYD::adoptIfSame(const std::string& text) {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string disk = buffer.str();
    disk.erase(std::remove(disk.begin(), disk.end(), '\r'), disk.end());
    if (disk != text) return false;
    snapshot_ = text;   // 文件里已经是本窗口要写的内容（另一个窗口做了同样的修改），记下来，不必再写
    existed_ = true;
    convertedFromUtf8_ = false;
    std::error_code ec;
    stamp_ = fs::last_write_time(path_, ec);
    return true;
}

bool DataFileYD::prepare(const std::string& text) {
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

bool DataFileYD::commit() {
    // 同一磁盘上的替换是一次原子操作：要么还是旧文件，要么已是完整的新文件
    if (!MoveFileExW(tempPath_.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        discard();
        return false;
    }
    snapshot_ = prepared_;
    existed_ = true;
    convertedFromUtf8_ = false;
    std::error_code ec;
    stamp_ = fs::last_write_time(path_, ec);
    return true;
}

void DataFileYD::discard() {
    std::error_code ec;
    fs::remove(tempPath_, ec);
}

bool DataFileYD::saveCopy(const std::string& dir, const std::string& text) const {
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream copy(fs::path(dir) / fileName_, std::ios::binary | std::ios::trunc);
    copy << text;
    copy.flush();
    return static_cast<bool>(copy);
}

bool DataFileYD::copyTo(const std::string& dir) const {
    std::error_code ec;
    if (!fs::exists(path_, ec)) return true;
    fs::create_directories(dir, ec);
    return fs::copy_file(path_, fs::path(dir) / fileName_, fs::copy_options::overwrite_existing, ec);
}

const std::string& DataFileYD::fileName() const { return fileName_; }
