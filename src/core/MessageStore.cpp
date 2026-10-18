#include "MessageStore.h"

// 已读位置的键：私信是“对方 + 读者”，群是“群号 + 读者”
void MessageStore::markPrivateRead(ServiceType service, const std::string& me, const std::string& peer, int seq) {
    markRead(service, "P", peer, me, seq);
}

void MessageStore::markGroupRead(ServiceType service, int groupNo, const std::string& me, int seq) {
    markRead(service, "G", std::to_string(groupNo), me, seq);
}

int MessageStore::groupReadMark(ServiceType service, int groupNo, const std::string& me) const {
    return readMark(service, "G", std::to_string(groupNo), me);
}

bool MessageStore::setCommitScheduler(Scheduler) { return false; }

void MessageStore::commitPending() {}
