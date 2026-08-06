/*-------------------------------------------
                Includes
-------------------------------------------*/
#include "file_utils.h"
#include "image_drawing.h"
#include "image_utils.h"
#include "rockchip/mpp_frame.h"
#include "rockchip/rk_mpi.h"
#include "yolov8.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
using namespace cv;
#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include "BYTETracker.h"
#include "RgaUtils.h"
#include "im2d.h"
#include "im2d.hpp"
#include "postprocess.h"
#include "rga.h"
#include "rknn_api.h"
#include "tcp_client.h"
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <unistd.h>
#include <vector>
#define RGB_SIZE (640 * 640 * 3)
#include <fstream>
#include <map>
#include <memory>
#include <unordered_map>

#include "AIFrameCache.h"
#include "AlarmLight.h"
#include "ConfigManager.h"
#include "DisplayManager.h"
#include "Frame.h"
#include "H265RingBuffer.h"
#include "ModelPool.h"
#include "RgaWorker.h"
#include "RtspManager.h"
#include "RtspStream.h"
#include "RtspUtils.h"
#include "SdkManager.h"
#include "TrackerUtils.h"
#include "UploadManager.h"
#include "logger.h"
#include "mysql_client.h"
#include "roi_manager.h"
#include "task_manager.h"
#include "voice_manager.h"
#include <atomic>
#include <signal.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>

std::atomic<bool> running(true);
std::unique_ptr<TCPClient> tcp_client;
// 全局配置
ConfigManager config;
Json::Value cfg;

void signalHandler(int signum) {
    spdlog::info("receive signal {}", signum);
    running = false;
    if (tcp_client) {
        tcp_client->stop();
    }
}

TaskManager task_manager;
ROIManager roi_manager;
std::ofstream g_log_file;
std::mutex g_log_mutex;

std::unordered_map<int, std::unique_ptr<byte_track::BYTETracker>> tracker_map;

std::vector<std::unique_ptr<RgaWorker>> rga_workers;

std::vector<uint64_t> last_ai_frame_id;

image_buffer_t frame_to_image_buffer(Frame &f) {
    image_buffer_t img;
    memset(&img, 0, sizeof(img));
    img.width = f.width;
    img.height = f.height;
    img.virt_addr = f.data->data();
    img.format = IMAGE_FORMAT_RGB888;
    return img;
}

void print_results(const object_detect_result_list &results, int cam_id) {
    printf("\n========== CAM %d ==========\n", cam_id);
    printf("count = %d\n", results.count);

    for (int i = 0; i < results.count; i++) {
        const object_detect_result *r = &results.results[i];

        printf("[%d] cls=%d score=%.3f box=[%d,%d,%d,%d]\n", i, r->cls_id, r->prop, r->box.left,
               r->box.top, r->box.right, r->box.bottom);
    }
}

void infer_worker(int cam_id, int model_id, RtspManager *manager, ModelPool *pool,
                  TaskManager *task_manager) {

    auto &tracker = *tracker_map[cam_id];

    while (running) {
        auto &rga = *rga_workers[cam_id];
        Frame raw = get_frame_from_rtsp(*manager, cam_id);

        if (!raw.data)
            continue;
        if (raw.frame_id == last_ai_frame_id[cam_id]) {
            continue;
        }
        last_ai_frame_id[cam_id] = raw.frame_id;
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
        auto tracks = tracker.update(objects);
        auto res = handle_tracks(tracks, cam_id);
        AIFrame ai;
        ai.frame_id = raw.frame_id;
        ai.pts = raw.pts;
        ai.real_time_ms = raw.real_time_ms;
        for (auto &r : res) {
            DetectionInfo d;
            d.track_id = r.track_id;
            d.confidence = r.score;
            d.x = r.x;
            d.y = r.y;
            d.w = r.w;
            d.h = r.h;
            ai.objects.push_back(d);
        }

        std::string cameraId = "cam" + std::to_string(cam_id);

        auto cache = manager->getAICache(cameraId);

        if (cache) {
            cache->push(ai);
            // spdlog::info("AI cache push addr={} frame={}", (void *)cache, ai.frame_id);
        }
        // spdlog::info("AI push frame={} pts={} time={} objects={}", ai.frame_id, ai.pts,
        //              ai.real_time_ms, ai.objects.size());

        std::unordered_map<int, std::vector<int>> roi_map;

        for (auto &r : res) {
            if (r.roi_id != -1) {
                roi_map[r.roi_id].push_back(r.track_id);
            }
        }

        for (auto &kv : roi_map) {
            int roi_id = kv.first;

            for (int id : kv.second) {
                task_manager->updateEnter(cam_id, roi_id, id, raw.pts);
            }
        }

        // 空ROI
        for (auto &roi : roi_manager.get(cam_id)) {
            if (roi_map.find(roi.id) == roi_map.end()) {
                task_manager->updateEnter(cam_id, roi.id, -1, raw.pts);
            }
        }

        task_manager->updateLogic(3);
        // update_display(img, results, cam_id, roi_manager.get(cam_id));
    }
}

void start_service() {
    // std::cout << "=== 程序启动 ===" << std::endl;
    spdlog::info("=== 程序启动 ===");
    // std::string host = "192.168.60.131";
    // std::string user = "admin";
    // std::string password = "123456";
    // std::string database = "ai_dec";
    // int port = 3306;
    auto mysql = cfg["mysql"];

    std::string host = mysql["host"].asString();

    std::string user = mysql["user"].asString();

    std::string password = mysql["password"].asString();

    std::string database = mysql["database"].asString();

    int port = mysql["port"].asInt();
    try {
        static MysqlClient mysql_client(host, user, password, database, port);
        task_manager.setMysqlClient(&mysql_client);

        auto tcp = cfg["tcp"];
        tcp_client = std::make_unique<TCPClient>(
            tcp["server_ip"].asString(), tcp["server_port"].asInt(), &task_manager, &mysql_client);
        // tcp_client =
        //     std::make_unique<TCPClient>("192.168.70.33", 2059, &task_manager, &mysql_client);
        // tcp_client = std::make_unique<TCPClient>("127.0.0.1", 9000, &task_manager,
        // &mysql_client); static TCPClient tcp_client("127.0.0.1", 9000, &task_manager,
        // &mysql_client);
        //   启动 TCP线程（后台运行）
        std::thread([]() {
            tcp_client->start();
            // ⚠️保持线程存活（但不阻塞main）
            while (running) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }).detach();
        // std::cout << "✅ TCP服务已后台启动" << std::endl;
        spdlog::info("TCP服务已后台启动");
    } catch (const std::exception &e) {
        // std::cerr << "❌ 启动失败: " << e.what() << std::endl;
        spdlog::error("启动失败: {}", e.what());
    }
}

int main() {

    InitLogger();

    if (!config.load("/home/admin/Rtsp_6model/rknn_model_zoo-2.3.2/examples/yolov8/config.json")) {
        spdlog::error("config load failed");
        return -1;
    }
    cfg = config.get();
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    spdlog::info("program start");

    auto alarm_cfg = cfg["alarm_light"];

    std::string alarm_ip = alarm_cfg["ip"].asString();

    int alarm_port = alarm_cfg["port"].asInt();

    AlarmLight alarm(alarm_ip, alarm_port);

    if (!alarm.connect()) {

        spdlog::error("alarm light connect failed {}:{}", alarm_ip, alarm_port);
    }

    // AlarmLight alarm("192.168.70.8", 23);
    // if (!alarm.connect()) {
    //     spdlog::error("alarm light connect failed");
    // };

    task_manager.setAlarmLight(&alarm);

    task_manager.initGridMap();

    roi_manager.load(
        "/home/admin/Rtsp_6model/rknn_model_zoo-2.3.2/examples/yolov8/roi_config.json");

    AudioManager audio;
    audio.start();

    task_manager.setAudioManager(&audio);

    VoiceManager voice_mgr;
    voice_mgr.load("/home/admin/Rtsp_6model/rknn_model_zoo-2.3.2/examples/yolov8/voice.json");

    task_manager.setVoiceManager(&voice_mgr);

    SdkManager speaker;

    if (!speaker.init(
            "init -node 300 -ip eth1 -path "
            "/home/admin/Rtsp_6model/rknn_model_zoo-2.3.2/examples/yolov8/cpp/ -debug -noalsa")) {
        std::cout << "init failed\n";
        return 1;
    }
    speaker.start();

    //  =========================
    //  1. 服务初始化
    //  =========================
    start_service();
    // 2. Tracker 初始化（每路一个）
    tracker_map[0] = std::make_unique<byte_track::BYTETracker>(30, 30, 0.3f, 0.4f, 0.8f);
    tracker_map[1] = std::make_unique<byte_track::BYTETracker>(30, 30, 0.3f, 0.4f, 0.8f);
    tracker_map[2] = std::make_unique<byte_track::BYTETracker>(30, 30, 0.3f, 0.4f, 0.8f);
    tracker_map[3] = std::make_unique<byte_track::BYTETracker>(30, 30, 0.3f, 0.4f, 0.8f);
    tracker_map[4] = std::make_unique<byte_track::BYTETracker>(30, 30, 0.3f, 0.4f, 0.8f);
    // tracker_map[5] = std::make_unique<byte_track::BYTETracker>(30, 30, 0.3f, 0.4f, 0.8f);
    //  3. RTSP 管理（不改）
    RtspManager manager;
    auto cameras = cfg["camera"];
    for (auto cam : cameras) {
        int id = cam["id"].asInt();
        manager.addStream(cam["url"].asString(), cam["name"].asString());
    }
    // manager.addStream("rtsp://192.168.70.12:554/"
    //                   "user=admin_password=tlJwpbo6_channel=0_stream=0&onvif=0.sdp?real_stream",
    //                   "cam0");
    // manager.addStream("rtsp://admin:qwerqaz1022@192.168.70.66:554/Streaming/Channels/101",
    // "cam1");
    // manager.addStream("rtsp://admin:qwerqaz1022@192.168.70.65:554/Streaming/Channels/101",
    // "cam2"); manager.addStream("rtsp://192.168.70.10:554/"
    //                   "user=admin_password=tlJwpbo6_channel=0_stream=0&onvif=0.sdp?real_stream",
    //                   "cam1");

    // manager.addStream("rtsp://192.168.70.11:554/"
    //                   "user=admin_password=tlJwpbo6_channel=0_stream=0&onvif=0.sdp?real_stream",
    //                   "cam2");

    // manager.addStream("rtsp://admin:qwerqaz1022@192.168.70.64:554/Streaming/Channels/101",
    // "cam3");

    for (int i = 0; i < 5; i++) {
        auto stream = manager.get(i);
        if (stream) {
            task_manager.setRecorder(i, stream->getRecorder());
        }
    }

    //  4. RGA worker（⭐关键新增）
    rga_workers.resize(5);
    for (int i = 0; i < 5; i++) {
        rga_workers[i] = std::make_unique<RgaWorker>();
    }
    last_ai_frame_id.resize(5, UINT64_MAX);

    //    // 5. 模型池（6 core）
    //     std::string model_path =
    //     "/home/admin/Rtsp_6model/rknn_model_zoo-2.3.2/examples/yolov8/model/"
    //                              "yolov8_new.rknn";

    //     ModelPool modelPool;
    //     modelPool.init(model_path, 5);

    std::string model_path = cfg["model"]["path"].asString();

    int model_count = cfg["model"]["count"].asInt();

    ModelPool modelPool;
    modelPool.init(model_path, model_count);
    // 6. 启动推理线程（每路一个）
    std::thread t0(infer_worker, 0, 0, &manager, &modelPool, &task_manager);
    std::thread t1(infer_worker, 1, 1, &manager, &modelPool, &task_manager);
    std::thread t2(infer_worker, 2, 2, &manager, &modelPool, &task_manager);
    std::thread t3(infer_worker, 3, 3, &manager, &modelPool, &task_manager);
    std::thread t4(infer_worker, 4, 4, &manager, &modelPool, &task_manager);
    // std::thread t5(infer_worker, 5, 5, &manager, &modelPool, &task_manager);

    // // // 7. UI窗口
    // cv::namedWindow("cam0");
    // cv::namedWindow("cam1");
    // cv::namedWindow("cam2");
    // cv::namedWindow("cam3");
    // cv::namedWindow("cam4");
    // cv::namedWindow("cam5");
    // // 8. 显示线程（主线程）
    // while (running) {
    //     {
    //         std::lock_guard<std::mutex> lock(g_display[0].mtx);
    //         if (!g_display[0].img.empty())
    //             cv::imshow("cam0", g_display[0].img);
    //     }

    //     {
    //         std::lock_guard<std::mutex> lock(g_display[1].mtx);
    //         if (!g_display[1].img.empty())
    //             cv::imshow("cam1", g_display[1].img);
    //     }
    //     {
    //         std::lock_guard<std::mutex> lock(g_display[2].mtx);
    //         if (!g_display[2].img.empty())
    //             cv::imshow("cam2", g_display[2].img);
    //     }
    //     // {
    //     //     std::lock_guard<std::mutex> lock(g_display[3].mtx);
    //     //     if (!g_display[3].img.empty())
    //     //         cv::imshow("cam3", g_display[3].img);
    //     // }
    //     // {
    //     //     std::lock_guard<std::mutex> lock(g_display[4].mtx);
    //     //     if (!g_display[4].img.empty())
    //     //         cv::imshow("cam4", g_display[4].img);
    //     // }
    //     // {
    //     //     std::lock_guard<std::mutex> lock(g_display[5].mtx);
    //     //     if (!g_display[5].img.empty())
    //     //         cv::imshow("cam5", g_display[5].img);
    //     // }
    //     if (cv::waitKey(1) == 27)
    //         break;
    // }

    // running = false;
    // if (tcp_client) {
    //     tcp_client->stop();
    // }

    //等待推理线程退出
    t0.join();
    t1.join();
    t2.join();
    t3.join();
    t4.join();

    return 0;
}
