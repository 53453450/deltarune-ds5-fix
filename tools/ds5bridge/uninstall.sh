#!/usr/bin/env bash
# 把 DELTARUNE.app 还原到注入前状态。
#
# ⚠️ 注意：本仓库的 backup-* 备份目录已按要求删除，因此本脚本**现在通常无法还原**。
#    还原请优先用 Steam：
#      库 → DELTARUNE → 属性 → 已安装文件 → 验证游戏文件完整性
#    （Mac_Runner 与 libYoYoGamepad.dylib 已被改动，哈希不符会被重新下载，含原始签名）
#
# 若只是想临时停用（不改任何文件、重启游戏即失效）：
#      touch /tmp/ds5rawfix.off
#
#   ./uninstall.sh            从 backup-* 还原（若存在）
#   ./uninstall.sh --dry-run  只看会做什么
#   ./uninstall.sh --help     显示本说明

set -euo pipefail

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  sed -n '2,16p' "${BASH_SOURCE[0]}"
  exit 0
fi

APP="${DELTARUNE_APP:-/Users/fterward/Library/Application Support/Steam/steamapps/common/DELTARUNE/DELTARUNE.app}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$(dirname "$HERE")")"   # tools/ds5bridge -> 仓库根

MODE="apply"
[[ "${1:-}" == "--dry-run" ]] && MODE="dryrun"

BK="$(ls -dt "$ROOT"/backup-* 2>/dev/null | head -1 || true)"
if [[ -z "${BK:-}" ]]; then
  cat >&2 <<'EOF'
找不到 backup-* 备份目录，无法用本脚本还原。

请改用 Steam 还原：
  库 → DELTARUNE → 属性 → 已安装文件 → 验证游戏文件完整性

只想临时停用而不还原文件：
  touch /tmp/ds5rawfix.off
EOF
  exit 1
fi

echo "备份目录 : $BK"
echo "目标 app : $APP"
echo

step() {
  if [[ "$MODE" == "dryrun" ]]; then echo "[dry-run] $*"; else echo "  -> $*"; fi
}

[[ -f "$BK/Mac_Runner" ]]           && step "恢复 Contents/MacOS/Mac_Runner"
[[ -f "$BK/libYoYoGamepad.dylib" ]] && step "恢复 Contents/Frameworks/libYoYoGamepad.dylib"
[[ -f "$BK/CodeResources" ]]        && step "恢复 Contents/_CodeSignature/CodeResources"
[[ -f "$BK/Info.plist" ]]           && step "恢复 Contents/Info.plist"
step "删除 Contents/Frameworks/libDS5RawFix.dylib"

[[ "$MODE" == "dryrun" ]] && exit 0

cp -p "$BK/Mac_Runner"           "$APP/Contents/MacOS/Mac_Runner"
cp -p "$BK/libYoYoGamepad.dylib" "$APP/Contents/Frameworks/libYoYoGamepad.dylib"
mkdir -p "$APP/Contents/_CodeSignature"
cp -p "$BK/CodeResources"        "$APP/Contents/_CodeSignature/CodeResources"
cp -p "$BK/Info.plist"           "$APP/Contents/Info.plist"
rm -f "$APP/Contents/Frameworks/libDS5RawFix.dylib"

echo
echo "=== 校验 ==="
codesign --verify --verbose=1 "$APP" 2>&1 | tail -2
codesign -dv "$APP" 2>&1 | grep -E "Authority|TeamIdentifier|flags" | head -3
if otool -L "$APP/Contents/Frameworks/libYoYoGamepad.dylib" | grep -q DS5RawFix; then
  echo "!! 依赖仍在，还原不完全"
else
  echo "依赖已清除 ✓"
fi
