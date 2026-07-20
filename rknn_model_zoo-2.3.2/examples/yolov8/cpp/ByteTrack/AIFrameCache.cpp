#include "AIFrameCache.h"
#include <spdlog/spdlog.h>
AIFrameCache::AIFrameCache(size_t max_size) : max_size(max_size) {}

void AIFrameCache::push(const AIFrame &result) {

    std::lock_guard<std::mutex> lock(mutex);

    buffer.push_back(result);

    // spdlog::info("AI cache push id={} time={} size={}", result.frame_id, result.real_time_ms,
    //              buffer.size());
    // 超过限制删除最老数据
    while (buffer.size() > max_size) {
        buffer.pop_front();
    }
}

std::vector<AIFrame> AIFrameCache::getRange_ai(uint64_t start_ms, uint64_t end_ms) {

    std::lock_guard<std::mutex> lock(mutex);

    std::vector<AIFrame> result;

    if (buffer.empty()) {
        return result;
    }

    for (const auto &frame : buffer) {

        // 还没到开始时间
        if (frame.real_time_ms < start_ms) {
            continue;
        }

        // 超过结束时间
        if (frame.real_time_ms > end_ms) {
            break;
        }

        result.push_back(frame);
    }

    return result;
}

void AIFrameCache::clear() {

    std::lock_guard<std::mutex> lock(mutex);

    buffer.clear();
}

size_t AIFrameCache::size() {

    std::lock_guard<std::mutex> lock(mutex);

    return buffer.size();
}