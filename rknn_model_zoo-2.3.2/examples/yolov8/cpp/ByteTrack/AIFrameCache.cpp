#include "AIFrameCache.h"

void AIFrameCache::push(const AIFrame &frame) {

    std::lock_guard<std::mutex> lock(mutex);

    cache.push_back(frame);

    // 超过限制删除旧数据

    while (cache.size() > max_frames) {
        cache.pop_front();
    }
}

std::vector<AIFrame> AIFrameCache::getRange(uint64_t start_pts, uint64_t end_pts) {
    std::vector<AIFrame> result;

    std::lock_guard<std::mutex> lock(mutex);

    for (auto &frame : cache) {

        if (frame.pts >= start_pts && frame.pts <= end_pts) {
            result.push_back(frame);
        }
    }

    return result;
}

void AIFrameCache::clear() {

    std::lock_guard<std::mutex> lock(mutex);

    cache.clear();
}