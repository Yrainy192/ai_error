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

#include "FileStore.h"
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

std::string H265Recorder::msToDatetimeString(uint64_t time_ms) {
    time_t sec = time_ms / 1000;
    uint64_t ms = time_ms % 1000;
    struct tm tm_time;
    localtime_r(&sec, &tm_time);

    std::stringstream ss;
    ss << std::put_time(&tm_time, "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms;
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

    std::string date = getDateDir(event_time_ms);

    /*
     * 未完成文件只写 temp，写完才发布到 event
     */

    std::string temp_dir = filestore::tempDir(camera_id, date, "event");

    std::string ready_dir = filestore::readyDir(camera_id, date, "event");

    uint64_t start_time = event_time_ms > 5000 ? event_time_ms - 5000 : 0;

    /*
     * 获取前5秒
     */
    auto before = cache->getRange(start_time, event_time_ms);

    std::thread([this, before, event_time_ms, temp_dir, ready_dir, code, type]() {
        /*
         * 等待后5秒
         */
        std::this_thread::sleep_for(std::chrono::seconds(5));

        /*
         * 获取后5秒
         */
        auto after = cache->getRange(event_time_ms, event_time_ms + 5000);

        std::vector<H265Packet> all;

        all.reserve(before.size() + after.size());

        all.insert(all.end(), before.begin(), before.end());

        all.insert(all.end(), after.begin(), after.end());

        if (all.empty()) {

            spdlog::warn("[EVENT] no packets");
            return;
        }

        /*
         * DTS排序
         */

        std::sort(all.begin(), all.end(),
                  [](const H265Packet &a, const H265Packet &b) { return a.dts < b.dts; });

        /*
         * 去重复
         */

        all.erase(std::unique(all.begin(), all.end(),
                              [](const H265Packet &a, const H265Packet &b) {
                                  return a.pts == b.pts && a.dts == b.dts;
                              }),
                  all.end());

        /*
         * 找最近IDR
         */

        uint64_t target = event_time_ms > 5000 ? event_time_ms - 5000 : 0;

        size_t start_index = 0;

        bool found = false;

        for (int i = (int)all.size() - 1; i >= 0; i--) {

            if (all[i].real_time_ms <= target && all[i].keyframe) {

                start_index = i;
                found = true;
                break;
            }
        }

        /*
         * 找不到前面的IDR
         * 找第一个
         */

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

            spdlog::error("[EVENT] no IDR");
            return;
        }

        /*
         * 删除IDR之前
         */

        if (start_index > 0) {

            all.erase(all.begin(), all.begin() + start_index);
        }

        if (all.empty())
            return;

        uint64_t video_start = all.front().real_time_ms;

        uint64_t video_end = all.back().real_time_ms;

        spdlog::info("[EVENT {}] start={} end={} duration={} packets={}", camera_id, video_start,
                     video_end, video_end - video_start, all.size());

        /*
         * 创建目录
         */

        if (!filestore::ensureDir(temp_dir) || !filestore::ensureDir(ready_dir)) {

            spdlog::error("[EVENT] mkdir failed {}", temp_dir);

            return;
        }

        std::string name = filestore::uniqueBaseName(ready_dir, getTimeString(event_time_ms));

        std::string temp_ts = temp_dir + "/" + name + ".ts";

        /*
         * 保存临时TS
         */

        bool written = false;

        {

            std::lock_guard<std::mutex> lock(save_mutex);

            written = savePackets(all, temp_ts);
        }

        if (!written) {

            spdlog::error("[EVENT] write failed {}", temp_ts);

            filestore::removeQuiet(temp_ts);

            return;
        }

        /*
         * 只有完整文件才 rename 到ready目录
         */

        std::string final_ts = ready_dir + "/" + name + ".ts";

        std::string err;

        if (!filestore::publishFile(temp_ts, final_ts, &err)) {

            spdlog::error("[EVENT publish] {} ({})", final_ts, err);

            filestore::removeQuiet(temp_ts);

            return;
        }

        spdlog::info("[EVENT READY] {}", final_ts);

        /*
         * 最后写数据库
         */

        if (mysql_client) {

            std::string video_url = final_ts.substr(final_ts.find("/Videos/") + 8);

            mysql_client->updateVideo(code, video_url, type);

        } else {

            spdlog::warn("[EVENT] mysql null");
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

    std::string date = getDateDir(event_time_ms);

    /*
     * 未完成文件只写 temp，写完才发布到 AI_error
     */

    std::string temp_dir = filestore::tempDir(camera_id, date, "AI_error");

    std::string ready_dir = filestore::readyDir(camera_id, date, "AI_error");

    uint64_t start_time = event_time_ms > 5000 ? event_time_ms - 5000 : 0;

    auto before = cache->getRange(start_time, event_time_ms);

    auto before_ai = ai_cache->getRange_ai(start_time, event_time_ms);

    std::thread([this, before, before_ai, event_time_ms, temp_dir, ready_dir, grid, id]() {
        /*
         * 等待后5秒
         */

        std::this_thread::sleep_for(std::chrono::seconds(5));

        auto after = cache->getRange(event_time_ms, event_time_ms + 5000);

        auto after_ai = ai_cache->getRange_ai(event_time_ms, event_time_ms + 5000);

        std::vector<H265Packet> all;

        all.insert(all.end(), before.begin(), before.end());

        all.insert(all.end(), after.begin(), after.end());

        if (all.empty()) {
            spdlog::warn("[AI] no packets");
            return;
        }

        std::vector<AIFrame> all_ai;

        all_ai.insert(all_ai.end(), before_ai.begin(), before_ai.end());

        all_ai.insert(all_ai.end(), after_ai.begin(), after_ai.end());

        /*
         * DTS排序
         */

        std::sort(all.begin(), all.end(),
                  [](const H265Packet &a, const H265Packet &b) { return a.dts < b.dts; });

        /*
         * 去重
         */

        all.erase(std::unique(all.begin(), all.end(),
                              [](const H265Packet &a, const H265Packet &b) {
                                  return a.pts == b.pts && a.dts == b.dts;
                              }),
                  all.end());

        /*
         * 找IDR
         */

        uint64_t target = event_time_ms > 5000 ? event_time_ms - 5000 : 0;

        size_t start_index = 0;

        bool found = false;

        for (int i = all.size() - 1; i >= 0; i--) {

            if (all[i].real_time_ms <= target && all[i].keyframe) {
                start_index = i;
                found = true;
                break;
            }
        }

        if (!found) {
            spdlog::error("[AI] no IDR");
            return;
        }

        if (start_index > 0) {
            all.erase(all.begin(), all.begin() + start_index);
        }

        if (all.empty())
            return;

        uint64_t video_start = all.front().real_time_ms;

        /*
         * AI同步
         */

        std::sort(all_ai.begin(), all_ai.end(), [](const AIFrame &a, const AIFrame &b) {
            return a.real_time_ms < b.real_time_ms;
        });

        auto it = std::lower_bound(all_ai.begin(), all_ai.end(), video_start,
                                   [](const AIFrame &a, uint64_t t) { return a.real_time_ms < t; });

        if (it != all_ai.begin()) {
            all_ai.erase(all_ai.begin(), it);
        }

        /*
         * 创建目录
         */

        if (!filestore::ensureDir(temp_dir) || !filestore::ensureDir(ready_dir)) {

            spdlog::error("[AI] mkdir failed {}", temp_dir);

            return;
        }

        std::string name = filestore::uniqueBaseName(ready_dir, getTimeString(event_time_ms));

        std::string temp_ts = temp_dir + "/" + name + ".ts";

        std::string temp_json = temp_dir + "/" + name + ".json";

        std::string final_ts = ready_dir + "/" + name + ".ts";

        std::string final_json = ready_dir + "/" + name + ".json";

        /*
         * 写临时TS
         */

        bool ts_ok = false;

        {
            std::lock_guard<std::mutex> lock(save_mutex);

            ts_ok = savePackets(all, temp_ts);
        }

        if (!ts_ok) {

            spdlog::error("[AI] write ts failed {}", temp_ts);

            filestore::removeQuiet(temp_ts);

            return;
        }

        /*
         * 写临时JSON
         */

        if (!ai_json_recorder || !ai_json_recorder->saveAIJson(all, all_ai, temp_json)) {

            spdlog::error("[AI] write json failed {}", temp_json);

            filestore::removeQuiet(temp_ts);

            filestore::removeQuiet(temp_json);

            return;
        }

        /*
         * TS与JSON都写完整后才发布
         *
         * 先发布JSON再发布TS：
         * 外部一旦看到 .ts，同名 .json 必定已经就绪
         */

        std::string err;

        if (!filestore::publishFile(temp_json, final_json, &err)) {

            spdlog::error("[AI publish json] {} ({})", final_json, err);

            filestore::removeQuiet(temp_ts);

            filestore::removeQuiet(temp_json);

            return;
        }

        if (!filestore::publishFile(temp_ts, final_ts, &err)) {

            spdlog::error("[AI publish ts] {} ({})", final_ts, err);

            filestore::removeQuiet(temp_ts);

            spdlog::warn("[AI] orphan json left: {}", final_json);

            return;
        }

        spdlog::info("[AI READY] {} (+json)", final_ts);

        /*
         * mysql最后写
         */

        if (mysql_client) {

            std::string video_url = final_ts.substr(final_ts.find("/Videos/") + 8);

            mysql_client->upAIError(grid, msToDatetimeString(event_time_ms), video_url, id);
        }
    }).detach();
}

bool H265Recorder::savePackets(const std::vector<H265Packet> &packets,
                               const std::string &filename) {
    if (packets.empty()) {
        spdlog::warn("[Recorder] empty packets");
        return false;
    }

    // spdlog::info("[Recorder] input packets={}", packets.size());

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

    // for (size_t i = 0; i < std::min<size_t>(valid.size(), 10); i++) {
    //     spdlog::info("[SORT] idx={} pts={} dts={} real={} key={}", i, valid[i].pts, valid[i].dts,
    //                  valid[i].real_time_ms, valid[i].keyframe);
    // }

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

        return false;
    }

    if (start_index > 0) {
        valid.erase(valid.begin(), valid.begin() + start_index);
    }

    if (valid.empty()) {
        spdlog::error("[Recorder] empty after IDR");

        return false;
    }

    // spdlog::info("[Recorder] IDR start real={} packets={}", valid.front().real_time_ms,
    //              valid.size());

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

    // for (size_t i = 0; i < std::min<size_t>(valid.size(), 10); i++) {
    //     spdlog::info("[RESET] idx={} pts={} dts={} key={}", i, valid[i].pts, valid[i].dts,
    //                  valid[i].keyframe);
    // }

    /*
     * 5. 创建目录
     */

    if (!filestore::ensureDir(filestore::parentDir(filename))) {

        spdlog::error("[Recorder] mkdir failed {}", filename);

        return false;
    }

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

        if (pipeline) {
            gst_object_unref(pipeline);
        }

        return false;
    }

    g_object_set(sink, "location", filename.c_str(), nullptr);

    gst_bin_add_many(GST_BIN(pipeline), appsrc, parser, muxer, sink, nullptr);

    if (!gst_element_link_many(appsrc, parser, muxer, sink, nullptr)) {
        spdlog::error("[Recorder] link failed");

        gst_element_set_state(pipeline, GST_STATE_NULL);

        gst_object_unref(pipeline);

        return false;
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

    bool push_ok = true;

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

            push_ok = false;

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

    bool mux_ok = true;

    if (msg) {

        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {

            GError *err = nullptr;

            gchar *debug = nullptr;

            gst_message_parse_error(msg, &err, &debug);

            spdlog::error("[Recorder] mux error {} file={}", err ? err->message : "unknown",
                          filename);

            mux_ok = false;

            if (err) {
                g_error_free(err);
            }

            g_free(debug);
        }

        gst_message_unref(msg);
    }

    gst_object_unref(bus);

    gst_element_set_state(pipeline, GST_STATE_NULL);

    gst_object_unref(pipeline);

    /*
     * 8. 校验TS确实写完且非空
     */

    std::error_code ec;

    if (!push_ok || !mux_ok) {

        spdlog::error("[Recorder] TS broken {} (push_ok={} mux_ok={})", filename, push_ok, mux_ok);

        return false;
    }

    if (!std::filesystem::exists(filename, ec) || std::filesystem::file_size(filename, ec) == 0 ||
        ec) {

        spdlog::error("[Recorder] TS missing/empty {}", filename);

        return false;
    }

    // spdlog::info("[Recorder] TS saved packets={} file={}", valid.size(), filename);

    return true;
}