#include "MessageYD.h"

MessageYD::MessageYD(int seq, ServiceTypeYD service, bool isGroup, const std::string& target, const std::string& sender,
                     const std::string& time, const std::string& text)
    : seq_(seq), service_(service), isGroup_(isGroup), target_(target), sender_(sender), time_(time), text_(text) {}

int MessageYD::seq() const { return seq_; }

ServiceTypeYD MessageYD::service() const { return service_; }

bool MessageYD::isGroup() const { return isGroup_; }

const std::string& MessageYD::target() const { return target_; }

const std::string& MessageYD::sender() const { return sender_; }

const std::string& MessageYD::time() const { return time_; }

const std::string& MessageYD::text() const { return text_; }

bool MessageYD::isSystem() const { return sender_ == systemSender(); }

bool MessageYD::revoked() const { return !revokedBy_.empty(); }

const std::string& MessageYD::revokedBy() const { return revokedBy_; }

void MessageYD::markRevoked(const std::string& by) { revokedBy_ = by; }

const char* MessageYD::systemSender() { return "#"; }
