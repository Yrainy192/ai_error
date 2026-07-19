#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <gst/gst.h>

#include "Frame.h"
#include "H265Recorder.h"
#include "H265RingBuffer.h"
#include "mysql_client.h"

class RtspStream {

  public:
    RtspStream(const std::string &url, const std::string &camera_id);

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

  private:
    void run();

    void buildPipeline();

    std::string makeRecordDir();

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

    std::mutex frame_mutex;
};