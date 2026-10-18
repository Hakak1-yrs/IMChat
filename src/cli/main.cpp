// IMChat 命令行客户端：通过网络连接 IMChatServer 收发消息。
// 用法：IMChatCli [--host 127.0.0.1] [--port 9000] [服务 账号 密码]
#include "ChatClient.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

namespace {

std::mutex g_outputMutex;   // 网络线程和输入线程都会输出，加锁避免两行交错

void print(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_outputMutex);
    std::cout << line << std::endl;
}

#ifdef _WIN32
std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return "";
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &result[0], size, nullptr, nullptr);
    return result;
}
#endif

// 读一行 UTF-8 输入。Windows 控制台要用 ReadConsoleW 才能正确读到中文；输入被重定向时照常用 getline
bool readLine(std::string& line) {
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(input, &mode)) {
        std::wstring text;
        wchar_t buffer[256];
        while (text.empty() || text.back() != L'\n') {
            DWORD count = 0;
            if (!ReadConsoleW(input, buffer, 256, &count, nullptr) || count == 0) return false;
            text.append(buffer, count);
        }
        while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) text.pop_back();
        line = toUtf8(text);
        return line.empty() || line[0] != 0x1A;   // Ctrl+Z：输入结束
    }
#endif
    if (!std::getline(std::cin, line)) return false;
    if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
    return true;
}

std::string shortTime(const std::string& time) { return time.size() >= 16 ? time.substr(11, 5) : time; }

// 会话标识：私信用对方账号，群用 "g" + 群号
std::string chatKey(const json& message, const std::string& me) {
    if (message.contains("groupNo")) return "g" + std::to_string(message.value("groupNo", 0));
    const std::string from = message.value("from", "");
    return from == me ? message.value("to", "") : from;
}

std::string formatMessage(const json& m, const std::string& me, bool history) {
    std::ostringstream out;
    out << (history ? "  [历史] " : "  ") << "[" << shortTime(m.value("time", "")) << "] ";
    if (m.contains("groupNo")) out << "[群 " << m.value("groupNo", 0) << "] ";
    const std::string from = m.value("from", "");
    if (m.value("system", false)) out << "—— " << m.value("text", "") << " ——";
    else if (m.value("revoked", false)) out << from << " 撤回了一条消息";
    else if (from == me) out << "我 → " << (m.contains("groupNo") ? "群" : m.value("to", "")) << "：" << m.value("text", "");
    else out << from << "：" << m.value("text", "");
    out << "   #" << m.value("seq", 0);
    return out.str();
}

void printHelp() {
    print("命令：\n"
          "  /login 服务 账号 密码    登录（服务：QQ / WECHAT / WEIBO / WEISHANG）\n"
          "  /to 账号 内容            发私信\n"
          "  /g 群号 内容             发群消息\n"
          "  直接输入内容             发给上一次 /to 或 /g 的对象\n"
          "  /read                    把当前会话标记为已读（对方会收到已读回执）\n"
          "  /status                  连接状态、待确认的消息数\n"
          "  /stats                   服务端运行统计（连接数、消息数、被限流的消息数……）\n"
          "  /quit                    退出");
}

const char* stateName(ChatClient::State state) {
    switch (state) {
    case ChatClient::State::DISCONNECTED: return "未连接";
    case ChatClient::State::CONNECTING: return "正在连接";
    case ChatClient::State::CONNECTED: return "已连接，未登录";
    case ChatClient::State::LOGGED_IN: return "已登录";
    }
    return "?";
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
    std::vector<std::string> loginArgs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        else loginArgs.push_back(arg);
    }

    std::mutex stateMutex;   // 保护下面三个由网络线程更新、输入线程读取的变量
    std::string me;
    std::map<std::string, int> lastSeqOf;   // 会话 → 收到的最后一条消息的序号（/read 使用）
    std::string currentChat;                // 当前会话：账号，或 "g" + 群号

    ChatClient::Callbacks callbacks;
    callbacks.onState = [](ChatClient::State state) { print(std::string("* ") + stateName(state)); };
    callbacks.onLogin = [&](bool ok, const json& response) {
        if (!ok) {
            print("* 登录失败：" + response.value("error", ""));
            return;
        }
        print("* 欢迎，" + response.value("nickname", "") + "。未读私信 " + std::to_string(response.value("unreadPrivate", 0)) +
              " 条，未读群消息 " + std::to_string(response.value("unreadGroup", 0)) + " 条");
    };
    callbacks.onMessage = [&](const json& message, bool history) {
        std::string self;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            self = me;
            int& last = lastSeqOf[chatKey(message, me)];
            last = std::max(last, message.value("seq", 0));
        }
        print(formatMessage(message, self, history));
    };
    callbacks.onAck = [](const json& ack) {
        if (ack.value("ok", false)) print("  ✓ 已送达 #" + std::to_string(ack.value("seq", 0)));
        else print("  ✗ 发送失败：" + ack.value("error", ""));
    };
    callbacks.onReadReceipt = [](const json& receipt) {
        print("  ✓✓ " + receipt.value("reader", "") + " 已读到 #" + std::to_string(receipt.value("seq", 0)));
    };
    callbacks.onKicked = [](const std::string& reason) { print("* 已下线：" + reason); };
    callbacks.onError = [](const json& error) { print("* 服务端错误：" + error.value("message", "")); };

    ChatClient client(callbacks);
    print("IMChat 命令行客户端，连接 " + host + ":" + std::to_string(port) + "。输入 /help 查看命令。");
    client.start(host, port);
    auto doLogin = [&](const std::string& service, const std::string& account, const std::string& password) {
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            me = account;
        }
        client.login(service, account, password);
    };
    if (loginArgs.size() == 3) doLogin(loginArgs[0], loginArgs[1], loginArgs[2]);

    std::string line;
    while (readLine(line)) {
        if (line.empty()) continue;
        std::istringstream in(line);
        std::string command;
        in >> command;
        auto rest = [&in] {
            std::string text;
            std::getline(in >> std::ws, text);
            return text;
        };
        if (command == "/quit") break;
        if (command == "/help") {
            printHelp();
        } else if (command == "/login") {
            std::string service, account, password;
            in >> service >> account >> password;
            doLogin(service, account, password);
        } else if (command == "/to" || command == "/g") {
            std::string target;
            in >> target;
            const std::string text = rest();
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                currentChat = command == "/g" ? "g" + target : target;
            }
            if (text.empty()) print("* 当前会话：" + target);
            else if (command == "/g") client.sendGroupText(std::atoi(target.c_str()), text);
            else client.sendText(target, text);
        } else if (command == "/read") {
            std::lock_guard<std::mutex> lock(stateMutex);
            const int seq = lastSeqOf[currentChat];
            if (currentChat.empty() || seq == 0) print("* 当前会话没有可以标记的消息");
            else if (currentChat[0] == 'g') client.markGroupRead(std::atoi(currentChat.c_str() + 1), seq);
            else client.markRead(currentChat, seq);
        } else if (command == "/status") {
            print(std::string("* ") + stateName(client.state()) + "，待确认的消息 " + std::to_string(client.pendingCount()) +
                  " 条，最新消息 #" + std::to_string(client.lastSeq()));
        } else if (command == "/stats") {
            client.call("server.stats", json::object(), [](bool ok, const json& s, const std::string& error) {
                if (!ok) {
                    print("* 查询失败：" + error);
                    return;
                }
                print("* 服务端：已运行 " + std::to_string(s.value("uptimeSeconds", 0LL)) + " 秒，I/O 线程 " +
                      std::to_string(s.value("ioThreads", 0)) + " 个，连接 " + std::to_string(s.value("connections", 0)) + "，在线 " +
                      std::to_string(s.value("online", 0)) + "；累计登录 " + std::to_string(s.value("logins", 0LL)) + " 次，消息 " +
                      std::to_string(s.value("messages", 0LL)) + " 条，推送 " + std::to_string(s.value("pushes", 0LL)) + " 条，因发送过快被拒 " +
                      std::to_string(s.value("rateLimited", 0LL)) + " 条");
            });
        } else if (command[0] == '/') {
            print("* 未知命令，输入 /help 查看命令");
        } else {
            std::string chat;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                chat = currentChat;
            }
            if (chat.empty()) print("* 先用 /to 账号 或 /g 群号 选择会话");
            else if (chat[0] == 'g') client.sendGroupText(std::atoi(chat.c_str() + 1), line);
            else client.sendText(chat, line);
        }
    }
    client.stop();
    return 0;
}
