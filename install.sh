#!/usr/bin/env bash
# 把 DualSense 映射安装到 DELTARUNE 的存档目录（GameMaker 运行时第一优先查找位置）。
#
#   ./install.sh              安装
#   ./install.sh --uninstall  回滚
#   ./install.sh --dry-run    只看会做什么
#
# 不会触碰 DELTARUNE.app —— 不改 bundle、不破坏代码签名。

set -euo pipefail

BUNDLE_ID="com.tobyfox.deltarune"
DEST_DIR="$HOME/Library/Application Support/$BUNDLE_ID"
DEST="$DEST_DIR/gamecontrollerdb.txt"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/gamecontrollerdb.txt"

MODE="install"
for arg in "$@"; do
  case "$arg" in
    --uninstall) MODE="uninstall" ;;
    --dry-run)   MODE="dryrun" ;;
    -h|--help)   sed -n '2,9p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "未知参数: $arg" >&2; exit 2 ;;
  esac
done

echo "存档目录 : $DEST_DIR"
echo "目标文件 : $DEST"

case "$MODE" in
  uninstall)
    if [[ -f "$DEST" ]]; then
      rm -f "$DEST"
      echo "已删除，已回滚到原状。"
    else
      echo "文件不存在，无需回滚。"
    fi
    exit 0
    ;;
  dryrun)
    [[ -d "$DEST_DIR" ]] || echo "[dry-run] 将创建目录 $DEST_DIR"
    echo "[dry-run] 将复制 $SRC -> $DEST"
    exit 0
    ;;
esac

[[ -f "$SRC" ]] || { echo "找不到源文件: $SRC" >&2; exit 1; }
[[ -d "$DEST_DIR" ]] || mkdir -p "$DEST_DIR"

if [[ -f "$DEST" ]]; then
  backup="$DEST.bak.$(date +%Y%m%d%H%M%S)"
  cp "$DEST" "$backup"
  echo "已备份原文件 -> $backup"
fi

cp "$SRC" "$DEST"

echo "已安装。生效条目："
grep -v '^#' "$DEST" | grep -v '^$' | cut -d, -f1,2 | sed 's/^/  /'
echo
echo "现在启动游戏即可验证：设置页按 ✕ 应为确认、◯ 应为取消。"
