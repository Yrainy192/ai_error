#include "task_manager.h"
#include "SdkManager.h"
#include <chrono>
#include <iostream>
#include <spdlog/spdlog.h>

static uint64_t getMonoTimeUs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
}

void TaskManager::setMysqlClient(MysqlClient *db) {
    mysql_client = db;
    spdlog::info("[TaskManager] mysql attached");
}

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
        } else {
            item.cam_id = 3;
            item.roi_id = i - 16;
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

void TaskManager::triggerRecord(int cam_id, const std::string &code, VideoType type) {
    auto it = recorders.find(cam_id);

    if (it == recorders.end()) {
        spdlog::error("[Recorder] no recorder cam={}", cam_id);
        return;
    }

    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

    spdlog::info("[Recorder] cam={} code={} trigger event {}", cam_id, code, now_ms);

    it->second->saveEvent(now_ms, code, type);
}

void TaskManager::triggerAIError(int cam_id, int roi_id) {
    auto it = recorders.find(cam_id);

    if (it == recorders.end()) {
        spdlog::error("[AI Error] no recorder cam={}", cam_id);
        return;
    }

    // 根据摄像头和ROI计算格口
    int grid_id = cam_id * 8 + roi_id - 8;

    std::string grid = std::to_string(grid_id);

    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

    spdlog::info("[AI Error] cam={} roi={} grid={} trigger event {}", cam_id, roi_id, grid, now_ms);

    it->second->saveAIError(now_ms, grid);
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

// 核心状态机
void TaskManager::updateEnter(int cam_id, int roi_id, int id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto &state = roi_state[cam_id][roi_id];
    auto &tasks = cam_tasks[cam_id];

    // =========================
    // 空帧 reset
    // =========================
    if (id == -1) {
        state.state = ROIState::EMPTY;
        state.stable_id = -1;
        state.current_id = -1;
        state.same_id_count = 0;
        return;
    }

    // =========================
    // TTL清理
    // =========================
    cleanupCache(cam_id, roi_id);

    // =========================
    // 3帧稳定计数
    // =========================
    if (id == state.current_id)
        state.same_id_count++;
    else {
        state.current_id = id;
        state.same_id_count = 1;
    }

    // =========================
    // ROI级去重
    // =========================
    if (state.decision_time.count(id))
        return;

    bool has_task = false;
    for (auto &t : tasks)
        if (t.roi_id == roi_id && t.state == WAIT)
            has_task = true;

    // =========================
    // NO TASK
    // =========================
    if (!has_task) {
        spdlog::warn("[ALARM] NO TASK cam={} roi={} id={}", cam_id, roi_id, id);
        std::string wav;
        if (voice_mgr) {
            wav = voice_mgr->get(cam_id, roi_id);
        } else {
            wav = "/home/admin/Rtsp/rknn_model_zoo-2.3.2/examples/yolov8/cpp/speaker/test.wav";
        }
        std::string cmd = "play -id 1 -s " + wav + " -d 1 -v 50 -fast";

        if (audio_mgr) {
            audio_mgr->post(cmd);
        }
        triggerAIError(cam_id, roi_id);
        //还需要控制对应蜂鸣器报警
        state.decision_time[id] = time(nullptr);
        return;
    }

    // =========================
    // ENTER（必须3帧稳定）
    // =========================
    if (state.state == ROIState::EMPTY && state.same_id_count >= 3) {
        state.state = ROIState::OCCUPIED;
        state.stable_id = id;
        for (auto &t : tasks) {
            if (t.roi_id == roi_id && t.state == WAIT) {
                t.state = CONSUMED;
                t.entered = true;
                // std::cout << "[INFO] ENTER cam="
                //           << cam_id
                //           << " roi=" << roi_id
                //           << " id=" << id << std::endl;
                spdlog::info("[INFO] ENTER  cam={} roi={} id={}", cam_id, roi_id, id);
                if (cam_id == 0) {
                    triggerRecord(cam_id, t.code, VideoType::UP);
                } else {
                    triggerRecord(cam_id, t.code, VideoType::DOWN);
                }
                break;
            }
        }
        state.decision_time[id] = time(nullptr);
        return;
    }

    // =========================
    // WRONG PACKAGE
    // =========================
    if (state.state == ROIState::OCCUPIED) {
        if (id != state.stable_id) {
            spdlog::warn("[ALARM] WRONG PACKAGE  cam={} roi={} id={}", cam_id, roi_id, id);
            // std::string key = "cam" + std::to_string(cam_id) + "_roi_" + std::to_string(roi_id);
            // speaker->updateAlarm(key, "play -id 1 -s test.wav -v 50", 10);
            // speaker->post("play -id 1 -s test.wav -d 1 -v 50 -fast");
            state.decision_time[id] = time(nullptr);
        }
    }
}

// =========================
void TaskManager::updateLogic(int timeout_sec) {
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