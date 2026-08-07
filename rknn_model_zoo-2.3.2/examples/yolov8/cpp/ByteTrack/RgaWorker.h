#pragma once

#include <memory>
#include <vector>

#include "Frame.h"
#include "image_utils.h"

class RgaWorker {

  public:
    bool process(const Frame &f, image_buffer_t &out);

  private:
    // ⭐复用目标 buffer，避免每帧分配
    std::vector<uint8_t> rgb_;
};