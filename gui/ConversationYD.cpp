#include "ConversationYD.h"

ConversationYD ConversationYD::privateChat(const std::string& peerId) {
    ConversationYD c;
    c.peerId = peerId;
    return c;
}

ConversationYD ConversationYD::groupChat(int groupNo) {
    ConversationYD c;
    c.isGroup = true;
    c.groupNo = groupNo;
    return c;
}

std::string ConversationYD::key() const { return isGroup ? "G:" + std::to_string(groupNo) : "P:" + peerId; }

bool ConversationYD::sameAs(const ConversationYD& other) const { return key() == other.key(); }
