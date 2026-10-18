#include "Moment.h"
#include <algorithm>

std::string Moment::scopeText(const std::string& scope) {
    if (scope == "self") return "仅自己可见";
    if (scope.compare(0, 5, "only:") == 0) return "部分可见";
    if (scope.compare(0, 7, "except:") == 0) return "不给谁看";
    return "公开";
}

bool Moment::likedBy(const std::string& id) const { return std::find(likes.begin(), likes.end(), id) != likes.end(); }
