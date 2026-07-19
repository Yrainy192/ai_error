#pragma once

#include <string>
#include <vector>

#include "Detection.h"

class JsonRecorder {

  public:
    JsonRecorder(const std::string &camera_id);

    void save(uint64_t frame_id, uint64_t pts, uint64_t real_time_ms,
              const std::vector<Detection> &objects);

  private:
    std::string camera_id;

    std::string makeDir();
};