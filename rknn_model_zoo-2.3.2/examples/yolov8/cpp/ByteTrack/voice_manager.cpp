#include "voice_manager.h"

#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool VoiceManager::load(const std::string &file) {
    std::ifstream in(file);

    if (!in.is_open()) {
        return false;
    }

    json j;
    in >> j;

    for (auto &item : j.items()) {
        voices[item.key()] = item.value();
    }

    return true;
}

std::string VoiceManager::get(int cam_id, int roi_id) {
    std::string key = std::to_string(cam_id) + "_" + std::to_string(roi_id);

    auto it = voices.find(key);

    if (it != voices.end()) {
        return it->second;
    }

    return "tts/default.wav";
}