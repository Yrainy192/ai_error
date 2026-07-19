#pragma once

#include "AIFrame.h"

#include <deque>
#include <mutex>
#include <vector>

class AIFrameCache {

  public:
    // 添加一帧AI结果
    void push(const AIFrame &frame);

    // 根据PTS取时间范围
    std::vector<AIFrame> getRange(uint64_t start_pts, uint64_t end_pts);

    // 清空
    void clear();

  private:
    std::deque<AIFrame> cache;

    std::mutex mutex;

    // 缓存多少帧
    size_t max_frames = 1000;
};