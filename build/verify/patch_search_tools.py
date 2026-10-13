import io

P = r"D:\project\cpp\ltool\src\SearchWindow.cpp"
lines = io.open(P, encoding="utf-8").read().split("\n")

# 1 基行号：957..1052 = 「插件那一节」那个 lambda + 整段工具直达，换成一份按插件表匹配的
start, end = 957, 1052
assert lines[start - 1].strip() == "// 插件那一节。定义在这儿、两处都调：工具直达命中时会 return 独占，", repr(lines[start - 1])
assert lines[end - 1] == "    }", repr(lines[end - 1])
assert lines[end].strip() == "", repr(lines[end])
assert lines[end + 1].strip().startswith("// 网站关键字"), repr(lines[end + 1])

new = '''    // 工具直达：卡片墙那些事，搜名字或关键词都能打开。
    // 现在九个自带工具和别人写的插件是同一张表（PluginHost::scan()），这里没有"内置"特判。
    // 两条规矩是从「计划任务抢页」那次教训直接抄过来的：
    // ① 命中只认开头（matchStart）—— 中段撞上是"名字里正好含这几个字母"，
    //    拿它独占整页会把用户要的那个顶掉（搜 renwu 想要任务管理器，却被「开机启动分析」
    //    的关键词"计划任务"当成直达）；
    // ② 只有"整条查询就是这个词"才 return 独占，否则只在末尾追加一节 ——
    //    不然"下载""网络"这种常用词会把应用列表挤掉。
    QVector<int> toolLoose;
    {
        const auto& metas = PluginHost::scan();
        if (low == QStringLiteral("工具") || low == QStringLiteral("tools")
            || low == QStringLiteral("工具箱")) {   // 想看全部就搜"工具"
            addSection(QStringLiteral("工具"));
            for (const int i : std::as_const(toolIndices))
                addToolRow(i);
            rebuildList();
            return;
        }
        QVector<int> exact;
        for (const int i : std::as_const(toolIndices)) {
            const PluginHost::Meta& td = metas[i];
            const QString name = td.name.toLower();
            bool hitExact = (low == name);
            bool hitLoose = low.contains(name);
            for (const QString& kw : td.keywords) {
                const QString k = kw.toLower();
                if (k.isEmpty())
                    continue;
                if (low == k || low.startsWith(k + QLatin1Char(' ')))
                    hitExact = true;
                if (low.contains(k))
                    hitLoose = true;
            }
            // 拼音直达：cipanqingli / cpql 也算"就要这个工具"。工具就卡片墙那几件事，
            // 命中面比应用列表窄得多，所以首字母也当直达（应用那边首字母只排第三档）。
            if (!hitExact && Pinyin::usableQuery(low)) {
                if (Pinyin::matchStart(td.name, low) != Pinyin::None)
                    hitExact = true;
                else
                    for (const QString& kw : td.keywords)
                        if (Pinyin::matchStart(kw, low) != Pinyin::None) {
                            hitExact = true;
                            break;
                        }
            }
            if (hitExact)
                exact << i;
            else if (hitLoose)
                toolLoose << i;
        }
        if (!exact.isEmpty()) {
            addSection(QStringLiteral("工具"));
            for (const int i : std::as_const(exact))
                addToolRow(i);
            rebuildList();
            return;
        }
    }'''

lines[start - 1:end] = new.split("\n")
txt = "\n".join(lines)

# 常规路径末尾那两处调用：addPluginRows() 没了（合并进工具那一节），只留 toolLoose
assert txt.count("    addPluginRows();\n") == 1, txt.count("    addPluginRows();\n")
txt = txt.replace("    addPluginRows();\n", "")
assert "addPluginRows" not in txt

io.open(P, "w", encoding="utf-8", newline="\n").write(txt)
print("ok")
