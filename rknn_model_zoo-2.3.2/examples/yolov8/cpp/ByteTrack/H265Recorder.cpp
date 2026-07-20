#include "H265Recorder.h"

#include <chrono>
#include <fstream>
#include <thread>

#include <ctime>
#include <filesystem>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <sstream>
#include <unordered_set>
std::string H265Recorder::getTimeString(uint64_t time_ms) {
    time_t sec = time_ms / 1000;

    uint64_t ms = time_ms % 1000;

    struct tm tm_time;

    localtime_r(&sec, &tm_time);

    std::stringstream ss;

    ss << std::put_time(&tm_time, "%H%M%S");

    ss << std::setfill('0') << std::setw(3) << ms;

    return ss.str();
}

// H265Recorder::H265Recorder(const std::string &id, H265RingBuffer *c) { camera_id = id; }

void H265Recorder::setMysqlClient(MysqlClient *db) { mysql_client = db; }

std::string H265Recorder::getDateDir(uint64_t time_ms) {
    time_t sec = time_ms / 1000;

    struct tm tm_time;

    localtime_r(&sec, &tm_time);

    std::stringstream ss;

    ss << std::put_time(&tm_time, "%Y%m%d");

    return ss.str();
}

void H265Recorder::saveEvent(uint64_t event_time_ms, const std::string &code, VideoType type) {
    if (!cache) {
        spdlog::error("[Recorder] cache null");
        return;
    }
    std::string dir =
        "/home/admin/Videos/" + camera_id + "/" + getDateDir(event_time_ms) + "/event";

    uint64_t start_time = (event_time_ms > 5000) ? event_time_ms - 5000 : 0;

    auto before = cache->getRange(start_time, event_time_ms);

    spdlog::info("[Recorder {}] before packets={}", camera_id, before.size());

    std::thread([this, before, event_time_ms, dir, code, type]() {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        auto after = cache->getRange(event_time_ms, event_time_ms + 5000);

        std::vector<H265Packet> all;

        all.reserve(before.size() + after.size());

        all.insert(all.end(), before.begin(), before.end());

        all.insert(all.end(), after.begin(), after.end());

        if (all.empty()) {
            spdlog::warn("[Recorder] no packets");

            return;
        }

        /*
         * PTS排序
         */
        std::sort(all.begin(), all.end(),
                  [](const H265Packet &a, const H265Packet &b) { return a.pts < b.pts; });

        /*
         * 找I帧
         */
        size_t start_index = 0;

        bool found_keyframe = false;

        for (size_t i = 0; i < all.size(); i++) {
            if (all[i].keyframe) {
                start_index = i;
                found_keyframe = true;
                break;
            }
        }

        if (!found_keyframe) {
            spdlog::error("[Recorder] no keyframe found");
            return;
        }

        if (start_index > 0) {
            all.erase(all.begin(), all.begin() + start_index);
        }

        if (all.empty()) {
            return;
        }
        std::filesystem::create_directories(dir);
        std::string filename = dir + "/" + getTimeString(event_time_ms) + ".ts";
        /*
         * 保存成功后更新数据库
         */
        int key_count = 0;

        for (auto &p : all) {
            if (p.keyframe)
                key_count++;
        }

        spdlog::info("[Recorder] packets={} keyframes={}", all.size(), key_count);
        savePackets(all, filename);

        if (mysql_client) {
            std::string video_url = filename.substr(filename.find("/Videos/") + 8);

            mysql_client->updateVideo(code, video_url, type);
        } else {
            spdlog::warn("[Recorder] mysql_client null");
        }
    }).detach();
}

void H265Recorder::saveAIError(uint64_t event_time_ms, const std::string &grid,
                               const std::string &id) {
    if (!cache) {

        spdlog::error("[Recorder] cache null");

        return;
    }

    if (!ai_cache) {

        spdlog::error("[Recorder] ai_cache null");

        return;
    }

    std::string dir =
        "/home/admin/Videos/" + camera_id + "/" + getDateDir(event_time_ms) + "/AI_error";

    uint64_t start_time = (event_time_ms > 5000) ? event_time_ms - 5000 : 0;

    /*
     * ===============================
     * 获取事件前5秒
     * ===============================
     */

    auto before = cache->getRange(start_time, event_time_ms);

    auto before_ai = ai_cache->getRange_ai(start_time, event_time_ms);

    spdlog::info("[AI] before video={} ai={}", before.size(), before_ai.size());

    std::thread([this, before, before_ai, event_time_ms, dir, grid, id]() {
        /*
         * 等待事件后5秒
         */

        std::this_thread::sleep_for(std::chrono::seconds(5));

        /*
         * ===============================
         * 获取后5秒
         * ===============================
         */

        auto after = cache->getRange(event_time_ms, event_time_ms + 5000);

        auto after_ai = ai_cache->getRange_ai(event_time_ms, event_time_ms + 5000);

        std::vector<H265Packet> all;

        all.reserve(before.size() + after.size());

        all.insert(all.end(), before.begin(), before.end());

        all.insert(all.end(), after.begin(), after.end());

        std::vector<AIFrame> all_ai;

        all_ai.reserve(before_ai.size() + after_ai.size());

        all_ai.insert(all_ai.end(), before_ai.begin(), before_ai.end());

        all_ai.insert(all_ai.end(), after_ai.begin(), after_ai.end());

        if (all.empty()) {

            spdlog::warn("[AI] no video packets");

            return;
        }

        /*
         * ===============================
         * 1. DTS排序
         * ===============================
         */

        std::sort(all.begin(), all.end(),
                  [](const H265Packet &a, const H265Packet &b) { return a.dts < b.dts; });

        spdlog::info("[AI] video after sort={}", all.size());

        /*
         * ===============================
         * 2. 去重复packet
         * ===============================
         */

        size_t duplicate_count = 0;

        auto end_it =
            std::unique(all.begin(), all.end(), [&](const H265Packet &a, const H265Packet &b) {
                bool same = (a.pts == b.pts) && (a.dts == b.dts);

                if (same) {
                    duplicate_count++;

                    spdlog::warn("[AI] remove duplicate pts={} real={}", a.pts, a.real_time_ms);
                }

                return same;
            });

        all.erase(end_it, all.end());

        spdlog::info("[AI] duplicate removed={} remain={}", duplicate_count, all.size());

        /*
         * ===============================
         * 3. 找事件前最近IDR
         * ===============================
         */

        uint64_t target_time = event_time_ms > 5000 ? event_time_ms - 5000 : 0;

        size_t start_index = 0;

        bool found = false;

        for (int i = (int)all.size() - 1; i >= 0; i--) {

            if (all[i].real_time_ms <= target_time && all[i].keyframe) {

                start_index = i;

                found = true;

                break;
            }
        }

        if (!found) {

            for (size_t i = 0; i < all.size(); i++) {

                if (all[i].keyframe) {
                    start_index = i;
                    found = true;
                    break;
                }
            }
        }

        if (!found) {

            spdlog::error("[AI] no IDR");

            return;
        }

        spdlog::info("[AI] select IDR index={} real={}", start_index,
                     all[start_index].real_time_ms);

        if (start_index > 0) {

            all.erase(all.begin(), all.begin() + start_index);
        }

        if (all.empty())
            return;

        /*
         * ===============================
         * 4. 视频真实开始时间
         * ===============================
         */

        uint64_t video_start_time = all.front().real_time_ms;

        uint64_t video_end_time = all.back().real_time_ms;

        spdlog::info("[AI] video start={} end={} duration={} packets={}", video_start_time,
                     video_end_time, video_end_time - video_start_time, all.size());

        /*
         * ===============================
         * 5. AI同步裁剪
         * ===============================
         */

        std::sort(all_ai.begin(), all_ai.end(), [](const AIFrame &a, const AIFrame &b) {
            return a.real_time_ms < b.real_time_ms;
        });

        auto ai_begin = std::lower_bound(
            all_ai.begin(), all_ai.end(), video_start_time,
            [](const AIFrame &frame, uint64_t t) { return frame.real_time_ms < t; });

        if (ai_begin != all_ai.begin()) {

            all_ai.erase(all_ai.begin(), ai_begin);
        }

        spdlog::info("[AI JSON] frames={} first={} last={}", all_ai.size(),
                     all_ai.empty() ? 0 : all_ai.front().real_time_ms,
                     all_ai.empty() ? 0 : all_ai.back().real_time_ms);

        /*
         * ===============================
         * 6. 保存文件
         * ===============================
         */
        std::filesystem::create_directories(dir);
        std::string filename = dir + "/" + getTimeString(event_time_ms) + ".ts";
        savePackets(all, filename);
        /*
         * 保存JSON
         */
        if (ai_json_recorder && !all.empty()) {

            std::string json_filename = dir + "/" + getTimeString(event_time_ms) + ".json";

            ai_json_recorder->saveAIJson(all,    // 视频帧
                                         all_ai, // AI结果
                                         json_filename);

            spdlog::info("[AI JSON] saved={} video={} ai={}", json_filename, all.size(),
                         all_ai.size());
        }

        if (mysql_client) {

            std::string video_url = filename.substr(filename.find("/Videos/") + 8);

            mysql_client->upAIError(grid, "", video_url, id);
        }
    }).detach();
}

void H265Recorder::savePackets(const std::vector<H265Packet> &packets,
                               const std::string &filename) {
    if (packets.empty()) {
        spdlog::warn("[Recorder] empty packets");
        return;
    }

    spdlog::info("[Recorder] input packets={}", packets.size());

    /*
     * 1. 拷贝
     */

    std::vector<H265Packet> valid = packets;

    /*
     * 2. 按真实时间排序
     *
     * 不使用PTS/DTS
     * ring buffer顺序可能乱
     */

    std::sort(valid.begin(), valid.end(), [](const H265Packet &a, const H265Packet &b) {
        return a.real_time_ms < b.real_time_ms;
    });

    /*
     * 调试排序结果
     */

    for (size_t i = 0; i < std::min<size_t>(valid.size(), 10); i++) {
        spdlog::info("[SORT] idx={} pts={} dts={} real={} key={}", i, valid[i].pts, valid[i].dts,
                     valid[i].real_time_ms, valid[i].keyframe);
    }

    /*
     * 3. 找第一个IDR
     *
     * 必须从I帧开始
     */

    size_t start_index = 0;

    bool found_idr = false;

    for (size_t i = 0; i < valid.size(); i++) {
        if (valid[i].keyframe) {
            start_index = i;
            found_idr = true;
            break;
        }
    }

    if (!found_idr) {
        spdlog::error("[Recorder] no IDR found");

        return;
    }

    if (start_index > 0) {
        valid.erase(valid.begin(), valid.begin() + start_index);
    }

    if (valid.empty()) {
        spdlog::error("[Recorder] empty after IDR");

        return;
    }

    spdlog::info("[Recorder] IDR start real={} packets={}", valid.front().real_time_ms,
                 valid.size());

    /*
     * 4. 重新生成PTS/DTS
     *
     * 25fps
     *
     * 40ms/frame
     *
     * GStreamer时间单位:
     * ns
     */

    const uint64_t frame_duration = 40000000;

    for (size_t i = 0; i < valid.size(); i++) {
        valid[i].pts = i * frame_duration;

        valid[i].dts = i * frame_duration;
    }

    /*
     * 时间戳检查
     */

    for (size_t i = 0; i < std::min<size_t>(valid.size(), 10); i++) {
        spdlog::info("[RESET] idx={} pts={} dts={} key={}", i, valid[i].pts, valid[i].dts,
                     valid[i].keyframe);
    }

    /*
     * 5. 创建目录
     */

    std::filesystem::path path(filename);

    std::filesystem::create_directories(path.parent_path());

    /*
     * 6. 创建GStreamer写TS pipeline
     */

    GstElement *pipeline = gst_pipeline_new("h265-ts");

    GstElement *appsrc = gst_element_factory_make("appsrc", nullptr);

    GstElement *parser = gst_element_factory_make("h265parse", nullptr);

    GstElement *muxer = gst_element_factory_make("mpegtsmux", nullptr);

    GstElement *sink = gst_element_factory_make("filesink", nullptr);

    if (!pipeline || !appsrc || !parser || !muxer || !sink) {
        spdlog::error("[Recorder] gst create failed");

        return;
    }

    g_object_set(sink, "location", filename.c_str(), nullptr);

    gst_bin_add_many(GST_BIN(pipeline), appsrc, parser, muxer, sink, nullptr);

    if (!gst_element_link_many(appsrc, parser, muxer, sink, nullptr)) {
        spdlog::error("[Recorder] link failed");

        gst_object_unref(pipeline);

        return;
    }

    /*
     * H265 Annex-B
     */

    GstCaps *caps = gst_caps_from_string("video/x-h265,"
                                         "stream-format=byte-stream,"
                                         "alignment=au");

    gst_app_src_set_caps(GST_APP_SRC(appsrc), caps);

    gst_caps_unref(caps);

    g_object_set(appsrc, "format", GST_FORMAT_TIME, nullptr);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    /*
     * 7. push buffer
     */

    for (size_t i = 0; i < valid.size(); i++) {

        auto &pkt = valid[i];

        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, pkt.data.size(), nullptr);

        gst_buffer_fill(buffer, 0, pkt.data.data(), pkt.data.size());

        GST_BUFFER_PTS(buffer) = pkt.pts;

        GST_BUFFER_DTS(buffer) = pkt.dts;

        if (pkt.keyframe) {
            GST_BUFFER_FLAG_UNSET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
        } else {
            GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
        }

        // spdlog::info("[PUSH] idx={} pts={} dts={} key={} size={}", i, pkt.pts, pkt.dts,
        //              pkt.keyframe, pkt.data.size());

        GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);

        if (ret != GST_FLOW_OK) {
            spdlog::error("[Recorder] push failed");

            break;
        }
    }

    /*
     * EOS
     */

    gst_app_src_end_of_stream(GST_APP_SRC(appsrc));

    GstBus *bus = gst_element_get_bus(pipeline);

    GstMessage *msg = gst_bus_timed_pop_filtered(
        bus, GST_CLOCK_TIME_NONE, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

    if (msg) {

        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {

            GError *err = nullptr;

            gchar *debug = nullptr;

            gst_message_parse_error(msg, &err, &debug);

            spdlog::error("[Recorder] {}", err->message);

            g_error_free(err);

            g_free(debug);
        }

        gst_message_unref(msg);
    }

    gst_object_unref(bus);

    gst_element_set_state(pipeline, GST_STATE_NULL);

    gst_object_unref(pipeline);

    spdlog::info("[Recorder] TS saved packets={} file={}", valid.size(), filename);
}