#include "Page.h"
#include "GuiWindow.h"
#include "GuiText.h"
#include "MainView.h"

Page::Page(GuiWindow& window) : window_(window) {}

void Page::onCompose(const std::string&) {}

bool Page::poll() { return false; }

std::string Page::key() const { return ""; }

Session& Page::session() { return window_.session(); }

LocalSession& Page::local() { return window_.localSession(); }

std::function<void(bool, const std::string&)> Page::report() {
    GuiWindow& window = window_;
    return [&window](bool, const std::string& message) {
        if (!message.empty()) window.mainView().toast(GuiText::wide(message));
        window.mainView().reload();
        window.invalidate();
    };
}

Platform& Page::platform() { return window_.platform(); }

ServiceBase& Page::me() { return *window_.localSession().current(); }

User& Page::user() { return *window_.localSession().user(); }

MainView& Page::main() { return window_.mainView(); }

void Page::toast(const std::wstring& text) { window_.mainView().toast(text); }

void Page::refresh() {
    window_.mainView().reload();
    window_.invalidate();
}
