#include "task_manager.h"
#include "AIFrameCache.h"
#include "AlarmTcpClient.h"
#include "SdkManager.h"
#include <chrono>
#include <iostream>
#include <spdlog/spdlog.h>

struct LineCounter {
    std::atomic<int> up{0};
    std::atomic<int> down{0};
};

LineCounter counter;

static uint64_t getMonoTimeUs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
}

// ==================================================
// 需要排除检测的格口（回流格口等）
// 当前：格口1 = 相机1(cam_id=1) 的第一个 ROI(roi_id=1)
//      格口39 = 相机5(cam_id=5) 的第7个 ROI(roi_id=7)
// 如需排除其它格口，在此追加 {cam_id, roi_id} 即可
// ==================================================
static bool isExcludedGrid(int cam_id, int roi_id) {
    if (cam_id == 1 && roi_id == 1) { // 格口1：回流格口，不检测
        return true;
    }
    if (cam_id == 5 && roi_id == 7) { // 格口39：与格口1一致，不检测
        return true;
    }
    return false;
}

//计算报警灯对应的首地址，上车相机对应32。其余相机对应1-40
int TaskManager::getGridId(int cam_id, int roi_id) {
    int grid_id;
    if (cam_id == 0) {
        grid_id = 0;
    } else {
        grid_id = cam_id * 8 + roi_id - 8;
    }
    return grid_id;
}

void TaskManager::setMysqlClient(MysqlClient *db) {
    mysql_client = db;
    spdlog::info("[TaskManager] mysql attached");
}

void TaskManager::setRecorder(int cam_id, H265Recorder *recorder) {
    if (!recorder) {
        spdlog::error("[TaskManager] recorder null cam={}", cam_id);
        return;
    }

    recorder->setMysqlClient(mysql_client);

    recorders[cam_id] = recorder;

    spdlog::info("[TaskManager] bind recorder cam={}", cam_id);
}

void TaskManager::setAlarmTcpClient(AlarmTcpClient *client) {
    alarm_tcp_ = client;
    spdlog::info("[TaskManager] alarm tcp client attached");
}

void TaskManager::setAlarmCallback(AlarmCallback cb) { alarm_callback = cb; }

// =========================
void TaskManager::initGridMap() {
    for (int i = 1; i <= 40; i++) {
        GridMapItem item;
        if (i <= 8) {
            item.cam_id = 1;
            item.roi_id = i;
        } else if (i <= 16) {
            item.cam_id = 2;
            item.roi_id = i - 8;
        } else if (i <= 24) {
            item.cam_id = 3;
            item.roi_id = i - 16;
        } else if (i <= 32) {
            item.cam_id = 4;
            item.roi_id = i - 24;
        } else {
            item.cam_id = 5;
            item.roi_id = i - 32;
        }
        grid_map[i] = item;
    }
}

// =========================
bool TaskManager::mapGrid(int grid_id, GridMapItem &item) {
    auto it = grid_map.find(grid_id);
    if (it == grid_map.end())
        return false;

    item = it->second;
    return true;
}

// =========================
void TaskManager::setTask(const std::string &code, const std::string &grid, int cam_id,
                          int roi_id) {
    // 回流格口不接收任务
    if (isExcludedGrid(cam_id, roi_id))
        return;

    std::lock_guard<std::mutex> lock(mutex);

    // ==================================================
    // 任务排队（FIFO）：每一次收到的 up_car / down_car 消息
    // 都是一次独立任务，不因同格口已有 WAIT 任务而丢弃。
    // 全部按到达顺序入队，由 updateEnter 在包裹进入格口时
    // 按队列顺序逐个消费。
    // 注：旧的"单任务槽位"去重会在繁忙格口丢弃后续任务，
    // 一旦前面的任务超时清掉，后续包裹进入时无任务而被误判为错分。
    // ==================================================
    Task t;
    t.code = code;
    t.grid = grid;
    t.cam_id = cam_id;
    t.roi_id = roi_id;
    t.create_time = time(nullptr);
    t.state = WAIT;

    spdlog::info("[TaskManager] setTask: code={}, grid={}, cam_id={}, roi_id={}", t.code, t.grid,
                 t.cam_id, t.roi_id);
    cam_tasks[cam_id].push_back(t);
}

void TaskManager::setAICache(int cam_id, AIFrameCache *cache) {
    if (ai_caches.size() <= cam_id)
        ai_caches.resize(cam_id + 1);

    ai_caches[cam_id] = cache;
}

void TaskManager::triggerRecord(int cam_id, const std::string &code, VideoType type) {
    auto it = recorders.find(cam_id);

    if (it == recorders.end()) {
        spdlog::error("[Recorder] no recorder cam={}", cam_id);
        return;
    }

    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

    spdlog::info("[Recorder] cam={} code={} trigger event {}  pts{}", cam_id, code, now_ms);

    it->second->saveEvent(now_ms, code, type);
}

void TaskManager::updateEnter(int cam_id, int roi_id, int id, uint64_t pts) {
    // 回流格口不参与检测（进入/错分/滞留均跳过）
    if (isExcludedGrid(cam_id, roi_id))
        return;

    std::lock_guard<std::mutex> lock(mutex);

    auto &state = roi_state[cam_id][roi_id];
    auto &tasks = cam_tasks[cam_id];

    int grid_id = getGridId(cam_id, roi_id);
    // ==================================================
    // 空帧处理
    // ==================================================
    if (id == -1) {
        // 当前没有检测到包裹
        state.empty_count++;
        // 如果当前已经占用
        if (state.state == ROIState::OCCUPIED) {
            // 允许短暂漏检
            if (state.empty_count < 3) {
                return;
            }
            // 连续多帧为空
            spdlog::info("[ROI CLEAR] cam={} roi={} id={} empty_frames={}", cam_id, roi_id,
                         state.stable_id, state.empty_count);

            state.state = ROIState::EMPTY;

            state.stable_id = -1;
            state.current_id = -1;

            state.same_id_count = 0;
            state.flicker_count = 0;

            state.occupied_since = 0;

            state.stuck_notified = false;

            // 注意：这里不再清空 decision_time，保留 id 级防重复记录。
            // 同一包裹漏检/离开后重新出现时，不会被误判为新包裹而连续报警；
            // 记录会由 cleanupCache 按 TTL 自然清理，
            // 格口真正空了超过宽限期后，同 track_id 的新包裹仍可正常重新报警。
            // state.decision_time.clear();

            state.empty_count = 0;

            return;

        }

        // 本来就是空
        else {
            state.empty_count = 0;
            return;
        }
    }
    // ==================================================
    // 清理历史缓存
    // ==================================================
    cleanupCache(cam_id, roi_id);

    // ==================================================
    // ID稳定检测：容忍 3 帧内短暂跳变，不因 ID 抖动清零
    // ==================================================
    if (id == state.current_id) {
        // 连续同 ID（或跳回主 ID），正常累计
        state.same_id_count++;
        state.flicker_count = 0;
    } else if (state.same_id_count == 0) {
        // 首次出现，确立主 ID
        state.current_id = id;
        state.same_id_count = 1;
        state.flicker_count = 0;
    } else if (state.flicker_count < 3) {
        // 主 ID 已确立，出现短暂跳变：不重置计数，只记录跳变
        state.flicker_count++;
    } else {
        // 连续跳变超过 3 帧，确认真正切换 ID
        state.current_id = id;
        state.same_id_count = 1;
        state.flicker_count = 0;
    }

    // ==================================================
    // 刷新该 id 的防重复时间戳（仅当它报过错分时）
    // 让包裹停留在格口期间防重复记录一直有效，
    // 避免被 cleanupCache 的 TTL 提前清掉，导致同一包裹再次报警
    // ==================================================
    auto dit = state.decision_time.find(id);
    if (dit != state.decision_time.end()) {
        dit->second = time(nullptr);
    }

    // ==================================================
    // 判断是否已有任务
    // ==================================================
    bool has_task = false;

    for (auto &t : tasks) {
        if (t.roi_id == roi_id && t.state == WAIT) {
            has_task = true;
            break;
        }
    }

    // ==================================================
    // EMPTY状态
    // ==================================================
    if (state.state == ROIState::EMPTY && state.same_id_count >= 3) {

        // ----------------------------------------------
        // 进入占用状态
        // 无论正常还是错分，都进入这里
        // ----------------------------------------------

        state.state = ROIState::OCCUPIED;
        state.stable_id = id;
        state.occupied_since = time(nullptr);
        state.stuck_notified = false;

        // ==================================================
        // 正常任务（FIFO：消费队列中最早入队的 WAIT 任务）
        // ==================================================
        if (has_task) {

            for (auto &t : tasks) {
                if (t.roi_id == roi_id && t.state == WAIT) {

                    t.state = CONSUMED;
                    t.entered = true;

                    spdlog::info("[ENTER] cam={} roi={} grid={} id={} code={}", cam_id, roi_id,
                                 grid_id, id, t.code);

                    if (cam_id == 0) {
                        counter.up++;
                    } else {
                        counter.down++;
                    }

                    spdlog::info("[COUNT] UP={} DOWN={}", counter.up.load(), counter.down.load());

                    break;
                }
            }

        }

        // ==================================================
        // 错分任务
        // ==================================================
        else if (cam_id != 0) {
            // 防重复：仅「同一格口 + 同一 track_id」且在 10s 内才抑制，
            // 不同格口之间互不影响
            bool already_alarm = false;

            time_t now_t = time(nullptr);
            auto it = state.decision_time.find(id);
            // id 级防重复：仅「同一格口 + 同一 track_id」且在宽限期（10s）内才抑制，
            // 不同格口 / 不同 id 互不影响
            if (it != state.decision_time.end() && now_t - it->second < 10) {
                already_alarm = true;
            }

            if (!already_alarm) {

                uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();

                spdlog::warn("[MIS_SORT] cam={} roi={} grid={} id={} time={}", cam_id, roi_id,
                             grid_id, id, now_ms);

                // TCP通知
                if (alarm_tcp_) {

                    alarm_tcp_->sendMessage({{"action", "mis_sort"},
                                             {"cam_id", std::to_string(cam_id)},
                                             {"roi_id", std::to_string(roi_id)},
                                             {"grid", std::to_string(grid_id)},
                                             {"track_id", std::to_string(id)},
                                             {"time", std::to_string(now_ms)}});
                }
                triggerAIError(cam_id, roi_id, std::to_string(id), pts, now_ms);
                state.decision_time[id] = time(nullptr);
            }
        }
        return;
    }
    // ==================================================
    // 已经占用
    // 检查滞留
    // ==================================================
    // cam0（上车相机）：仅保留检测/计数，不触发滞留报警
    if (cam_id != 0 && state.state == ROIState::OCCUPIED && state.occupied_since > 0 &&
        !state.stuck_notified) {

        time_t now = time(nullptr);

        if (now - state.occupied_since >= 10) {

            state.stuck_notified = true;

            spdlog::warn("[GRID STUCK] cam={} roi={} grid={} id={} stay={}s", cam_id, roi_id,
                         grid_id, state.stable_id, now - state.occupied_since);

            if (alarm_tcp_) {
                alarm_tcp_->sendMessage({{"action", "grid_stuck"},
                                         {"cam_id", std::to_string(cam_id)},
                                         {"roi_id", std::to_string(roi_id)},
                                         {"grid", std::to_string(grid_id)},
                                         {"track_id", std::to_string(state.stable_id)}});
            }
        }
    }
}

void TaskManager::triggerAIError(int cam_id, int roi_id, const std::string &id, uint64_t pts,
                                 uint64_t now_ms) {
    auto it = recorders.find(cam_id);

    if (it == recorders.end()) {
        spdlog::error("[AI Error] no recorder cam={}", cam_id);
        return;
    }

    // 根据摄像头和ROI计算格口
    int grid_id = getGridId(cam_id, roi_id);
    std::string grid = std::to_string(grid_id);
    spdlog::info("[AI Error] cam={} roi={} grid={} trigger event {} pts {}", cam_id, roi_id, grid,
                 now_ms, pts);
    it->second->saveAIError(now_ms, grid, id);
}

// =========================
std::vector<Task> TaskManager::getTasks(int cam_id) {
    std::lock_guard<std::mutex> lock(mutex);
    return cam_tasks[cam_id];
}

// cache清理（TTL）
void TaskManager::cleanupCache(int cam_id, int roi_id) {
    auto &state = roi_state[cam_id][roi_id];
    time_t now = time(nullptr);

    for (auto it = state.decision_time.begin(); it != state.decision_time.end();) {
        if (now - it->second > 10)
            it = state.decision_time.erase(it);
        else
            ++it;
    }
}

// =========================
void TaskManager::updateLogic(float timeout_sec) {
    std::vector<std::tuple<int, int, std::string>> alarms;
    time_t now = time(nullptr);
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto &kv : cam_tasks) {
            for (auto &t : kv.second) {
                if (t.state != WAIT)
                    continue;
                // 回流格口不参与超时报警
                if (isExcludedGrid(t.cam_id, t.roi_id))
                    continue;
                if (!t.entered && now - t.create_time > timeout_sec) {
                    t.state = TIMEOUT;

                    // cam0（上车相机）：仅清理任务状态，不触发超时报警/录像
                    if (t.cam_id == 0)
                        continue;

                    spdlog::warn("[ALARM] TASK TIMEOUT code={} roi={}", t.code, t.roi_id);

                    int grid_id = getGridId(t.cam_id, t.roi_id);
                    // int slave_id = getSlaveId(cam_id, roi_id);
                    // triggerShortAlarm(grid_id);

                    if (alarm_tcp_) {
                        alarm_tcp_->sendMessage({
                            {"action", "time_out"},
                            {"code", t.code},
                            {"cam_id", std::to_string(t.cam_id)},
                            {"roi_id", std::to_string(t.roi_id)},
                            {"grid", std::to_string(grid_id)},
                        });
                    }

                    if (t.cam_id == 0) {
                        triggerRecord(t.cam_id, t.code, VideoType::UP_ERROR);
                    } else {
                        triggerRecord(t.cam_id, t.code, VideoType::DOWN_ERROR);
                    }

                    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count();
                    spdlog::info("[Recorder] trigger event time={}", now_ms);
                }
            }
        }
    }
}