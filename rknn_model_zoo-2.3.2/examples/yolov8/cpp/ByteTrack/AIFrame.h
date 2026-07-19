#pragma once

#include <cstdint>
#include <vector>

struct DetectionInfo {

    int track_id;

    float confidence;

    int x;

    int y;

    int w;

    int h;
};

struct AIFrame {

    uint64_t frame_id;

    // GStreamer PTS
    uint64_t pts;

    // Unix ms
    uint64_t real_time_ms;

    std::vector<DetectionInfo> objects;
};