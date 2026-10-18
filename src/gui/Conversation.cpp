#include "Conversation.h"

Conversation Conversation::privateChat(const std::string& peerId) {
    Conversation c;
    c.peerId = peerId;
    return c;
}

Conversation Conversation::groupChat(int groupNo) {
    Conversation c;
    c.isGroup = true;
    c.groupNo = groupNo;
    return c;
}

std::string Conversation::key() const { return isGroup ? "G:" + std::to_string(groupNo) : "P:" + peerId; }

bool Conversation::sameAs(const Conversation& other) const { return key() == other.key(); }
