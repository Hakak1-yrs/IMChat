#include "RecordSchema.h"

const RecordSchema* RecordSchema::of(char kind) {
    static const RecordSchema schemas[] = {
        { 'F', 3, true, true, true, { 0, 1 } },     // 对方 申请人 备注
        { 'V', 4, false, false, false, { 2 } },     // 申请序号 A/X 处理人 备注
        { 'D', 2, false, false, false, { 0, 1 } },  // 删除者 被删除者
        { 'B', 3, false, false, false, { 0, 1 } },  // 拉黑者 对方 1/0
        { 'S', 3, false, false, false, { 0 } },     // 账号 verify 1/0
        { 'P', 2, true, true, true, { 0 } },        // 作者 可见范围
        { 'L', 3, false, false, true, { 1 } },      // 动态序号 点赞者 1/0
        { 'K', 3, true, true, true, { 1 } },        // 动态序号 评论者 回复对象
        { 'Y', 1, false, false, false, {} },        // 动态序号
        { 'Z', 1, false, false, false, {} },        // 评论序号
    };
    for (const RecordSchema& s : schemas) {
        if (s.kind == kind) return &s;
    }
    return nullptr;
}
