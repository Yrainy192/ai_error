// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <chrono>
#include "yolov8.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"
#include <iostream>
#include <thread>
#include <atomic>
#include "rockchip/rk_mpi.h"
#include "rockchip/mpp_frame.h"
#include <opencv2/opencv.hpp>
using namespace cv;
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <ctime>
#include <cstring>
#include "im2d.hpp"
#include "RgaUtils.h"
#include "im2d.h"
#include "rga.h"
#include <vector>
#include <condition_variable>
#include "rknn_api.h"
#include "postprocess.h"
#include <string>
#include <queue>
#include <mutex>
#include <unistd.h>
#include <functional>
#include "BYTETracker.h"
#include "tcp_client.h"
#define RGB_SIZE (640 * 640 * 3)
#include <map>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <chrono>
#include <ctime>
#include "mysql_client.h"
#include "task_manager.h"
#include <map>
#include <vector>
#include "roi_manager.h"
#include <mutex>

static std::mutex rga_mutex;

TaskManager task_manager;
ROIManager roi_manager;
std::ofstream g_log_file;
std::mutex g_log_mutex;

std::unordered_map<int, std::unique_ptr<byte_track::BYTETracker>> tracker_map;
using namespace byte_track;
struct Frame {
    std::shared_ptr<std::vector<uint8_t>> data;
    int width;
    int height;
    int64_t timestamp;
};

struct LetterBox {
    int new_w;
    int new_h;
    int pad_x;
    int pad_y;
    float scale;
};
class RgaWorker;
std::vector<std::unique_ptr<RgaWorker>> rga_workers;


//============================================ByteTracker+id追踪===================================
//全局 tracker（也可以封装成类）
std::vector<byte_track::Object>
convertToByteTrackObjects(const object_detect_result_list& results)
{
    std::vector<byte_track::Object> objects;
    objects.reserve(results.count);

    for (int i = 0; i < results.count; ++i)
    {
        const auto& r = results.results[i];

        objects.emplace_back(
            byte_track::Rect<float>(
                static_cast<float>(r.box.left),
                static_cast<float>(r.box.top),
                static_cast<float>(r.box.right - r.box.left),
                static_cast<float>(r.box.bottom - r.box.top)
            ),
            r.cls_id,
            r.prop
        );
    }

    return objects;
}

//===============================================模型池============================================
typedef struct
{
    guint64 t_capture; // 拿到帧时间
    guint64 t_infer_start;
    guint64 t_infer_end;
} FrameMeta;

struct ModelItem
{
    rknn_app_context_t ctx;
    int core_id;
    ModelItem() { memset(&ctx, 0, sizeof(ctx)); }
};

class ModelPool
{
public:
    int init(const std::string &model_path, int num_models)
    {
        models.resize(num_models);

        for (int i = 0; i < num_models; i++)
        {
            models[i] = std::make_shared<ModelItem>();
            models[i]->core_id = i;

            int ret = init_yolov8_model(
                model_path.c_str(),
                &models[i]->ctx,
                i // core_id
            );

            if (ret != 0)
            {
                printf("init model %d failed\n", i);
                return -1;
            }

            // printf("model %d bind core %d ctx=%p\n",
            //        i, i, models[i]->ctx.rknn_ctx);
        }

        return 0;
    }

    int infer(int model_id,
              image_buffer_t *img,
              object_detect_result_list *results)
    {
        if (model_id < 0 || model_id >= models.size())
            return -1;

        auto &model = models[model_id];

        if (!model || model->ctx.rknn_ctx == 0)
        {
            // printf("invalid ctx %d\n", model_id);
            return -1;
        }

        return inference_yolov8_model(
            &model->ctx,
            img,
            results);
    }
    void release()
    {
        for (auto &m : models)
        {
            if (m && m->ctx.rknn_ctx)
            {
                release_yolov8_model(&m->ctx);
                m->ctx.rknn_ctx = 0;
            }
        }
        models.clear();
    }
    int size()
    {
        return models.size();
    }

private:
    std::vector<std::shared_ptr<ModelItem>> models;
};


//======================================RGA线程池================================

static LetterBox get_letterbox(int w, int h, int dst_w, int dst_h)
{
    LetterBox lb;

    float r = std::min((float)dst_w / w, (float)dst_h / h);

    lb.scale = r;
    lb.new_w = (int)(w * r);
    lb.new_h = (int)(h * r);
    lb.pad_x = (dst_w - lb.new_w) / 2;
    lb.pad_y = (dst_h - lb.new_h) / 2;

    return lb;
}

class RgaWorker
{
public:
   bool process(const Frame &f, image_buffer_t &out)
{
    auto rgb = std::make_shared<std::vector<uint8_t>>();
    rgb->resize(640 * 640 * 3);

    LetterBox lb = get_letterbox(f.width, f.height, 640, 640);

    memset(rgb->data(), 0, rgb->size());

    rga_buffer_t src = wrapbuffer_virtualaddr(
        f.data->data(),
        f.width,
        f.height,
        RK_FORMAT_YCbCr_420_SP);

    rga_buffer_t dst = wrapbuffer_virtualaddr(
        rgb->data(),
        640,
        640,
        RK_FORMAT_RGB_888);

    im_rect srect = {0, 0, f.width, f.height};
    im_rect drect = {lb.pad_x, lb.pad_y, lb.new_w, lb.new_h};

    // ✅ ⭐ 就加这一段（关键）
    {
        std::lock_guard<std::mutex> lock(rga_mutex);

        int ret = improcess(src, dst, {},
                             srect, drect, {},
                             0, nullptr, nullptr, 0);

        if (ret != IM_STATUS_SUCCESS)
            return false;
    }

    out.width = 640;
    out.height = 640;
    out.size = 640 * 640 * 3;
    out.format = IMAGE_FORMAT_RGB888;
    out.virt_addr = rgb->data();

    cache = rgb;
    return true;
}

private:
    std::shared_ptr<std::vector<uint8_t>> cache;
};


class RtspStream
{
public:
    RtspStream(const std::string &url)
        : rtsp_url(url) {}

    ~RtspStream()
    {
        stop();
    }

    bool start()
    {
        running = true;
        worker = std::thread(&RtspStream::run, this);
        return true;
    }

    void stop()
    {
        running = false;

        if (worker.joinable())
            worker.join();

        if (pipeline)
        {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
            pipeline = nullptr;
        }
    }

    // =========================
    // 获取最新 NV12 帧
    // =========================
    bool getLatest(Frame &out)
    {
        std::lock_guard<std::mutex> lock(frame_mutex);
        if (!latest_frame.data)
            return false;

        out = latest_frame;
        return true;
    }

private:
    void run()
    {
        gst_init(nullptr, nullptr);
        buildPipeline();

        if (!pipeline)
        {
            std::cerr << "pipeline create failed\n";
            return;
        }

        GstElement *sink =
            gst_bin_get_by_name(GST_BIN(pipeline), "sink");

        g_signal_connect(sink, "new-sample",
                         G_CALLBACK(on_new_sample_static), this);

        gst_element_set_state(pipeline, GST_STATE_PLAYING);

        while (running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void buildPipeline()
    {
        std::string pipeline_str =
            "rtspsrc location=" + rtsp_url +
            " latency=100 protocols=tcp ! "
            "queue ! "
            "rtph265depay ! "
            "h265parse ! "
            "mppvideodec ! "
            "video/x-raw,format=NV12 ! "
            "appsink name=sink emit-signals=true sync=false drop=true max-buffers=1";

        GError *error = nullptr;
        pipeline = gst_parse_launch(pipeline_str.c_str(), &error);

        if (error)
        {
            std::cerr << "pipeline error: " << error->message << std::endl;
        }
    }

    static GstFlowReturn on_new_sample_static(
        GstElement *sink,
        gpointer user_data)
    {
        return static_cast<RtspStream *>(user_data)
            ->on_new_sample(sink);
    }

    // =========================
    // ⭐核心：只取 NV12，不处理
    // =========================
    GstFlowReturn on_new_sample(GstElement *sink)
    {
        GstSample *sample = nullptr;
        g_signal_emit_by_name(sink, "pull-sample", &sample);

        if (!sample)
            return GST_FLOW_ERROR;

        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstMapInfo map;

        if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
        {
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
        f.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();

        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            latest_frame = f;
        }

        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);

        return GST_FLOW_OK;
    }

private:
    std::string rtsp_url;
    GstElement *pipeline = nullptr;
    std::thread worker;
    std::atomic<bool> running{false};

    std::mutex frame_mutex;
    Frame latest_frame;
};



class RtspManager
{
public:
    void addStream(const std::string &url)
    {
        auto stream = std::make_shared<RtspStream>(url);
        stream->start();
        streams.push_back(stream);
    }

    void stopAll()
    {
        for (auto &s : streams)
            s->stop();

        streams.clear();
    }

    std::shared_ptr<RtspStream> get(int i)
    {
        if (i < 0 || i >= streams.size())
            return nullptr;

        return streams[i];
    }
private:
    std::vector<std::shared_ptr<RtspStream>> streams;
};

Frame get_frame_from_rtsp(RtspManager &manager, int stream_id)
{
    auto stream = manager.get(stream_id);
    if (!stream)
        return {};

    Frame f;
    if (!stream->getLatest(f))
        return {};

    return f;
}

image_buffer_t frame_to_image_buffer(Frame &f)
{
    image_buffer_t img;
    memset(&img, 0, sizeof(img));

    img.width = f.width;
    img.height = f.height;

    img.virt_addr = f.data->data();

    img.format = IMAGE_FORMAT_RGB888;

    return img;
}

void print_results(const object_detect_result_list &results, int cam_id)
{
    printf("\n========== CAM %d ==========\n", cam_id);
    printf("count = %d\n", results.count);

    for (int i = 0; i < results.count; i++)
    {
        const object_detect_result *r = &results.results[i];

        printf("[%d] cls=%d score=%.3f box=[%d,%d,%d,%d]\n",
               i,
               r->cls_id,
               r->prop,
               r->box.left,
               r->box.top,
               r->box.right,
               r->box.bottom);
    }
}
// =========================
// 画框（调试用）
// =========================
void draw_result(const image_buffer_t &img,
                 object_detect_result_list &results,
                 int cam_id)
{
    cv::Mat show(img.height,
                 img.width,
                 CV_8UC3,
                 img.virt_addr);
    for (int i = 0; i < results.count; i++)
    {
        object_detect_result *r = &results.results[i];
        cv::rectangle(show,
                      cv::Point(r->box.left, r->box.top),
                      cv::Point(r->box.right, r->box.bottom),
                      cv::Scalar(0, 255, 0),
                      2);
        cv::putText(show,
                    std::to_string(r->cls_id),
                    cv::Point(r->box.left, r->box.top - 5),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.6,
                    cv::Scalar(0, 0, 255),
                    2);
    }
    cv::putText(show,
                "CAM " + std::to_string(cam_id),
                cv::Point(20, 30),
                cv::FONT_HERSHEY_SIMPLEX,
                1.0,
                cv::Scalar(255, 0, 0),
                2);
    cv::imshow("YOLO Engine", show);
    cv::waitKey(1);
}

struct DisplayFrame
{
    cv::Mat img;
    std::mutex mtx;
};

void draw_rois(cv::Mat& img, const std::vector<ROI>& rois)
{
    for (const auto& r : rois)
    {   
        cv::rectangle(img,
                      cv::Point(r.x1, r.y1),
                      cv::Point(r.x2, r.y2),
                      cv::Scalar(255, 0, 0),
                      2);

        cv::putText(img,
                    std::to_string(r.id),
                    cv::Point(r.x1, r.y1 - 5),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.6,
                    cv::Scalar(255,255,0),
                    2);
    }
}

DisplayFrame g_display[3];
void update_display(const image_buffer_t &img,
                    object_detect_result_list &results,
                    int cam_id,
                    const std::vector<ROI>& rois)
{
    cv::Mat display(img.height,
                    img.width,
                    CV_8UC3,
                    img.virt_addr);
    cv::cvtColor(display, display, cv::COLOR_RGB2BGR);
    display = display.clone();

    // 1. 画检测框
    for (int i = 0; i < results.count; i++)
    {
        auto *r = &results.results[i];

        cv::rectangle(display,
                      cv::Point(r->box.left, r->box.top),
                      cv::Point(r->box.right, r->box.bottom),
                      cv::Scalar(0,255,0), 2);
    }

    // 2. ⭐画 ROI（关键）
    draw_rois(display, rois);

    // 3. 更新显示
    {
        std::lock_guard<std::mutex> lock(g_display[cam_id].mtx);
        g_display[cam_id].img = display;
    }
}

void print_tracks(const std::vector<byte_track::BYTETracker::STrackPtr>& tracks,
                  int cam_id)
{
    printf("\n========== CAM %d TRACK ==========\n", cam_id);

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& t = tracks[i];
        const auto& r = t->getRect();

        float x = r.x();
        float y = r.y();
        float w = r.width();
        float h = r.height();
        // float cx = x + w * 0.5f;
        // float cy = y + h * 0.5f;


        printf("[%zu] id=%zu score=%.3f box=[%d,%d,%d,%d]\n",
               i,
               t->getTrackId(),
               t->getScore(),
               (int)x,
               (int)y,
               (int)(x + w),
               (int)(y + h));
    }
}

struct TrackResult
{
    int track_id;
    float score;

    float x, y, w, h;
    float cx, cy;

    int roi_id;
};

std::vector<TrackResult>
handle_tracks(const std::vector<byte_track::BYTETracker::STrackPtr>& tracks,
              int cam_id)
{
    std::vector<TrackResult> results;

    auto& rois = roi_manager.get(cam_id);

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& t = tracks[i];
        const auto& r = t->getRect();
        float x = r.x();
        float y = r.y();
        float w = r.width();
        float h = r.height();
        float cx = x + w * 0.5f;
        float cy = y + h * 0.5f;
        int hit_roi = -1;
        for (const auto& roi : rois)
        {
            if (cx >= roi.x1 && cx <= roi.x2 &&
                cy >= roi.y1 && cy <= roi.y2)
            {
                hit_roi = roi.id;
                break;
            }
        }
        TrackResult tr;
        tr.track_id = t->getTrackId();
        tr.score = t->getScore();
        tr.x = x;
        tr.y = y;
        tr.w = w;
        tr.h = h;
        tr.cx = cx;
        tr.cy = cy;
        tr.roi_id = hit_roi;
        results.push_back(tr);
    }
    return results;
}

void infer_worker(int cam_id,
                  int model_id,
                  RtspManager *manager,
                  ModelPool *pool,
                  TaskManager *task_manager)
{   
   
    auto& tracker = *tracker_map[cam_id];
 
    while (true)
    {
    auto &rga = *rga_workers[cam_id];
    Frame raw = get_frame_from_rtsp(*manager, cam_id);
    if (!raw.data)
        continue;
    // image_buffer_t img = frame_to_image_buffer(raw);

    // // =========================
    //     // ⭐RGA（必须独立worker）
    //     // =========================
    image_buffer_t img;
    if (!rga.process(raw, img))
            continue;

    object_detect_result_list results{};
    pool->infer(model_id, &img, &results);

    auto objects = convertToByteTrackObjects(results);
    auto tracks  = tracker.update(objects);

    auto res = handle_tracks(tracks, cam_id);

    std::unordered_map<int, std::vector<int>> roi_map;

    for (auto &r : res)
    {
        if (r.roi_id != -1)
        {
            roi_map[r.roi_id].push_back(r.track_id);
        }
    }

    for (auto &kv : roi_map)
    {
        int roi_id = kv.first;

        for (int id : kv.second)
        {
            task_manager->updateEnter(cam_id, roi_id, id);
        }
    }

    // 空ROI
    for (auto &roi : roi_manager.get(cam_id))
    {
        if (roi_map.find(roi.id) == roi_map.end())
        {
            task_manager->updateEnter(cam_id, roi.id, -1);
        }
    }

    task_manager->updateLogic(3);

    update_display(img, results, cam_id, roi_manager.get(cam_id));
}
}


void start_service()
{
    std::cout << "=== 程序启动 ===" << std::endl;
    std::string host = "192.168.60.126";
    std::string user = "admin";
    std::string password = "123456";
    std::string database = "ai_dec";
    int port = 3306;

    try {
        static MysqlClient mysql_client(
            host, user, password, database, port
        );
        static TCPClient tcp_client(
            "127.0.0.1",
            9000,
            &task_manager,
            &mysql_client
        );
        // 启动 TCP线程（后台运行）
        std::thread([](){
            tcp_client.start();
            // ⚠️保持线程存活（但不阻塞main）
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }).detach();
        std::cout << "✅ TCP服务已后台启动" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "❌ 启动失败: " << e.what() << std::endl;
    }
}


int main()
{
    // =========================
    // 1. 服务初始化
    // =========================

    start_service();

    task_manager.initGridMap();
    roi_manager.load("/home/admin/Rtsp/rknn_model_zoo-2.3.2/examples/yolov8/roi_config.json");

    // =========================
    // 2. Tracker 初始化（每路一个）
    // =========================
    tracker_map[0] = std::make_unique<byte_track::BYTETracker>(30, 30, 0.3f, 0.4f, 0.8f);
    tracker_map[1] = std::make_unique<byte_track::BYTETracker>(30, 30, 0.3f, 0.4f, 0.8f);
    tracker_map[2] = std::make_unique<byte_track::BYTETracker>(30, 30, 0.3f, 0.4f, 0.8f);

    // =========================
    // 3. RTSP 管理（不改）
    // =========================
    RtspManager manager;

    // manager.addStream("rtsp://admin:qwerqaz1022@192.168.70.64:554/Streaming/Channels/101");
    // manager.addStream("rtsp://admin:qwerqaz1022@192.168.70.65:554/Streaming/Channels/101");
    manager.addStream("rtsp://192.168.70.11:554/user=admin_password=tlJwpbo6_channel=0_stream=0&onvif=0.sdp?real_stream");
    manager.addStream("rtsp://192.168.70.10:554/user=admin_password=tlJwpbo6_channel=0_stream=0&onvif=0.sdp?real_stream");

    manager.addStream("rtsp://192.168.70.12:554/user=admin_password=tlJwpbo6_channel=0_stream=0&onvif=0.sdp?real_stream");
    // =========================
    // 4. RGA worker（⭐关键新增）
    // =========================
    rga_workers.resize(3);
    for (int i = 0; i < 3; i++)
    {
        rga_workers[i] = std::make_unique<RgaWorker>();
    }

    // =========================
    // 5. 模型池（3 core）
    // =========================
    std::string model_path =
        "/home/admin/Rtsp/rknn_model_zoo-2.3.2/install/rk3588_linux_aarch64/rknn_yolov8_demo/model/yolobest.rknn";

    ModelPool modelPool;
    modelPool.init(model_path, 3);

    // =========================
    // 6. 启动推理线程（每路一个）
    // =========================
    std::thread t0(infer_worker, 0, 0, &manager, &modelPool, &task_manager);
    std::thread t1(infer_worker, 1, 1, &manager, &modelPool, &task_manager);
    std::thread t2(infer_worker, 2, 2, &manager, &modelPool, &task_manager);

    // =========================
    // 7. UI窗口
    // =========================
    cv::namedWindow("cam0");
    cv::namedWindow("cam1");
    cv::namedWindow("cam2");

    // =========================
    // 8. 显示线程（主线程）
    // =========================
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(g_display[0].mtx);
            if (!g_display[0].img.empty())
                cv::imshow("cam0", g_display[0].img);
        }

        {
            std::lock_guard<std::mutex> lock(g_display[1].mtx);
            if (!g_display[1].img.empty())
                cv::imshow("cam1", g_display[1].img);
        }

        {
            std::lock_guard<std::mutex> lock(g_display[2].mtx);
            if (!g_display[2].img.empty())
                cv::imshow("cam2", g_display[2].img);
        }

        if (cv::waitKey(1) == 27)
            break;
    }

    // =========================
    // 9. 线程回收
    // =========================
    t0.join();
    t1.join();
    t2.join();

    manager.stopAll();

    return 0;
}




