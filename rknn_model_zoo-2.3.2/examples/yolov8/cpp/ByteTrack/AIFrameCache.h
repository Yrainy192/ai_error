#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>
struct DetectionInfo {

    int track_id = -1;

    float confidence = 0.0f;

    int x = 0;
    int y = 0;

    int w = 0;
    int h = 0;
};

struct AIFrame {

    uint64_t frame_id = 0;

    // GStreamer PTS(ns)
    uint64_t pts = 0;

    // Unix ms
    uint64_t real_time_ms = 0;

    std::vector<DetectionInfo> objects;
};

class AIFrameCache {

  public:
    explicit AIFrameCache(size_t max_size = 600);

    // 添加一帧结果
    void push(const AIFrame &result);

    // 按真实时间查询
    std::vector<AIFrame> getRange_ai(uint64_t start_ms, uint64_t end_ms);

    void clear();

    size_t size();

  private:
    std::deque<AIFrame> buffer;

    mutable std::mutex mutex;

    size_t max_size;
};