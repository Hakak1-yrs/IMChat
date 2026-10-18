#include "PageYD.h"
#include "GuiWindowYD.h"
#include "MainViewYD.h"

PageYD::PageYD(GuiWindowYD& window) : window_(window) {}

void PageYD::onCompose(const std::string&) {}

bool PageYD::poll() { return false; }

std::string PageYD::key() const { return ""; }

SessionYD& PageYD::session() { return window_.session(); }

PlatformYD& PageYD::platform() { return window_.platform(); }

ServiceBaseYD& PageYD::me() { return *window_.session().current(); }

UserYD& PageYD::user() { return *window_.session().user(); }

MainViewYD& PageYD::main() { return window_.mainView(); }

void PageYD::toast(const std::wstring& text) { window_.mainView().toast(text); }

void PageYD::refresh() {
    window_.mainView().reload();
    window_.invalidate();
}
