#include "YoloCache.h"

YoloCache::YoloCache(size_t max_size) : max_size(max_size) {}

void YoloCache::push(const YoloResult &result) {

    std::lock_guard<std::mutex> lock(mutex);

    buffer.push_back(result);

    // 超过限制删除最老数据
    while (buffer.size() > max_size) {
        buffer.pop_front();
    }
}

std::vector<YoloResult> YoloCache::getRange(uint64_t start_ms, uint64_t end_ms) {

    std::lock_guard<std::mutex> lock(mutex);

    std::vector<YoloResult> result;

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

void YoloCache::clear() {

    std::lock_guard<std::mutex> lock(mutex);

    buffer.clear();
}

size_t YoloCache::size() {

    std::lock_guard<std::mutex> lock(mutex);

    return buffer.size();
}