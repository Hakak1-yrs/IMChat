#include "Message.h"

Message::Message(int seq, ServiceType service, bool isGroup, const std::string& target, const std::string& sender,
                     const std::string& time, const std::string& text)
    : seq_(seq), service_(service), isGroup_(isGroup), target_(target), sender_(sender), time_(time), text_(text) {}

int Message::seq() const { return seq_; }

ServiceType Message::service() const { return service_; }

bool Message::isGroup() const { return isGroup_; }

const std::string& Message::target() const { return target_; }

const std::string& Message::sender() const { return sender_; }

const std::string& Message::time() const { return time_; }

const std::string& Message::text() const { return text_; }

bool Message::isSystem() const { return sender_ == systemSender(); }

bool Message::revoked() const { return !revokedBy_.empty(); }

const std::string& Message::revokedBy() const { return revokedBy_; }

void Message::markRevoked(const std::string& by) { revokedBy_ = by; }

const char* Message::systemSender() { return "#"; }
