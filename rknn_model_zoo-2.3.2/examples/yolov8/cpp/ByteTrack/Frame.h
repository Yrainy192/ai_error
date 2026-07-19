#pragma once

#include <cstdint>
#include <memory>
#include <vector>

struct Frame {
    // NV12原始数据
    std::shared_ptr<std::vector<uint8_t>> data;

    // 图像尺寸
    int width = 0;
    int height = 0;

    // 时间戳
    int64_t timestamp = 0;
};