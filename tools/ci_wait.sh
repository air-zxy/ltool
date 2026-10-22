#!/usr/bin/env bash
# ci_wait.sh <run-id> -- 轮询一次 GitHub Actions run 直到终态。
# 为什么要它：判据"绿"要说的是"这一栏量的是哪次 run 的终态"，推上去只算进了流水线。
# token 走 git credential fill，只进 header、不进任何输出。
set -u
RUN="${1:?用法：bash tools/ci_wait.sh <run-id>}"
RUNFILE=$(mktemp)   # 落在临时目录，不进仓库
# 挪进 tools/ 的时候这里的深度要跟着改：以前在 build/verify/ 下是 ../..，现在只有一层。
cd "$(dirname "$0")/.." || exit 1
CRED=$(mktemp)
printf 'protocol=https\nhost=github.com\n\n' | git credential fill >"$CRED" 2>/dev/null
TOK=$(grep -m1 '^password=' "$CRED" | cut -d= -f2)
rm -f "$CRED"
if [ -z "$TOK" ]; then echo "拿不到 token"; exit 2; fi
for i in $(seq 1 40); do
  # 这台机上 GitHub 的 443 时不时回一截被掐断的 JSON（`Expecting ',' delimiter`）——
  # 那是**读失败**，不是"这次 run 没成"。所以量不到就说量不到、下一轮再问，别让轮询自己崩掉。
  curl -s -m 25 -H "Authorization: Bearer $TOK" \
       "https://api.github.com/repos/mateo-ki/ltool/actions/runs/$RUN" -o "$RUNFILE" 2>/dev/null
  line=$(python -c "
import json,sys
try:
    d=json.load(open(sys.argv[1],encoding='utf-8'))
    print(d.get('status'), d.get('conclusion'))
except Exception as e:
    print('unreadable', repr(e)[:60])
" "$RUNFILE" 2>/dev/null)
  echo "poll$i: $line"
  case "$line" in completed*) rm -f "$RUNFILE"; exit 0;; esac
  sleep 45
done
rm -f "$RUNFILE"
echo "poll: 到点也没终态（去 Actions 页面看，别当成功）"
