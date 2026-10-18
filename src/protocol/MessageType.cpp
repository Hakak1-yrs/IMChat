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
    case MessageType::SYNC_REQ: return "SYNC_REQ";
    case MessageType::SYNC_RESP: return "SYNC_RESP";
    case MessageType::READ_REQ: return "READ_REQ";
    case MessageType::READ_RESP: return "READ_RESP";
    case MessageType::READ_RECEIPT: return "READ_RECEIPT";
    case MessageType::CALL_REQ: return "CALL_REQ";
    case MessageType::CALL_RESP: return "CALL_RESP";
    case MessageType::PRESENCE: return "PRESENCE";
    case MessageType::MSG_REVOKED: return "MSG_REVOKED";
    case MessageType::ERROR_RESP: return "ERROR_RESP";
    }
    return "UNKNOWN";
}
