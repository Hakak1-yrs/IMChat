#include "MomentCommentYD.h"

bool MomentCommentYD::isReply() const { return !replyTo.empty() && replyTo != "-"; }
