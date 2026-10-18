#include "Record.h"

const std::string& Record::field(size_t i) const {
    static const std::string empty;
    return i < fields.size() ? fields[i] : empty;
}
