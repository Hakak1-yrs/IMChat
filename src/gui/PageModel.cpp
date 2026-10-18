#include "PageModel.h"

PageModel::Button PageModel::button(const std::wstring& text, int action, const std::string& arg, Style style) {
    return { text, action, arg, style };
}

void PageModel::section(const std::wstring& heading) {
    Row row;
    row.kind = SECTION;
    row.title = heading;
    rows.push_back(row);
}

PageModel::Row& PageModel::info(const std::wstring& label, const std::wstring& value) {
    Row row;
    row.kind = INFO;
    row.title = label;
    row.value = value;
    rows.push_back(row);
    return rows.back();
}

void PageModel::text(const std::wstring& text) {
    Row row;
    row.kind = TEXT;
    row.title = text;
    rows.push_back(row);
}

PageModel::Row& PageModel::person(const std::string& avatarKey, const std::wstring& avatarName,
                                  const std::wstring& name, const std::wstring& detail, bool square) {
    Row row;
    row.kind = PERSON;
    row.avatarKey = avatarKey;
    row.avatarName = avatarName;
    row.title = name;
    row.value = detail;
    row.square = square;
    rows.push_back(row);
    return rows.back();
}

void PageModel::actions(const std::vector<Button>& buttons) {
    Row row;
    row.kind = ACTIONS;
    row.buttons = buttons;
    rows.push_back(row);
}

void PageModel::note(const std::wstring& text) {
    Row row;
    row.kind = NOTE;
    row.title = text;
    rows.push_back(row);
}

void PageModel::chat(const std::wstring& text, ChatKind kind) {
    Row row;
    row.kind = CHAT;
    row.title = text;
    row.chatKind = kind;
    rows.push_back(row);
}
