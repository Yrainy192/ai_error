#!/bin/bash
# ============================================================
# 安装/更新三个自启服务：ai / ai_control / upload_service
# 用法：
#   sudo ./install.sh              # 安装 ai_control + upload_service（不带 ai）
#   sudo ENABLE_AI=1 ./install.sh  # 同时启用 ai.service（开机自启 AI 推理）
#   sudo RESTART=1  ./install.sh   # 安装后立即重启这三个服务
# ============================================================
set -euo pipefail

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
DEST_DIR="/etc/systemd/system"

UNITS=(ai.service ai_control.service upload_service.service)
# 默认启用（与现场一致）：ai_control、upload_service
ENABLE_UNITS=(ai_control.service upload_service.service)

if [[ "$(id -u)" -ne 0 ]]; then
  echo "错误：需要 root 权限，请用 sudo $0" >&2
  exit 1
fi

echo ">>> 安装单元文件到 $DEST_DIR"
for u in "${UNITS[@]}"; do
  if [[ ! -f "$SRC_DIR/$u" ]]; then
    echo "错误：缺少 $SRC_DIR/$u" >&2
    exit 1
  fi
  install -m 644 "$SRC_DIR/$u" "$DEST_DIR/$u"
  echo "    ok  $u"
done

if [[ "${ENABLE_AI:-0}" == "1" ]]; then
  ENABLE_UNITS+=(ai.service)
  echo ">>> 已包含 ai.service（开机自启）"
fi

systemctl daemon-reload

echo ">>> 设置开机自启"
for u in "${ENABLE_UNITS[@]}"; do
  systemctl enable "$u"
done

if [[ "${RESTART:-0}" == "1" ]]; then
  echo ">>> 重启服务"
  for u in "${ENABLE_UNITS[@]}"; do
    systemctl restart "$u"
  done
fi

echo
echo ">>> 当前状态"
for u in "${UNITS[@]}"; do
  printf '%-24s %s / %s\n' "$u" "$(systemctl is-enabled "$u" 2>&1)" "$(systemctl is-active "$u" 2>&1)"
done

echo
echo "提示：ai.service 需先停止才能替换二进制，否则报 Text file busy"
echo "      sudo systemctl stop ai.service   # 替换 rknn_yolov8_demo_zero_copy 后再 start"
