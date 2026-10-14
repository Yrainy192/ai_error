# 开机自启服务整理（ai / ai_control / upload_service）

从板子 `admin@admin` 上导出的三个 systemd 系统级服务配置，均为 `WantedBy=multi-user.target`。
单元文件来源目录：`/etc/systemd/system/`。

## 一览

| 单元文件 | 作用 | 运行用户 | 工作目录 / 程序 | 日志 | 重启策略 | 现场状态 |
|---|---|---|---|---|---|---|
| `ai.service` | RK3588 YOLOv8 AI 推理主程序 | admin | `~/Rtsp_6model/rknn_demo_release` → `rknn_yolov8_demo_zero_copy` | 由程序自写 `log/rknn_*.log` | always / 3s | **disabled**, inactive |
| `ai_control.service` | AI 远程控制服务（收指令、控格口等） | admin | 无（绝对路径启动）→ `~/ai_control/ai_control_server` | append 到 `~/ai_control/ai_control_server.log`（stdout+stderr） | always / 3s | enabled, active |
| `upload_service.service` | 视频上传服务 | admin | `~/upload_service/build` → `./upload_service` | journal | always / 5s | enabled, active |

## 关键差异点

- **依赖**：`ai.service` / `ai_control.service` 只 `After=network.target`；`upload_service.service` 用
  `After=network-online.target` + `Wants=network-online.target`（等网络真正就绪，上传才不失败）。
- **额外环境**：只有 `ai.service` 设置了 `Environment=LD_LIBRARY_PATH=/usr/lib:/usr/lib/aarch64-linux-gnu`。
- **WorkingDirectory**：`ai.service` 和 `upload_service.service` 有，`ai_control.service` 没有
  （因此它所有路径都必须是绝对路径）。
- **日志落盘**：只有 `ai_control.service` 用 `StandardOutput/StandardError=append:...` 写文件，
  文件不会自动轮转，需要留意体积。
- **路径强绑定**：三个单元里的路径全部是 `/home/admin/...` 绝对路径，换机器/换用户名必须改。

## 安装

```bash
cd ~/Rtsp_6model/deploy/systemd
sudo ./install.sh              # 安装并 enable ai_control + upload_service（与现场一致）
sudo ENABLE_AI=1 ./install.sh  # 额外 enable ai.service
sudo RESTART=1 ./install.sh    # 安装后立即启动
```

手动方式（等价）：

```bash
sudo cp ai.service ai_control.service upload_service.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable ai_control.service upload_service.service
sudo systemctl restart ai_control.service upload_service.service
```

## 常见运维命令

```bash
systemctl status ai_control.service upload_service.service
systemctl is-enabled ai.service ai_control.service upload_service.service
journalctl -u upload_service.service -f          # upload_service 日志走 journal
tail -f ~/ai_control/ai_control_server.log        # ai_control 日志走文件
sudo systemctl restart ai_control.service
```

## 注意

1. **`ai.service` 目前是 disabled 状态**（本次导出时确认），即开机不会自动拉起 AI 推理程序。
   需要开机自启用 `sudo ENABLE_AI=1 ./install.sh`。
2. 替换 AI 二进制前必须先 `systemctl stop ai.service`，否则报 `Text file busy`。
3. `upload_service` 的工作目录是 `build/`，其读取的 `config.json` 在上一级
   （`~/upload_service/config.json`），改动时注意实际生效路径。
4. 若目标机器用户名不是 `admin` 或家目录不是 `/home/admin`，需批量替换单元文件中的路径。
