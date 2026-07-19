#pragma once

#include <string>
#include <unordered_map>

class VoiceManager {
  public:
    bool load(const std::string &file);

    std::string get(int cam_id, int roi_id);

  private:
    std::unordered_map<std::string, std::string> voices;
};