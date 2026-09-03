#!/usr/bin/env bash
# ============================================================
# rk3588_temp.sh - RK3588 温度监测脚本
# 每 10 秒输出一次 CPU(Big/Little)/GPU/NPU/SOC 温度
#
# 用法:  ./rk3588_temp.sh
# 退出:  Ctrl+C
#
# 温度源: /sys/class/thermal/thermal_zone{N}/temp (单位 m℃)
# ============================================================

set -u

INTERVAL=10          # 输出间隔（秒）
WARN=70              # ≥70℃ 黄色
CRIT=85              # ≥85℃ 红色

# 传感器: zone 路径 -> 显示名（按需增删）
declare -a ZONES=(
    "/sys/class/thermal/thermal_zone1:big0"    # Cortex-A76 簇0
    "/sys/class/thermal/thermal_zone2:big1"    # Cortex-A76 簇1
    "/sys/class/thermal/thermal_zone3:little"  # Cortex-A55
    "/sys/class/thermal/thermal_zone5:gpu"
    "/sys/class/thermal/thermal_zone6:npu"
    "/sys/class/thermal/thermal_zone4:center"
    "/sys/class/thermal/thermal_zone0:soc"
)

# 颜色
RED='\033[31m'
YELLOW='\033[33m'
GREEN='\033[32m'
CYAN='\033[36m'
NC='\033[0m'

# 读取某 zone 温度(℃, 1位小数)，失败返回空
get_temp() {
    local zone="$1"
    if [[ -r "$zone/temp" ]]; then
        awk '{printf "%.1f", $1/1000}' "$zone/temp"
    fi
}

# 按阈值着色
colorize() {
    local t="$1"
    local ti
    ti=$(awk -v t="$t" 'BEGIN{printf "%d", t}')
    if (( ti >= CRIT )); then
        printf "${RED}%s${NC}" "$t"
    elif (( ti >= WARN )); then
        printf "${YELLOW}%s${NC}" "$t"
    else
        printf "${GREEN}%s${NC}" "$t"
    fi
}

trap 'echo; echo -e "${CYAN}退出${NC}"; exit 0' INT

echo -e "${CYAN}RK3588 温度监测（每 ${INTERVAL}s 刷新，Ctrl+C 退出）${NC}"
echo -e "${CYAN}阈值: ≥${WARN}℃ 黄 / ≥${CRIT}℃ 红${NC}"
echo

while true; do
    ts=$(date '+%F %T')
    line="[$ts] "
    max=0

    for z in "${ZONES[@]}"; do
        zone="${z%%:*}"
        name="${z##*:}"
        t=$(get_temp "$zone")
        if [[ -z "$t" ]]; then
            line+="${name}=-- "
            continue
        fi
        ti=$(awk -v t="$t" 'BEGIN{printf "%d", t}')
        (( ti > max )) && max=$ti
        line+="${name}=$(colorize "$t")℃ "
    done

    echo -e "$line"
    echo -e "       ${CYAN}最高:${NC} $(colorize "$max")℃"
    echo
    sleep "$INTERVAL"
done
