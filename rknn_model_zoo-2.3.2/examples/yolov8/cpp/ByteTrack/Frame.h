#pragma once

#include <cstdint>
#include <memory>
#include <vector>

// struct Frame {
//     // NV12原始数据
//     std::shared_ptr<std::vector<uint8_t>> data;

//     // 图像尺寸
//     int width = 0;
//     int height = 0;

//     // 时间戳
//     int64_t timestamp = 0;
// };

struct Frame {
    std::shared_ptr<std::vector<uint8_t>> data;

    int width;
    int height;

    uint64_t pts;

    uint64_t real_time_ms;

    uint64_t frame_id;
};