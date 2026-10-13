import io

targets = {
    "FileIndexer.h": "class FileIndexer : public QObject {",
    "InfoRow.h": "class InfoRow : public QWidget {",
    "InstallLogDialog.h": "class InstallLogDialog : public QDialog {",
    "RecordUi.h": "class WindowPicker : public QWidget {",
    "SearchWindow.h": "class SearchWindow : public QWidget {",
    "SettingsDialog.h": "class SettingsDialog : public QDialog {",
}
base = r"D:\project\cpp\ltool\src"

for fn, decl in targets.items():
    p = base + "\\" + fn
    txt = io.open(p, encoding="utf-8").read()
    assert txt.count(decl) == 1, (fn, txt.count(decl))
    cls = decl.split()[1]
    new_decl = "class LTOOL_CORE_API %s%s" % (cls, decl[len("class " + cls):])
    txt = txt.replace(decl, new_decl)
    assert new_decl in txt and decl not in txt, fn
    if '#include "core-api.h"' not in txt:
        lines = txt.split("\n")
        assert lines[0].strip() == "#pragma once", (fn, lines[0])
        lines.insert(1, '#include "core-api.h"')
        txt = "\n".join(lines)
    io.open(p, "w", encoding="utf-8", newline="\n").write(txt)
    print("patched", fn)
