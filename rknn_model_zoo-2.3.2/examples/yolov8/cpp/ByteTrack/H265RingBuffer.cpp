#include "H265RingBuffer.h"

#include <algorithm>
#include <spdlog/spdlog.h>

H265RingBuffer::H265RingBuffer(uint64_t keep_time_ms) : keep_time_ms(keep_time_ms) {}

//================================================
// 添加H265数据
//================================================
void H265RingBuffer::push(const H265Packet &packet) {
    std::lock_guard<std::mutex> lock(mutex);
    buffer.push_back(packet);
    uint64_t newest = packet.real_time_ms;

    while (!buffer.empty()) {
        uint64_t old = buffer.front().real_time_ms;
        if (newest > old && newest - old > keep_time_ms) {
            buffer.pop_front();
        } else {
            break;
        }
    }
}

//================================================
// 查找开始位置
//================================================
// size_t H265RingBuffer::findStartIndex(uint64_t start_ms) {
//     size_t index = 0;

//     for (size_t i = 0; i < buffer.size(); i++) {

//         if (buffer[i].real_time_ms > start_ms)
//             break;

//         if (buffer[i].keyframe)
//             index = i;
//     }
//     return index;
// }
size_t H265RingBuffer::findStartIndex(uint64_t start_ms) {
    size_t index = SIZE_MAX;

    for (size_t i = 0; i < buffer.size(); i++) {
        const auto &pkt = buffer[i];

        if (pkt.real_time_ms > start_ms) {
            break;
        }

        if (pkt.keyframe) {
            index = i;
        }
    }

    /*
     * 找不到I帧
     */
    if (index == SIZE_MAX) {
        spdlog::warn("[RingBuffer] no keyframe before {}", start_ms);

        return 0;
    }

    return index;
}
//================================================
// 获取范围
//================================================
std::vector<H265Packet> H265RingBuffer::getRange(uint64_t start_ms, uint64_t end_ms) {
    std::lock_guard<std::mutex> lock(mutex);

    std::vector<H265Packet> result;
    if (buffer.empty()) {
        return result;
    }
    size_t start = findStartIndex(start_ms);
    for (size_t i = start; i < buffer.size(); i++) {
        auto &pkt = buffer[i];
        if (pkt.real_time_ms > end_ms)
            break;
        result.push_back(pkt);
    }

    auto result_start = result.empty() ? 0 : result.front().real_time_ms;
    auto result_end = result.empty() ? 0 : result.back().real_time_ms;
    // spdlog::info("getRange {}-{} result {} packets time {}-{} duration={}ms", start_ms, end_ms,
    //              result.size(), result_start, result_end, result_end - result_start);
    return result;
}

//================================================
// 当前缓存数量
//================================================
size_t H265RingBuffer::size() {
    std::lock_guard<std::mutex> lock(mutex);
    return buffer.size();
}