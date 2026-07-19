#pragma once

#include <memory>
#include <vector>

#include "Frame.h"
#include "image_utils.h"

class RgaWorker {

  public:
    bool process(const Frame &f, image_buffer_t &out);

  private:
    std::shared_ptr<std::vector<uint8_t>> cache;
};