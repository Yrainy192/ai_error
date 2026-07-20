// #include "AIJsonRecorder.h"

// #include <filesystem>
// #include <fstream>

// #include <nlohmann/json.hpp>

// using json = nlohmann::json;

// bool AIJsonRecorder::saveAIJson(const std::vector<AIFrame> &frames, const std::string &filename)
// {

//     if (frames.empty()) {
//         return false;
//     }

//     try {

//         // 创建目录
//         std::filesystem::path path(filename);

//         auto parent = path.parent_path();

//         if (!parent.empty()) {
//             std::filesystem::create_directories(parent);
//         }

//         json root;

//         root["frame_count"] = frames.size();

//         root["frames"] = json::array();

//         for (const auto &frame : frames) {

//             json jf;

//             jf["frame_id"] = frame.frame_id;

//             jf["pts"] = frame.pts;

//             jf["real_time_ms"] = frame.real_time_ms;

//             jf["objects"] = json::array();

//             for (const auto &obj : frame.objects) {

//                 json jo;

//                 jo["track_id"] = obj.track_id;

//                 jo["confidence"] = obj.confidence;

//                 jo["x"] = obj.x;

//                 jo["y"] = obj.y;

//                 jo["w"] = obj.w;

//                 jo["h"] = obj.h;

//                 jf["objects"].push_back(jo);
//             }

//             root["frames"].push_back(jf);
//         }

//         std::ofstream ofs(filename);

//         if (!ofs.is_open()) {
//             return false;
//         }

//         ofs << root.dump(4);

//         ofs.close();

//     } catch (const std::exception &e) {

//         return false;
//     }

//     return true;
// }

#include "AIJsonRecorder.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

void AIJsonRecorder::saveAIJson(const std::vector<H265Packet> &video_frames,
                                const std::vector<AIFrame> &ai_frames,
                                const std::string &filename) {

    if (video_frames.empty()) {
        spdlog::warn("[AIJsonRecorder] video empty");

        return;
    }

    json root;

    root["video_frames"] = video_frames.size();

    json frames = json::array();

    /*
     * AI缓存排序
     */
    std::vector<AIFrame> ai = ai_frames;

    std::sort(ai.begin(), ai.end(),
              [](const AIFrame &a, const AIFrame &b) { return a.real_time_ms < b.real_time_ms; });

    /*
     * 以视频帧为主
     */

    for (size_t i = 0; i < video_frames.size(); i++) {

        const auto &video = video_frames[i];

        json frame;

        /*
         * 视频帧编号
         */
        frame["frame_index"] = i;

        /*
         * 保存原始时间
         */
        frame["video_time_ms"] = video.real_time_ms;

        frame["video_pts"] = video.pts;

        /*
         * 查找最近AI结果
         */

        const AIFrame *best = nullptr;

        uint64_t min_diff = UINT64_MAX;

        for (const auto &a : ai) {

            uint64_t diff = (a.real_time_ms > video.real_time_ms)
                                ? (a.real_time_ms - video.real_time_ms)
                                : (video.real_time_ms - a.real_time_ms);

            if (diff < min_diff) {
                min_diff = diff;
                best = &a;
            }
        }

        json objects = json::array();

        /*
         * 最大允许时间差
         *
         * 例如超过200ms认为没有AI
         */

        if (best && min_diff < 200) {

            frame["ai_time_ms"] = best->real_time_ms;

            frame["ai_pts"] = best->pts;

            for (auto &obj : best->objects) {

                json item;

                item["track_id"] = obj.track_id;

                item["confidence"] = obj.confidence;

                item["x"] = obj.x;

                item["y"] = obj.y;

                item["w"] = obj.w;

                item["h"] = obj.h;

                objects.push_back(item);
            }

        } else {

            /*
             * 没有AI结果
             */
            frame["ai_time_ms"] = 0;

            frame["ai_pts"] = 0;
        }

        frame["objects"] = objects;

        frames.push_back(frame);
    }

    root["frames"] = frames;

    std::ofstream ofs(filename);

    if (!ofs.is_open()) {
        spdlog::error("[AIJsonRecorder] open failed {}", filename);

        return;
    }

    ofs << root.dump(4);

    ofs.close();

    spdlog::info("[AIJsonRecorder] save {} video_frames={} json_frames={}", filename,
                 video_frames.size(), frames.size());
}