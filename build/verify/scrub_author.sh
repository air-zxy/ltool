#!/bin/sh
# 把公开历史里那条 QQ 号邮箱换成 GitHub 的 noreply 地址（他 2026-10-15 明确选了"改写历史 + 强推清掉它"）。
#
# 只改**元数据**，不动一个字节的内容：查过了 ——
#   git log -S "1216915972" --all     → 空（那串数字从来不在任何 blob 里）
#   grep -rn "1216915972" 工作树        → 空
# 它只在 32 条提交的 author/committer 字段上。GitHub 自己那条合并提交（noreply@github.com）原样留着。
#
# 这个脚本**不推送**：它只改本地那份 C:/Users/loukas/ltool-work/repo，
# 改完打印要按的那条命令（带 --force-with-lease，不是裸 --force）。
set -e
REPO="/c/Users/loukas/ltool-work/repo"
OLD_EMAIL="1216915972@qq.com"
NEW_NAME="mateo-ki"
NEW_EMAIL="52289949+mateo-ki@users.noreply.github.com"

cd "$REPO"
BEFORE=$(git rev-parse HEAD)
git tag -f pre-scrub "$BEFORE"
echo "改写前：$(git rev-list --count HEAD) 条提交，HEAD=$BEFORE（已记在本地 tag pre-scrub，可回退）"
git log --format='%an|%ae' | sort | uniq -c | sort -rn
echo

# filter-branch 是自带工具，不依赖 pip（本机 mingw python 的 pip 是坏的：SRE module mismatch）
FILTER_BRANCH_SQUELCH_WARNING=1 git filter-branch -f --env-filter "
if [ \"\$GIT_AUTHOR_EMAIL\" = \"$OLD_EMAIL\" ]; then
    export GIT_AUTHOR_NAME=\"$NEW_NAME\"
    export GIT_AUTHOR_EMAIL=\"$NEW_EMAIL\"
fi
if [ \"\$GIT_COMMITTER_EMAIL\" = \"$OLD_EMAIL\" ]; then
    export GIT_COMMITTER_NAME=\"$NEW_NAME\"
    export GIT_COMMITTER_EMAIL=\"$NEW_EMAIL\"
fi
" --tag-name-filter cat -- --branches --tags

echo
echo "改写后：$(git rev-list --count HEAD) 条提交"
git log --format='%an|%ae' | sort | uniq -c | sort -rn
LEFT=$(git log --format='%ae %ce' | grep -c "$OLD_EMAIL" || true)
echo "历史里还剩几条带那串邮箱：$LEFT（期望 0）"
# 内容零差异：改写前后 HEAD 的树必须一模一样
if git diff --quiet "$BEFORE" HEAD; then
    echo "文件内容：零差异 ✔（只动了署名）"
else
    echo "!! 文件内容有差异 —— 别推，先查"
    git diff --stat "$BEFORE" HEAD | tail -5
    exit 1
fi
echo
echo "要推就自己按这条（远端 main 会被改写后的历史顶掉；回退用本地 tag pre-scrub）："
echo "  git -C \"$REPO\" push --force-with-lease=main:$(git rev-parse origin/main) origin main"
