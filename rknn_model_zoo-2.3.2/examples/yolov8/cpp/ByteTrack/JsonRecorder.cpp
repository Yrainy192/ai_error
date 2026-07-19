#include "JsonRecorder.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

JsonRecorder::JsonRecorder(const std::string &id) : camera_id(id) {}

std::string JsonRecorder::makeDir() {

    namespace fs = std::filesystem;

    auto now = std::chrono::system_clock::now();

    auto tt = std::chrono::system_clock::to_time_t(now);

    tm tm_time;

    localtime_r(&tt, &tm_time);

    char buf[32];

    strftime(buf, sizeof(buf), "%Y%m%d", &tm_time);

    std::string dir = "/home/admin/AI/" + camera_id + "/" + buf;

    fs::create_directories(dir);

    return dir;
}

void JsonRecorder::save(uint64_t frame_id, uint64_t pts, uint64_t real_time_ms,
                        const std::vector<Detection> &objects) {

    json j;

    j["camera_id"] = camera_id;

    j["frame_id"] = frame_id;

    j["pts"] = pts;

    j["real_time_ms"] = real_time_ms;

    for (auto &obj : objects) {

        json item;

        item["class"] = obj.class_name;

        item["confidence"] = obj.confidence;

        item["bbox"] = {obj.x, obj.y, obj.width, obj.height};

        j["objects"].push_back(item);
    }

    auto dir = makeDir();

    auto now = std::chrono::system_clock::now();

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::string file = dir + "/" + std::to_string(ms) + ".json";

    std::ofstream ofs(file);

    ofs << j.dump(4);
}