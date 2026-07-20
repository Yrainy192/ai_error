#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

// 单个目标
struct DetectionInfo {
    int track_id = -1;

    float confidence = 0.0f;

    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// 一帧检测结果
struct YoloResult {
    // 对应Frame
    uint64_t frame_id = 0;

    // GStreamer PTS
    uint64_t pts = 0;

    // 系统时间ms
    uint64_t real_time_ms = 0;

    // 这一帧所有目标
    std::vector<DetectionInfo> objects;
};

class YoloCache {

  public:
    explicit YoloCache(size_t max_size = 300);

    // 添加一帧结果
    void push(const YoloResult &result);

    // 按真实时间查询
    std::vector<YoloResult> getRange(uint64_t start_ms, uint64_t end_ms);

    void clear();

    size_t size();

  private:
    std::deque<YoloResult> buffer;

    mutable std::mutex mutex;

    size_t max_size;
};