#include "MessageType.h"

const char* messageTypeName(MessageType type) {
    switch (type) {
    case MessageType::PING: return "PING";
    case MessageType::PONG: return "PONG";
    case MessageType::LOGIN_REQ: return "LOGIN_REQ";
    case MessageType::LOGIN_RESP: return "LOGIN_RESP";
    case MessageType::KICKED: return "KICKED";
    case MessageType::SEND_MSG_REQ: return "SEND_MSG_REQ";
    case MessageType::SEND_MSG_ACK: return "SEND_MSG_ACK";
    case MessageType::MSG_PUSH: return "MSG_PUSH";
    case MessageType::ERROR_RESP: return "ERROR_RESP";
    }
    return "UNKNOWN";
}
