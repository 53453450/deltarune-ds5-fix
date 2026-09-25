#!/usr/bin/env bash
# 把 DELTARUNE.app 还原到注入前状态（恢复原始二进制与签名）。
#
#   ./uninstall.sh            还原
#   ./uninstall.sh --dry-run  只看会做什么
#
# 依赖同目录上一级的 backup-YYYYmmdd-HHMMSS/ 备份。

set -euo pipefail

APP="${DELTARUNE_APP:-/Users/fterward/Library/Application Support/Steam/steamapps/common/DELTARUNE/DELTARUNE.app}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"

MODE="apply"
[[ "${1:-}" == "--dry-run" ]] && MODE="dryrun"

BK="$(ls -dt "$ROOT"/backup-* 2>/dev/null | head -1 || true)"
if [[ -z "${BK:-}" ]]; then
  echo "找不到备份目录：$ROOT/backup-*" >&2
  echo "可用 Steam「验证游戏文件完整性」还原。" >&2
  exit 1
fi

echo "备份目录 : $BK"
echo "目标 app : $APP"
echo

step() {
  if [[ "$MODE" == "dryrun" ]]; then echo "[dry-run] $*"; else echo "  -> $*"; fi
}

[[ -f "$BK/Mac_Runner" ]]                  && step "恢复 Contents/MacOS/Mac_Runner"
[[ -f "$BK/libYoYoGamepad.dylib" ]]        && step "恢复 Contents/Frameworks/libYoYoGamepad.dylib"
[[ -f "$BK/CodeResources" ]]               && step "恢复 Contents/_CodeSignature/CodeResources"
[[ -f "$BK/Info.plist" ]]                  && step "恢复 Contents/Info.plist"
step "删除 Contents/Frameworks/libDS5RawFix.dylib"

[[ "$MODE" == "dryrun" ]] && exit 0

cp -p "$BK/Mac_Runner"                    "$APP/Contents/MacOS/Mac_Runner"
cp -p "$BK/libYoYoGamepad.dylib"          "$APP/Contents/Frameworks/libYoYoGamepad.dylib"
mkdir -p "$APP/Contents/_CodeSignature"
cp -p "$BK/CodeResources"                 "$APP/Contents/_CodeSignature/CodeResources"
cp -p "$BK/Info.plist"                    "$APP/Contents/Info.plist"
rm -f "$APP/Contents/Frameworks/libDS5RawFix.dylib"

echo
echo "=== 校验 ==="
codesign --verify --verbose=1 "$APP" 2>&1 | tail -2
codesign -dv "$APP" 2>&1 | grep -E "Authority|TeamIdentifier|flags" | head -3
otool -L "$APP/Contents/Frameworks/libYoYoGamepad.dylib" | grep -c DS5RawFix \
  && echo "!! 依赖仍在，还原不完全" || echo "依赖已清除 ✓"
