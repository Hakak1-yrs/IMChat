#include "MomentComment.h"

bool MomentComment::isReply() const { return !replyTo.empty() && replyTo != "-"; }
