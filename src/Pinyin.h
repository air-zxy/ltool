#pragma once

#include <QString>

// 搜索框的拼音匹配：输入 renwu 命中"任务/人物"，输入 rw 命中"任务/软件/输入法…"。
// 表在 src/pinyin_data.h（由 tools/genpinyin.py 从 pypinyin 的字典生成，不手抄）。
//
// 只给"显示名"用：应用列表和工具直达那两处。文件索引（270 万条）不做 —— 现算拼音会把
// 20ms 的查询拖到秒级，要做就得给每条路径预建索引。
namespace Pinyin {

enum Kind {
    None = 0,
    Full = 1,       // 全拼：renwu / wu / renwuguanli 这种，命中面窄但准
    Initial = 2,    // 首字母：rw / txhy，命中面广但歧义大
};

// 这条查询值不值得走拼音：必须是纯 ASCII 字母且至少两个字符。
// 单字母不启用是刻意的 —— 中文显示名里本来没有那个字母，开了等于每个中文名都命中。
bool usableQuery(const QString& low);

// 名字以哪种方式命中这条查询。多音字每个读音都算（"重庆" chongqing / zhongqing 都在）。
// 全拼允许中段：搜"任务管理器"打 guanli 也要能出（应用那一档靠它）。
Kind match(const QString& name, const QString& low);

// 同上，但全拼也必须**从开头**命中。工具直达用这个：直达是"整条查询就要这个"的意思，
// 中段命中会把不相干的东西顶上来 —— 实测踩过：搜 renwu 想要任务管理器，结果被
// 「开机启动分析」的关键词"计划任务"（jihua**renwu**）中段命中独占掉，应用那一节整个没了。
Kind matchStart(const QString& name, const QString& low);

// 下面两个只给 --pinyin 自测用：把名字展开成"全拼串"和"首字母串"的所有可能读法
// （多音字取笛卡尔积，上限 64 条，防止五字 × 四读音 = 1024 条那种组合爆炸）。
QStringList fullReadings(const QString& name);
QStringList initialReadings(const QString& name);

} // namespace Pinyin
