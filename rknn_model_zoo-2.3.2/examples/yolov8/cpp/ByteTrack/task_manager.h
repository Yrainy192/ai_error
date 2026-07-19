
#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include "AlarmLight.h"
#include "AudioManager.h"
#include "H265Recorder.h"
#include "VideoType.h"
#include "mysql_client.h"
#include "voice_manager.h"
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
using AlarmCallback = std::function<void(int cam_id, int roi_id, uint64_t event_time, int type)>;

class SdkManager;
class H265Recorder;

using json = nlohmann::json;

class Alarm {

  public:
    void setVoiceManager(VoiceManager *vm);

  private:
    VoiceManager *voice_mgr = nullptr;
};

struct VideoEvent {
    int cam_id;
    int roi_id;

    time_t event_time;

    time_t start_time;
    time_t end_time;

    std::string event_type;
};

// =========================
enum class ROIState { EMPTY = 0, OCCUPIED = 1 };

enum TaskState { WAIT = 0, CONSUMED = 1, TIMEOUT = 2 };

struct AlarmAudio {
    std::string wav;
    int priority = 0;
};

// =========================
struct GridMapItem {
    int cam_id = -1;
    int roi_id = -1;
};

// =========================
struct Task {
    std::string code;
    std::string grid;

    int cam_id = -1;
    int roi_id = -1;

    time_t create_time = 0;

    TaskState state = WAIT;
    bool entered = false;
};

// =========================
// 🚀 ROI状态（工业增强版）
// =========================
struct ROIStateInfo {
    ROIState state = ROIState::EMPTY;

    int stable_id = -1;

    // 🔥 3帧稳定机制
    int current_id = -1;
    int same_id_count = 0;

    // 🔥 防重复触发
    bool enter_triggered = false;
    bool alarm_triggered = false;

    // 🔥 ROI级 decision cache（带时间）
    std::unordered_map<int, time_t> decision_time;
};

// =========================
class TaskManager {
  public:
    void initGridMap();

    bool mapGrid(int grid_id, GridMapItem &item);

    void setTask(const std::string &code, const std::string &grid, int cam_id, int roi_id);

    std::vector<Task> getTasks(int cam_id);

    void setVoiceManager(VoiceManager *vm);

    void setAudioManager(AudioManager *audio);

    void setSpeaker(SdkManager *speaker);

    void setAlarmCallback(AlarmCallback cb);

    void updateEnter(int cam_id, int roi_id, int id);

    void updateLogic(int timeout_sec);

    void cleanupCache(int cam_id, int roi_id);

    // void setRecorder(H265Recorder *recorder);

    void setRecorder(int cam_id, H265Recorder *recorder);

    void setAlarmLight(AlarmLight *light);

    void setMysqlClient(MysqlClient *db);

    int getGridId(int cam_id, int roi_id);

  private:
    void triggerRecord(int cam_id, const std::string &code, VideoType type);

    void triggerAIError(int cam_id, int roi_id, const std::string &id);

    // AI短报警3秒自动关闭
    void triggerShortAlarm(int slave_id);
    //长报警等待人工解除
    void triggerLongAlarm(int slave_id);

  private:
    std::mutex mutex;

    std::unordered_map<int, std::unordered_map<int, AlarmAudio>> alarm_audio_map;

    std::unordered_map<int, GridMapItem> grid_map;

    std::unordered_map<int, std::vector<Task>> cam_tasks;

    SdkManager *speaker = nullptr;

    VoiceManager *voice_mgr = nullptr;

    AudioManager *audio_mgr = nullptr;

    MysqlClient *mysql_client = nullptr;

    AlarmLight *alarm_light_ = nullptr;

    // H265Recorder *recorder = nullptr;
    std::map<int, H265Recorder *> recorders;

    std::unordered_map<int, std::unordered_map<int, ROIStateInfo>> roi_state;

    AlarmCallback alarm_callback;

    std::mutex alarm_mutex_;

    std::set<int> active_alarm_;
};

#endif
