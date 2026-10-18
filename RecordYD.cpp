#include "RecordYD.h"

const std::string& RecordYD::field(size_t i) const {
    static const std::string empty;
    return i < fields.size() ? fields[i] : empty;
}
