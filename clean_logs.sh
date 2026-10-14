#!/usr/bin/env bash
# ============================================================
# clean_logs.sh - 自动删除 7 天前的历史日志
#
# 用法:
#   ./clean_logs.sh                 # 删除超过保留期的日志
#   DRY_RUN=1 ./clean_logs.sh       # 只打印将删除的文件，不实际删除
#   RETAIN_DAYS=3 ./clean_logs.sh   # 自定义保留天数
#
# 说明:
#   1. 只处理形如 rknn_YYYY-MM-DD.log 的按天日志，不动其它文件；
#   2. 按“文件名中的日期”判断，保留最近 RETAIN_DAYS 天（含今天），
#      因此当前正在写入的当天日志绝不会被删除；
#   3. 由 systemd 用户定时器 clean-logs.timer 每天调用。
# ============================================================

set -u

RETAIN_DAYS="${RETAIN_DAYS:-7}"   # 保留天数
DRY_RUN="${DRY_RUN:-0}"           # 1=只演练不删除

# 需要清理的日志目录（按需增删）
LOG_DIRS=(
    "/home/admin/Rtsp_6model/rknn_demo_release/log"
    "/home/admin/Rtsp_6model/rknn_model_zoo-2.3.2/examples/yolov8/cpp/build/log"
)

# 保留边界：早于该日期的日志一律删除
oldest_keep=$(date -d "$((RETAIN_DAYS - 1)) days ago" +%Y-%m-%d)

deleted=0

for dir in "${LOG_DIRS[@]}"; do
    if [[ ! -d "$dir" ]]; then
        echo "[跳过] 目录不存在: $dir"
        continue
    fi

    while IFS= read -r file; do
        base="${file##*/}"

        # 解析文件名中的日期，非 rknn_YYYY-MM-DD.log 一律跳过
        if [[ "$base" =~ ^rknn_([0-9]{4}-[0-9]{2}-[0-9]{2})\.log$ ]]; then
            fdate="${BASH_REMATCH[1]}"
        else
            continue
        fi

        # 日期字符串可直接按字典序比较
        [[ "$fdate" < "$oldest_keep" ]] || continue

        if [[ "$DRY_RUN" == "1" ]]; then
            echo "[演练] 将删除: $file (日志日期 $fdate)"
        elif rm -f -- "$file"; then
            echo "[删除] $file (日志日期 $fdate)"
        else
            echo "[失败] $file" >&2
        fi
        deleted=$((deleted + 1))
    done < <(find "$dir" -maxdepth 1 -type f -name 'rknn_*.log' | sort)
done

echo "$(date '+%F %T') 清理完成: 保留 ${RETAIN_DAYS} 天(不含早于 ${oldest_keep} 的日志), 共处理 ${deleted} 个文件"
