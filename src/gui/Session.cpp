#include "Session.h"

std::vector<PendingView> Session::pending(const Conversation&) const { return {}; }

std::string Session::takeNotice() { return ""; }

std::string Session::status() const { return ""; }

int Session::pendingFriendRequests() const { return 0; }

int Session::momentNotices() const { return 0; }
