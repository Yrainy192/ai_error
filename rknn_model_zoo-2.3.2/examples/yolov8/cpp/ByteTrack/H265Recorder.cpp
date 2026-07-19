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

void H265Recorder::saveAIError(uint64_t event_time_ms, const std::string &grid,
                               const std::string &id) {
    if (!cache) {
        spdlog::error("[Recorder] cache null");
        return;
    }

    std::string dir =
        "/home/admin/Videos/" + camera_id + "/" + getDateDir(event_time_ms) + "/AI_error";

    uint64_t start_time = (event_time_ms > 5000) ? event_time_ms - 5000 : 0;

    auto before = cache->getRange(start_time, event_time_ms);

    spdlog::info("[AI Error Recorder {}] before packets={}", camera_id, before.size());

    std::thread([this, before, event_time_ms, dir, grid, id]() {
        // 等待后5秒数据
        std::this_thread::sleep_for(std::chrono::seconds(5));

        auto after = cache->getRange(event_time_ms, event_time_ms + 5000);

        std::vector<H265Packet> all;

        all.reserve(before.size() + after.size());

        all.insert(all.end(), before.begin(), before.end());

        all.insert(all.end(), after.begin(), after.end());

        if (all.empty()) {

            spdlog::warn("[AI Error Recorder] no packets");

            return;
        }

        // PTS排序
        std::sort(all.begin(), all.end(),
                  [](const H265Packet &a, const H265Packet &b) { return a.pts < b.pts; });

        // 找I帧
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
            spdlog::error("[AI Error Recorder] no keyframe found");
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

        // 保存视频
        savePackets(all, filename);

        // 保存数据库
        if (mysql_client) {
            std::string video_url = filename.substr(filename.find("/Videos/") + 8);

            std::time_t now = std::time(nullptr);

            char timeBuffer[32];

            std::strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S",
                          std::localtime(&now));

            mysql_client->upAIError(grid, timeBuffer, video_url, id);

        } else {
            spdlog::warn("[AI Error Recorder] mysql_client null");
        }
    }).detach();
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
        savePackets(all, filename);

        if (mysql_client) {
            std::string video_url = filename.substr(filename.find("/Videos/") + 8);

            mysql_client->updateVideo(code, video_url, type);
        } else {
            spdlog::warn("[Recorder] mysql_client null");
        }
    }).detach();
}

void H265Recorder::savePackets(const std::vector<H265Packet> &packets,
                               const std::string &filename) {
    // spdlog::info("[{}] savePackets packets={} duration={}ms", camera_id, packets.size(),
    //              packets.empty() ? 0 : packets.back().real_time_ms -
    //              packets.front().real_time_ms);

    if (packets.empty()) {
        spdlog::warn("[Recorder] empty packets");
        return;
    }

    /*
     * 1. 排序
     * 防止ring buffer取出的数据PTS乱序
     */
    std::vector<H265Packet> sorted = packets;

    std::sort(sorted.begin(), sorted.end(),
              [](const H265Packet &a, const H265Packet &b) { return a.pts < b.pts; });

    /*
     * 2. 找开始位置
     *
     * 找最近IDR
     * 前面保留参数集
     *
     * VPS/SPS/PPS一般在IDR前
     */
    size_t start = 0;

    for (size_t i = 0; i < sorted.size(); i++) {
        if (sorted[i].keyframe) {
            start = i;
            break;
        }
    }

    /*
     * 如果前面有参数帧
     * 往前找
     */
    while (start > 0) {
        /*
         * VPS/SPS/PPS不是keyframe
         * 这里简单向前扩展
         *
         * 你的缓存如果保存完整NAL，
         * 这里可以判断NAL type
         */
        start--;

        if (sorted[start].keyframe)
            break;
    }

    std::vector<H265Packet> valid;

    for (size_t i = start; i < sorted.size(); i++) {
        valid.push_back(sorted[i]);
    }

    if (valid.empty()) {
        spdlog::error("[Recorder] no valid packets");

        return;
    }

    /*
     * 3. 修正时间戳
     *
     * TS最好从0开始
     */
    uint64_t base_pts = valid.front().pts;

    uint64_t base_dts = valid.front().dts;

    for (auto &pkt : valid) {
        pkt.pts -= base_pts;

        pkt.dts -= base_dts;
    }

    /*
     * 4. 创建目录
     */
    std::filesystem::path path(filename);

    std::filesystem::create_directories(path.parent_path());

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
        spdlog::error("[Recorder] gst link failed");

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
     * 5. 推送数据
     */
    for (auto &pkt : valid) {

        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, pkt.data.size(), nullptr);

        gst_buffer_fill(buffer, 0, pkt.data.data(), pkt.data.size());

        GST_BUFFER_PTS(buffer) = pkt.pts;

        GST_BUFFER_DTS(buffer) = pkt.dts;

        if (pkt.keyframe) {
            GST_BUFFER_FLAG_UNSET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
        } else {
            GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
        }

        GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);

        if (ret != GST_FLOW_OK) {
            spdlog::error("[Recorder] push failed");

            break;
        }
    }

    /*
     * 结束输入
     */
    gst_app_src_end_of_stream(GST_APP_SRC(appsrc));

    /*
     * 等待mux完成
     */
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

    spdlog::info("[Recorder] TS saved {} packets -> {}", valid.size(), filename);
}
