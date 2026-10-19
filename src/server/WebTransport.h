#pragma once
#include <cstddef>
#include <string>
#include "TcpConnection.h"

// 浏览器接入（手机等没有原生客户端的设备）。同一个端口上：
// - 原生客户端直接发协议帧。帧的第一个字节是长度的最高字节，长度不超过 1 MiB 多一点，这个字节总是 0；
// - 浏览器发来的是 HTTP 请求（"GET ..."）：要网页就回复内置的网页客户端（web/index.html，编译时嵌进程序里），
//   要求升级就切换成 WebSocket，之后每条 WebSocket 消息里装一帧协议帧。
// WebSocket 只是外面多包的一层：拆开后得到的字节流和原生连接的完全一样，交给同一套切帧、分发代码，
// 业务处理不知道、也不关心对方是哪种客户端。
// 每条连接的状态存放在 TcpConnection::context 里，所有函数都只能在连接所属的 I/O 线程里调用
class WebTransport {
public:
    // 处理收到的数据，返回协议帧字节流所在的缓冲区（原生连接就是 input 本身）。
    // 返回 nullptr 表示没有帧要处理：HTTP 请求还没收全、已经回复了网页、或者对方违反协议、连接正在关闭
    static Buffer* unwrap(const TcpConnection::Ptr& connection, Buffer& input);
    // 发送一帧协议帧：WebSocket 连接装进一条二进制消息，原生连接原样发送
    static void send(const TcpConnection::Ptr& connection, std::string bytes);
    static void send(const TcpConnection::Ptr& connection, const char* data, std::size_t size);
};
