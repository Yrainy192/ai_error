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

//计算报警灯对应的首地址，上车相机对应32。其余相机对应1-30
int TaskManager::getGridId(int cam_id, int roi_id) {
    int grid_id;
    if (cam_id == 0) {
        grid_id = 32;
    } else {
        grid_id = cam_id * 8 + roi_id - 8;
    }
    return grid_id;
}

void TaskManager::setMysqlClient(MysqlClient *db) {
    mysql_client = db;
    spdlog::info("[TaskManager] mysql attached");
}

void TaskManager::setAlarmLight(AlarmLight *light) { alarm_light_ = light; }

void TaskManager::setVoiceManager(VoiceManager *vm) { voice_mgr = vm; }

void TaskManager::setAudioManager(AudioManager *audio) { audio_mgr = audio; }

void TaskManager::setRecorder(int cam_id, H265Recorder *recorder) {
    if (!recorder) {
        spdlog::error("[TaskManager] recorder null cam={}", cam_id);
        return;
    }

    recorder->setMysqlClient(mysql_client);

    recorders[cam_id] = recorder;

    spdlog::info("[TaskManager] bind recorder cam={}", cam_id);
}

void TaskManager::setSpeaker(SdkManager *spk) { speaker = spk; }

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
        } else if (i <= 32) {
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
    std::lock_guard<std::mutex> lock(mutex);

    for (auto &t : cam_tasks[cam_id])
        if (t.roi_id == roi_id && t.state == WAIT)
            return;

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

void TaskManager::triggerLongAlarm(int slave_id) {
    if (!alarm_light_)
        return;

    {
        std::lock_guard<std::mutex> lock(alarm_mutex_);

        long_alarm_active_.insert(slave_id);
    }

    std::thread([this, slave_id]() {
        alarm_light_->on(slave_id);

        spdlog::info("长报警启动 slave={}", slave_id);

        if (alarm_light_->waitAlarmClear(slave_id)) {
            if (mysql_client) {
                mysql_client->clearAIError(std::to_string(slave_id));
            }
        }

        {
            std::lock_guard<std::mutex> lock(alarm_mutex_);

            long_alarm_active_.erase(slave_id);
        }

        spdlog::info("长报警结束 slave={}", slave_id);
    }).detach();
}

void TaskManager::triggerShortAlarm(int slave_id) {
    spdlog::info("[ShortAlarm] trigger slave={}", slave_id);

    if (!alarm_light_) {
        spdlog::error("[ShortAlarm] alarm_light nullptr");
        return;
    }

    std::thread([this, slave_id]() {
        // 开灯
        if (!alarm_light_->on(slave_id)) {
            spdlog::error("短报警启动失败 slave={}", slave_id);

            return;
        }

        spdlog::info("短报警启动 slave={}", slave_id);

        // 持续3秒
        // sleep(3);
        std::this_thread::sleep_for(std::chrono::seconds(3));

        bool can_off = false;

        {
            std::lock_guard<std::mutex> lock(alarm_mutex_);

            // 如果没有长报警，占用灯的权利归短报警，可以关闭
            if (long_alarm_active_.count(slave_id) == 0) {
                can_off = true;
            }
        }

        if (can_off) {
            alarm_light_->off(slave_id);

            spdlog::info("短报警关闭灯 slave={}", slave_id);
        } else {
            spdlog::info("存在长报警，短报警不关闭灯 slave={}", slave_id);
        }
    }).detach();
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
    std::lock_guard<std::mutex> lock(mutex);

    auto &state = roi_state[cam_id][roi_id];
    auto &tasks = cam_tasks[cam_id];

    int grid_id = getGridId(cam_id, roi_id);

    // ==================================================
    // 空帧
    // ==================================================
    if (id == -1) {
        state.state = ROIState::EMPTY;
        state.stable_id = -1;
        state.current_id = -1;
        state.same_id_count = 0;
        state.occupied_since = 0;
        state.stuck_notified = false;

        return;
    }

    // ==================================================
    // 清理历史缓存
    // ==================================================
    cleanupCache(cam_id, roi_id);

    // ==================================================
    // ID稳定检测
    // ==================================================
    if (id == state.current_id) {
        state.same_id_count++;
    } else {
        state.current_id = id;
        state.same_id_count = 1;
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
        // 正常任务
        // ==================================================
        if (has_task) {

            for (auto &t : tasks) {
                if (t.roi_id == roi_id && t.state == WAIT) {

                    t.state = CONSUMED;
                    t.entered = true;

                    spdlog::info("[ENTER] cam={} roi={} grid={} id={}", cam_id, roi_id, grid_id,
                                 id);

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

            bool already_alarm = false;

            if (state.decision_time.count(id)) {
                already_alarm = true;
            }

            if (!already_alarm) {

                uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();

                spdlog::warn("[MIS_SORT] cam={} roi={} grid={} id={} time={}", cam_id, roi_id,
                             grid_id, id, now_ms);
                // 蜂鸣器
                // if (alarm_light_) {
                //     triggerLongAlarm(grid_id);
                // }

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
    if (state.state == ROIState::OCCUPIED && state.occupied_since > 0 && !state.stuck_notified) {

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
        if (now - it->second > 30)
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
                if (!t.entered && now - t.create_time > timeout_sec) {
                    t.state = TIMEOUT;
                    spdlog::warn("[ALARM] TASK TIMEOUT code={} roi={}", t.code, t.roi_id);

                    int grid_id = getGridId(t.cam_id, t.roi_id);
                    // int slave_id = getSlaveId(cam_id, roi_id);
                    // triggerShortAlarm(grid_id);

                    if (alarm_tcp_) {
                        alarm_tcp_->sendMessage({
                            {"action", "time_out"},
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