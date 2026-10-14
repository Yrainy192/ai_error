#include "AIJsonRecorder.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "FileStore.h"

using json = nlohmann::json;

bool AIJsonRecorder::saveAIJson(const std::vector<H265Packet> &video_frames,
                                const std::vector<AIFrame> &ai_frames,
                                const std::string &filename) {

    if (video_frames.empty()) {
        spdlog::warn("[AIJsonRecorder] video empty");

        return false;
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

            // if (i < 20) {

            //     spdlog::info(
            //         "[AI MATCH] video_index={} video_time={} ai_time={} diff={} objects={}", i,
            //         video.real_time_ms, best->real_time_ms, min_diff, best->objects.size());
            // }

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

    // ================================
    // AI匹配统计
    // ================================

    size_t match_count = 0;

    for (auto &f : frames) {
        if (f["ai_time_ms"] != 0)
            match_count++;
    }

    // spdlog::info("[AI MATCH SUMMARY] video_frames={} matched={} ratio={}%", video_frames.size(),
    //              match_count, video_frames.empty() ? 0 : match_count * 100 /
    //              video_frames.size());

    root["frames"] = frames;

    /*
     * 先写 .part，落盘后再原子改名，
     * 保证 filename 出现时内容一定完整
     */

    std::string part = filename + ".part";

    if (!filestore::ensureDir(filestore::parentDir(filename))) {
        spdlog::error("[AIJsonRecorder] mkdir failed {}", filename);

        return false;
    }

    std::string payload;

    try {
        payload = root.dump(4);
    } catch (const std::exception &e) {
        spdlog::error("[AIJsonRecorder] dump failed {}", e.what());

        return false;
    }

    {
        std::ofstream ofs(part, std::ios::binary | std::ios::trunc);

        if (!ofs.is_open()) {
            spdlog::error("[AIJsonRecorder] open failed {}", part);

            return false;
        }

        ofs.write(payload.data(), static_cast<std::streamsize>(payload.size()));

        ofs.flush();

        if (!ofs.good()) {

            spdlog::error("[AIJsonRecorder] write failed {}", part);

            ofs.close();

            filestore::removeQuiet(part);

            return false;
        }
    }

    std::string err;

    if (!filestore::publishFile(part, filename, &err)) {

        spdlog::error("[AIJsonRecorder] publish failed {} ({})", filename, err);

        filestore::removeQuiet(part);

        return false;
    }

    spdlog::info("[AIJsonRecorder] save {} video_frames={} json_frames={} bytes={}", filename,
                 video_frames.size(), frames.size(), payload.size());

    return true;
}