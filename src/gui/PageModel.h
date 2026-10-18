#pragma once
#include <string>
#include <vector>

// 页面内容：功能页（个人中心、好友资料、群设置、全部群、点对点聊天……）先把要显示的内容按行描述出来，
// 再由 PagePanel 统一绘制。这样每个功能页只关心“显示什么、点了按钮做什么”，所有页面的样式保持一致
class PageModel {
public:
    enum Kind {
        SECTION,   // 小标题
        INFO,      // 一行“名称：内容”，右侧可以有按钮
        TEXT,      // 一段文字（自动换行）
        PERSON,    // 带头像的一行（好友、群成员、群……），可以有标签和按钮
        ACTIONS,   // 一排按钮
        NOTE,      // 灰色的提示文字
        CHAT       // 聊天消息（TCP 聊天页使用）
    };
    enum Style { NORMAL, PRIMARY, DANGER };
    enum ChatKind { CHAT_OTHER, CHAT_MINE, CHAT_SYSTEM };

    struct Button {
        std::wstring text;
        int action;
        std::string arg;   // 动作的参数，如账号、群号
        Style style;
    };
    struct Tag {
        std::wstring text;
        unsigned color;
    };
    struct Row {
        Kind kind;
        std::wstring title;          // 名称 / 文字内容
        std::wstring value;          // 内容 / 副标题
        std::string avatarKey;       // 头像颜色依据
        std::wstring avatarName;     // 头像上的字取自它
        bool square = false;         // 方形头像（群）
        std::vector<Tag> tags;
        std::vector<Button> buttons;
        int chatKind = CHAT_OTHER;
    };

    static Button button(const std::wstring& text, int action, const std::string& arg = "", Style style = NORMAL);

    void section(const std::wstring& heading);
    Row& info(const std::wstring& label, const std::wstring& value);
    void text(const std::wstring& text);
    Row& person(const std::string& avatarKey, const std::wstring& avatarName, const std::wstring& name,
                const std::wstring& detail, bool square = false);
    void actions(const std::vector<Button>& buttons);
    void note(const std::wstring& text);
    void chat(const std::wstring& text, ChatKind kind);

    std::wstring title;
    std::wstring subtitle;
    std::vector<Button> headerButtons;   // 标题栏右侧的按钮
    std::vector<Row> rows;
    bool composer = false;                 // 底部是否显示输入框（聊天页）
    bool stickBottom = false;              // 内容增加时自动滚到最底部（聊天页）
};
