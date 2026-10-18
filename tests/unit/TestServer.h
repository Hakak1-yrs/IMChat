#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include "ChatServer.h"
#include "LoopThread.h"
#include "Platform.h"

// 集成测试用的服务端：在后台线程里运行 ChatServer，数据放在独立的临时目录里。
// 销毁后可以用同一个数据目录、同一个端口再建一个，模拟服务端重启
class TestServer {
public:
    TestServer(std::string dataDir, ChatServer::Options options) : dataDir_(std::move(dataDir)) {
        options.ip = "127.0.0.1";
        if (options.tokenSecret.empty()) options.tokenSecret = "unit-test-token-secret-0123456789abcdef";   // 重启后令牌仍然有效
        // 设置了 IMCHAT_TEST_IO_THREADS 时，全部集成测试改在多线程模式下再跑一遍（见 CMakeLists.txt）
        const std::string threads = environment("IMCHAT_TEST_IO_THREADS");
        if (!threads.empty()) options.ioThreads = std::atoi(threads.c_str());
        thread_ = std::make_unique<LoopThread>(
            [this, options](EventLoop& loop) {
                platform_ = std::make_unique<Platform>(dataDir_);
                server_ = std::make_unique<ChatServer>(loop, *platform_, options);
                std::string error;
                if (!server_->start(error)) throw std::runtime_error(error);
                port_ = server_->port();
            },
            [this] {
                server_.reset();
                platform_.reset();   // 析构时写回数据文件
            });
    }

    ~TestServer() { thread_.reset(); }   // 先停事件循环（在那个线程里销毁服务端），再释放其他成员

    std::uint16_t port() const { return port_; }
    std::size_t onlineCount() { return thread_->call([this] { return server_->onlineCount(); }); }

    // 每个测试一份全新的演示数据，放在系统临时目录下（用户名含中文时，路径也含中文）
    static std::string freshDataDir(const std::string& name) {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "imchat_unit_tests" / name;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        for (const auto& entry : fs::directory_iterator(fs::u8path(IMCHAT_SOURCE_DIR) / "data_demo"))
            fs::copy_file(entry.path(), dir / entry.path().filename());
        return dir.u8string() + "/";
    }

    static std::string environment(const char* name) {
#ifdef _MSC_VER
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, name) != 0 || !value) return "";
        std::string result = value;
        std::free(value);
        return result;
#else
        const char* value = std::getenv(name);
        return value ? value : "";
#endif
    }

    static std::string readText(const std::string& utf8Path) {
        std::ifstream in(std::filesystem::u8path(utf8Path), std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

private:
    std::string dataDir_;
    std::unique_ptr<Platform> platform_;
    std::unique_ptr<ChatServer> server_;
    std::unique_ptr<LoopThread> thread_;
    std::uint16_t port_ = 0;
};
