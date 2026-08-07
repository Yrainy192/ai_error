#pragma once

#include <cstdint>
#include <gst/gst.h>
#include <memory>
#include <vector>

struct Frame {

    std::shared_ptr<std::vector<uint8_t>> data;

    // ⭐DMABUF 零拷贝：fd + gst_buffer 引用（保证使用期间 fd 有效）
    int fd = -1;

    GstBuffer *gst_buffer = nullptr;

    int width = 0;
    int height = 0;

    // MPP实际stride
    int width_stride = 0;
    int height_stride = 0;

    uint64_t pts = 0;

    uint64_t real_time_ms = 0;

    uint64_t frame_id = 0;
};