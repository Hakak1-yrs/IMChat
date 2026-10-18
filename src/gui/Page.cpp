#include "Page.h"
#include "GuiWindow.h"
#include "MainView.h"

Page::Page(GuiWindow& window) : window_(window) {}

void Page::onCompose(const std::string&) {}

bool Page::poll() { return false; }

std::string Page::key() const { return ""; }

LocalSession& Page::session() { return window_.localSession(); }

Platform& Page::platform() { return window_.platform(); }

ServiceBase& Page::me() { return *window_.localSession().current(); }

User& Page::user() { return *window_.localSession().user(); }

MainView& Page::main() { return window_.mainView(); }

void Page::toast(const std::wstring& text) { window_.mainView().toast(text); }

void Page::refresh() {
    window_.mainView().reload();
    window_.invalidate();
}
