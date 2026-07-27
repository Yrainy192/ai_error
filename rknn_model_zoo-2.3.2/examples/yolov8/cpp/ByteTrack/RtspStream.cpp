#include "RtspStream.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>

#include <spdlog/spdlog.h>

// RtspStream::RtspStream(const std::string &url, const std::string &id)
//     : rtsp_url(url), camera_id(id) {
//     h265_recorder = std::make_unique<H265Recorder>(camera_id, &h265_cache, &ai_frame_cache);
// }

RtspStream::RtspStream(const std::string &url, const std::string &cameraId, AIFrameCache *cache)
    : rtsp_url(url), camera_id(cameraId), ai_cache(cache) {

    h265_recorder = std::make_unique<H265Recorder>(camera_id, &h265_cache, ai_cache);
}

RtspStream::~RtspStream() { stop(); }

void RtspStream::stop() {
    spdlog::info("[RTSP] stop request {}", camera_id);

    running = false;

    /*
     * 等待run线程自己释放GStreamer
     */
    if (worker.joinable()) {
        worker.join();
    }

    spdlog::info("[RTSP] stopped {}", camera_id);
}

bool RtspStream::start() {

    running = true;

    worker = std::thread(&RtspStream::run, this);

    return true;
}

// void RtspStream::stop() {
//     running = false;

//     if (worker.joinable())
//         worker.join();

//     if (h265_sink) {
//         gst_object_unref(h265_sink);
//         h265_sink = nullptr;
//     }

//     // GStreamer splitmuxsink recorder
//     if (gst_recorder) {
//         gst_object_unref(gst_recorder);
//         gst_recorder = nullptr;
//     }

//     if (pipeline) {
//         gst_element_set_state(pipeline, GST_STATE_NULL);

//         gst_object_unref(pipeline);
//         pipeline = nullptr;
//     }
// }
// =========================
// 获取最新 NV12 帧
// =========================
bool RtspStream::getLatest(Frame &out) {
    std::lock_guard<std::mutex> lock(frame_mutex);
    if (!latest_frame.data)
        return false;

    out = latest_frame;
    return true;
}

void RtspStream::run() {
    gst_init(nullptr, nullptr);

    buildPipeline();

    if (!pipeline) {
        spdlog::error("pipeline create failed");
        return;
    }

    // appsink
    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");

    if (!sink) {
        spdlog::error("appsink not found");
        return;
    }

    g_signal_connect(sink, "new-sample", G_CALLBACK(on_new_sample_static), this);

    // H265缓存 appsink
    h265_sink = gst_bin_get_by_name(GST_BIN(pipeline), "h265cache");

    if (h265_sink) {

        g_signal_connect(h265_sink, "new-sample", G_CALLBACK(on_h265_sample_static), this);

    } else {
        spdlog::error("h265cache not found");
    }

    // GStreamer splitmuxsink
    gst_recorder = gst_bin_get_by_name(GST_BIN(pipeline), "recorder");

    if (!gst_recorder) {
        spdlog::error("gst recorder not found");
    } else {
        g_signal_connect(gst_recorder, "format-location", G_CALLBACK(format_location_cb), this);
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (sink) {
        gst_object_unref(sink);
        sink = nullptr;
    }

    if (gst_recorder) {
        gst_object_unref(gst_recorder);
    }

    if (h265_sink) {
        gst_object_unref(h265_sink);
    }

    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);

        gst_object_unref(pipeline);
        pipeline = nullptr;
    }
}

void RtspStream::buildPipeline() {
    std::string record_file = "/home/admin/Videos/" + camera_id + ".ts";
    std::string pipeline_str =

        "rtspsrc location=" + rtsp_url +
        " latency=100 protocols=tcp ! "

        "rtph265depay ! "

        "h265parse config-interval=-1 ! "

        "tee name=t "

        /******** YOLO ********/

        "t. ! queue ! "
        "mppvideodec ! "
        "video/x-raw,format=NV12 ! "
        "appsink "
        "name=sink "
        "emit-signals=true "
        "sync=false "
        "drop=true "
        "max-buffers=1 "

        /******** 长期录像 ********/

        "t. ! queue ! "
        "h265parse ! "
        "splitmuxsink "
        "name=recorder "
        "muxer-factory=mpegtsmux "
        "location=dummy.ts "
        "max-size-time=60000000000 "

        /******** 事件缓存 ********/

        "t. ! queue ! "
        "h265parse disable-passthrough=true config-interval=-1 ! "
        "video/x-h265,stream-format=byte-stream,alignment=au ! "
        "appsink "
        "name=h265cache "
        "emit-signals=true "
        "sync=false "
        "drop=false "
        "max-buffers=600 ";

    spdlog::info("Pipeline:\n{}", pipeline_str);
    GError *error = nullptr;
    pipeline = gst_parse_launch(pipeline_str.c_str(), &error);
    if (error) {
        spdlog::error("gst_parse_launch error: {}", error->message);
        g_error_free(error);
    }
    if (!pipeline) {
        spdlog::error("pipeline == nullptr");
    }
}

GstFlowReturn RtspStream::on_new_sample_static(GstElement *sink, gpointer user_data) {
    return static_cast<RtspStream *>(user_data)->on_new_sample(sink);
}

GstFlowReturn RtspStream::on_h265_sample_static(GstElement *sink, gpointer user_data) {

    return static_cast<RtspStream *>(user_data)->on_h265_sample(sink);
}

GstFlowReturn RtspStream::on_h265_sample(GstElement *sink) {

    if (!running) {
        return GST_FLOW_FLUSHING;
    }

    GstSample *sample = nullptr;

    g_signal_emit_by_name(sink, "pull-sample", &sample);

    if (!sample) {
        return GST_FLOW_ERROR;
    }

    GstBuffer *buffer = gst_sample_get_buffer(sample);

    if (!buffer) {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    GstMapInfo map;

    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {

        H265Packet pkt;

        /*
         * =========================
         * frame id
         * =========================
         */

        pkt.frame_id = h265_counter.fetch_add(1);

        /*
         * =========================
         * PTS

         * RTSP H265有PTS
         * =========================
         */

        if (GST_BUFFER_PTS_IS_VALID(buffer)) {
            pkt.pts = GST_BUFFER_PTS(buffer);
        } else {
            pkt.pts = 0;

            spdlog::warn("[H265] invalid PTS frame={}", pkt.frame_id);
        }

        /*
         * =========================
         * DTS
         *
         * 摄像头没有提供DTS
         *
         * 当前流无B帧
         *
         * DTS=PTS
         * =========================
         */

        pkt.dts = pkt.pts;

        /*
         * =========================
         * real time
         * =========================
         */

        pkt.real_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

        /*
         * =========================
         * keyframe
         * =========================
         *
         * IDR判断
         *
         */

        pkt.keyframe = !GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);

        /*
         * =========================
         * H265 AU数据
         * =========================
         */

        pkt.data.assign(map.data, map.data + map.size);

        /*
         * =========================
         * push ring buffer
         * =========================
         */

        h265_cache.push(pkt);

        gst_buffer_unmap(buffer, &map);
    }

    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

// =========================
// ⭐核心：只取 NV12，不处理
// =========================
GstFlowReturn RtspStream::on_new_sample(GstElement *sink) {

    if (!running) {
        return GST_FLOW_FLUSHING;
    }

    GstSample *sample = nullptr;
    g_signal_emit_by_name(sink, "pull-sample", &sample);

    if (!sample)
        return GST_FLOW_ERROR;
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    // =========================
    // ⭐只做数据拷贝（NV12）
    // =========================
    auto frame_data = std::make_shared<std::vector<uint8_t>>();
    frame_data->resize(map.size);
    memcpy(frame_data->data(), map.data, map.size);
    Frame f;
    f.data = frame_data;
    f.width = 1920;
    f.height = 1080;
    // f.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    // 新增

    f.frame_id = frame_counter.fetch_add(1);

    // f.pts = GST_BUFFER_PTS(buffer);
    if (GST_BUFFER_PTS_IS_VALID(buffer)) {
        f.pts = GST_BUFFER_PTS(buffer);
    } else {
        f.pts = 0;
    }

    f.real_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    {
        std::lock_guard<std::mutex> lock(frame_mutex);
        latest_frame = f;
    }
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

std::string RtspStream::makeRecordDir() {
    namespace fs = std::filesystem;
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    tm tm_time;
    localtime_r(&tt, &tm_time);
    char date[16];
    strftime(date, sizeof(date), "%Y%m%d", &tm_time);
    std::string dir = "/home/admin/Videos/" + camera_id + "/" + date;
    fs::create_directories(dir);
    return dir;
}

gchar *RtspStream::format_location_cb(GstElement *, guint, gpointer user_data) {
    RtspStream *self = static_cast<RtspStream *>(user_data);
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    tm tm_time;
    localtime_r(&tt, &tm_time);
    char time_name[32];
    strftime(time_name, sizeof(time_name), "%H%M%S.ts", &tm_time);
    std::string dir = self->makeRecordDir();
    std::string filename = dir + "/" + time_name;
    return g_strdup(filename.c_str());
}
// gchar *RtspStream::format_location_cb(GstElement *, guint fragment_id, gpointer user_data) {
//     RtspStream *self = static_cast<RtspStream *>(user_data);

//     auto now = std::chrono::system_clock::now();

//     auto tt = std::chrono::system_clock::to_time_t(now);

//     tm tm_time;

//     localtime_r(&tt, &tm_time);

//     char filename[64];

//     /*
//      * 文件名:
//      *
//      * 20260723_163012_001.ts
//      *
//      */
//     strftime(filename, sizeof(filename), "%Y%m%d_%H%M%S", &tm_time);

//     std::string dir = self->makeRecordDir();

//     std::string filepath =
//         dir + "/" + std::string(filename) + "_" + std::to_string(fragment_id) + ".ts";

//     spdlog::info("[Recorder] create {}", filepath);

//     return g_strdup(filepath.c_str());
// }