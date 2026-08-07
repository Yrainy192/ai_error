#include "RtspStream.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>

#include <gst/allocators/allocators.h>
#include <gst/video/video.h>
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

    // 释放最新帧持有的 DMABUF 引用
    {
        std::lock_guard<std::mutex> lock(frame_mutex);
        if (latest_frame.gst_buffer) {
            gst_buffer_unref(latest_frame.gst_buffer);
            latest_frame.gst_buffer = nullptr;
        }
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
    if (!latest_frame.data && latest_frame.fd < 0)
        return false;

    out = latest_frame;

    // ⭐拷贝时增加引用，保证使用期间 DMABUF fd 有效
    if (out.gst_buffer) {
        out.gst_buffer = gst_buffer_ref(out.gst_buffer);
    }

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

        "t. ! queue max-size-buffers=2 ! "
        "mppvideodec dma-feature=true ! "
        "video/x-raw(memory:DMABuf),format=NV12 ! "
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
// ⭐核心：只取 DMABUF fd，不拷贝
// =========================
GstFlowReturn RtspStream::on_new_sample(GstElement *sink) {

    if (!running) {
        return GST_FLOW_FLUSHING;
    }

    GstSample *sample = nullptr;

    g_signal_emit_by_name(sink, "pull-sample", &sample);

    if (!sample) {
        spdlog::error("[RTSP] sample null");
        return GST_FLOW_ERROR;
    }

    GstBuffer *buffer = gst_sample_get_buffer(sample);

    if (!buffer) {
        spdlog::error("[RTSP] buffer null");

        gst_sample_unref(sample);

        return GST_FLOW_ERROR;
    }

    Frame f;

    //---------------------------------
    // 获取DMABUF fd
    //---------------------------------

    GstMemory *memory = gst_buffer_peek_memory(buffer, 0);

    if (gst_is_dmabuf_memory(memory)) {

        int fd = gst_dmabuf_memory_get_fd(memory);

        f.fd = fd;

    } else {

        spdlog::error("[RTSP] not dmabuf");

        gst_sample_unref(sample);

        return GST_FLOW_ERROR;
    }

    //---------------------------------
    // 获取视频信息
    //---------------------------------

    GstCaps *caps = gst_sample_get_caps(sample);

    GstVideoInfo info;

    if (!gst_video_info_from_caps(&info, caps)) {

        spdlog::error("[RTSP] get video info failed");

        gst_sample_unref(sample);

        return GST_FLOW_ERROR;
    }

    f.width = GST_VIDEO_INFO_WIDTH(&info);

    f.height = GST_VIDEO_INFO_HEIGHT(&info);

    //---------------------------------
    // stride：必须用 GstVideoMeta 的真实行距！
    //   mppvideodec 的 NV12 每行有 padding（1920→2304），caps 的
    //   GST_VIDEO_INFO_PLANE_STRIDE 只报 1920，用错会导致 RGA 条纹花屏。
    //---------------------------------

    f.width_stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);

    f.height_stride = GST_VIDEO_INFO_HEIGHT(&info);

    GstVideoMeta *vmeta = gst_buffer_get_video_meta(buffer);
    if (vmeta && vmeta->n_planes > 0) {
        f.width_stride = vmeta->stride[0];
    }

    //---------------------------------
    // frame id
    //---------------------------------

    f.frame_id = frame_counter.fetch_add(1);

    //---------------------------------
    // 时间
    //---------------------------------

    if (GST_BUFFER_PTS_IS_VALID(buffer)) {
        f.pts = GST_BUFFER_PTS(buffer);
    } else {
        f.pts = 0;
    }

    f.real_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    //---------------------------------
    // 保存最新帧（持有 buffer 引用保证 fd 有效）
    //---------------------------------

    {
        std::lock_guard<std::mutex> lock(frame_mutex);

        if (latest_frame.gst_buffer) {
            gst_buffer_unref(latest_frame.gst_buffer);
            latest_frame.gst_buffer = nullptr;
        }

        latest_frame = f;

        latest_frame.gst_buffer = gst_buffer_ref(buffer);
    }

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
