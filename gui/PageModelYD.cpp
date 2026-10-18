#include "PageModelYD.h"

PageModelYD::ButtonYD PageModelYD::button(const std::wstring& text, int action, const std::string& arg, StyleYD style) {
    return { text, action, arg, style };
}

void PageModelYD::section(const std::wstring& title) {
    RowYD row;
    row.kind = SECTION;
    row.title = title;
    rows.push_back(row);
}

PageModelYD::RowYD& PageModelYD::info(const std::wstring& label, const std::wstring& value) {
    RowYD row;
    row.kind = INFO;
    row.title = label;
    row.value = value;
    rows.push_back(row);
    return rows.back();
}

void PageModelYD::text(const std::wstring& text) {
    RowYD row;
    row.kind = TEXT;
    row.title = text;
    rows.push_back(row);
}

PageModelYD::RowYD& PageModelYD::person(const std::string& avatarKey, const std::wstring& avatarName,
                                        const std::wstring& title, const std::wstring& subtitle, bool square) {
    RowYD row;
    row.kind = PERSON;
    row.avatarKey = avatarKey;
    row.avatarName = avatarName;
    row.title = title;
    row.value = subtitle;
    row.square = square;
    rows.push_back(row);
    return rows.back();
}

void PageModelYD::actions(const std::vector<ButtonYD>& buttons) {
    RowYD row;
    row.kind = ACTIONS;
    row.buttons = buttons;
    rows.push_back(row);
}

void PageModelYD::note(const std::wstring& text) {
    RowYD row;
    row.kind = NOTE;
    row.title = text;
    rows.push_back(row);
}

void PageModelYD::chat(const std::wstring& text, ChatKindYD kind) {
    RowYD row;
    row.kind = CHAT;
    row.title = text;
    row.chatKind = kind;
    rows.push_back(row);
}
