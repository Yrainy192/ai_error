#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gst/gst.h>

#include "AIFrameCache.h"
#include "Frame.h"
#include "H265Recorder.h"
#include "H265RingBuffer.h"
#include "mysql_client.h"
class RtspStream {

  public:
    RtspStream(const std::string &url, const std::string &cameraId, AIFrameCache *cache);

    ~RtspStream();

    bool start();

    void stop();

    bool getLatest(Frame &out);

    GstFlowReturn on_new_sample(GstElement *sink);

    static GstFlowReturn on_new_sample_static(GstElement *sink, gpointer user_data);

    static GstFlowReturn on_h265_sample_static(GstElement *sink, gpointer user_data);

    GstFlowReturn on_h265_sample(GstElement *sink);

    H265RingBuffer *getH265Cache() { return &h265_cache; }

    H265Recorder *getRecorder() { return h265_recorder.get(); }

    AIFrameCache *getAIFrameCache() { return &ai_frame_cache; }

  private:
    void run();

    void buildPipeline();

    // <root>/<cam>/<date>/record：主录像正式目录（一段写完才放进来）
    std::string makeRecordDir();

    // <root>/<cam>/<date>/temp/record：主录像写入中的目录
    std::string makeRecordTempDir();

    // 把上一段已写完的临时TS发布到正式目录
    void publishCurrentRecord();

    // 发布单个临时TS
    void publishRecordFile(const std::string &temp_path);

    // 扫描 temp/record，发布残留的完整TS（异常退出/最后一段）
    void flushRecordTempDir(const std::string &temp_dir);

    static gchar *format_location_cb(GstElement *, guint, gpointer user_data);

  private:
    std::string rtsp_url;

    std::string camera_id;

    GstElement *pipeline = nullptr;

    GstElement *gst_recorder = nullptr;

    GstElement *h265_sink = nullptr;

    MysqlClient *mysql_client = nullptr;

    std::thread worker;

    std::atomic<bool> running{false};

    Frame latest_frame;

    H265RingBuffer h265_cache{15000};

    // H265事件录像
    // H265Recorder h265_recorder{&h265_cache};
    std::unique_ptr<H265Recorder> h265_recorder;

    std::atomic<uint64_t> frame_counter{0};
    std::atomic<uint64_t> h265_counter{0};
    std::mutex frame_mutex;

    // 正在写入的临时TS（splitmuxsink 当前片段）
    std::mutex record_mutex;
    std::string current_record_path;

    // 本次运行使用过的 temp/record 目录（跨天/跨目录时都能收尾）
    std::vector<std::string> record_temp_dirs;

    AIFrameCache *ai_cache = nullptr;
    AIFrameCache ai_frame_cache{600};
};