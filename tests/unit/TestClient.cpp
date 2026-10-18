#include "TestClient.h"
#include <chrono>

#ifndef _WIN32
#include <sys/select.h>
#endif

TestClient::TestClient(std::uint16_t port) : socket_(SocketOps::connectTcp("127.0.0.1", port)) {}

TestClient::~TestClient() { SocketOps::close(socket_); }

bool TestClient::connected() const { return socket_ != kInvalidSocket; }

void TestClient::sendRaw(const std::string& bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const long n = SocketOps::sendSome(socket_, bytes.data() + sent, bytes.size() - sent);
        if (n <= 0) return;
        sent += static_cast<std::size_t>(n);
    }
}

void TestClient::send(MessageType type, const nlohmann::json& body) {
    Packet packet;
    packet.type = type;
    packet.body = body;
    sendRaw(packet.encode());
}

std::optional<Packet> TestClient::receive(int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        Frame frame;
        std::size_t consumed = 0;
        if (Frame::decode(buffer_.data(), buffer_.size(), frame, consumed) == Frame::DecodeResult::OK) {
            buffer_.erase(0, consumed);
            return Packet::fromFrame(frame);
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (peerClosed_ || left.count() <= 0 || !waitReadable(static_cast<int>(left.count()))) return std::nullopt;
        char chunk[4096];
        const long n = SocketOps::recvSome(socket_, chunk, sizeof(chunk));
        if (n <= 0) {
            peerClosed_ = true;
            return std::nullopt;
        }
        buffer_.append(chunk, static_cast<std::size_t>(n));
    }
}

bool TestClient::closedByPeer(int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!peerClosed_) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0 || !waitReadable(static_cast<int>(left.count()))) return false;
        char chunk[4096];
        if (SocketOps::recvSome(socket_, chunk, sizeof(chunk)) <= 0) peerClosed_ = true;
    }
    return true;
}

nlohmann::json TestClient::login(const std::string& service, const std::string& account, const std::string& password) {
    send(MessageType::LOGIN_REQ, { { "reqId", 1 }, { "service", service }, { "account", account }, { "password", password } });
    std::optional<Packet> response = receive();
    return response && response->type == MessageType::LOGIN_RESP ? response->body : nlohmann::json();
}

bool TestClient::waitReadable(int timeoutMs) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket_, &set);
    timeval timeout{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    return select(static_cast<int>(socket_ + 1), &set, nullptr, nullptr, &timeout) > 0;
}
