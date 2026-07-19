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
#include "logger.h"
#include "mysql_client.h"
#include "roi_manager.h"
#include "task_manager.h"
#include "voice_manager.h"
#include <spdlog/spdlog.h>
#include <sys/stat.h>
// std::vector<cv::VideoWriter> g_writers(3);
// std::mutex g_writer_mtx;

// void init_video_writers(int width, int height, double fps) {
//     std::lock_guard<std::mutex> lock(g_writer_mtx);

//     for (int i = 0; i < 3; i++) {
//         std::string name =
//             "/home/admin/Rtsp/rknn_model_zoo-2.3.2/video/test_" + std::to_string(i) + ".avi";
//         bool ok = g_writers[i].open(name, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps,
//                                     cv::Size(width, height));

//         std::cout << "[VideoWriter] cam " << i << " open result: " << ok << " file: " << name
//                   << std::endl;
//     }
// }

// void save_camera_frame_to_video(const image_buffer_t &img, int cam_id) {
//     // 原始图像（假设RGB）
//     cv::Mat raw(img.height, img.width, CV_8UC3, img.virt_addr);

//     // 转 BGR（VideoWriter需要）
//     cv::Mat frame;
//     cv::cvtColor(raw, frame, cv::COLOR_RGB2BGR);

//     std::lock_guard<std::mutex> lock(g_writer_mtx);

//     if (cam_id >= 0 && cam_id < g_writers.size()) {
//         if (g_writers[cam_id].isOpened()) {
//             g_writers[cam_id].write(frame);
//         }
//     }
// }

//===============================================已经实现视频存储功能==========================================================//

TaskManager task_manager;
// RtspStream rtsp;
ROIManager roi_manager;
std::ofstream g_log_file;
std::mutex g_log_mutex;

std::unordered_map<int, std::unique_ptr<byte_track::BYTETracker>> tracker_map;

std::vector<std::unique_ptr<RgaWorker>> rga_workers;

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

    while (true) {
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
        auto tracks = tracker.update(objects);
        // print_tracks(tracks,0);
        auto res = handle_tracks(tracks, cam_id);

        std::unordered_map<int, std::vector<int>> roi_map;

        for (auto &r : res) {
            if (r.roi_id != -1) {
                roi_map[r.roi_id].push_back(r.track_id);
            }
        }

        for (auto &kv : roi_map) {
            int roi_id = kv.first;

            for (int id : kv.second) {
                task_manager->updateEnter(cam_id, roi_id, id);
            }
        }

        // 空ROI
        for (auto &roi : roi_manager.get(cam_id)) {
            if (roi_map.find(roi.id) == roi_map.end()) {
                task_manager->updateEnter(cam_id, roi.id, -1);
            }
        }

        task_manager->updateLogic(1);
        update_display(img, results, cam_id, roi_manager.get(cam_id));
    }
}

void start_service() {
    // std::cout << "=== 程序启动 ===" << std::endl;
    spdlog::info("=== 程序启动 ===");
    std::string host = "192.168.60.134";
    std::string user = "admin";
    std::string password = "123456";
    std::string database = "ai_dec";
    int port = 3306;

    try {
        static MysqlClient mysql_client(host, user, password, database, port);
        task_manager.setMysqlClient(&mysql_client);
        // static TCPClient tcp_client("192.168.70.33", 2059, &task_manager, &mysql_client);

        static TCPClient tcp_client("127.0.0.1", 9000, &task_manager, &mysql_client);
        //  启动 TCP线程（后台运行）
        std::thread([]() {
            tcp_client.start();
            // ⚠️保持线程存活（但不阻塞main）
            while (true) {
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

    task_manager.initGridMap();
    roi_manager.load("/home/admin/Rtsp/rknn_model_zoo-2.3.2/examples/yolov8/roi_config.json");

    AudioManager audio;
    audio.start();

    task_manager.setAudioManager(&audio);

    VoiceManager voice_mgr;
    voice_mgr.load(
        "/home/admin/Rtsp/rknn_model_zoo-2.3.2/examples/yolov8/cpp/ByteTrack/voice.json");

    task_manager.setVoiceManager(&voice_mgr);

    SdkManager speaker;

    if (!speaker.init(
            "init -node 300 -ip eth1 -path "
            "/home/admin/Rtsp/rknn_model_zoo-2.3.2/examples/yolov8/cpp/ -debug -noalsa")) {
        std::cout << "init failed\n";
        return 1;
    }
    speaker.start();

    //  =========================
    //  1. 服务初始化
    //  =========================
    //  int width = 640;
    //  int height = 640;
    //  double fps = 25;
    // // ⭐先初始化录像
    // init_video_writers(width, height, fps);
    InitLogger();
    spdlog::info("program start");

    start_service();

    // 2. Tracker 初始化（每路一个）
    tracker_map[0] = std::make_unique<byte_track::BYTETracker>(30, 30, 0.3f, 0.4f, 0.8f);
    tracker_map[1] = std::make_unique<byte_track::BYTETracker>(30, 30, 0.3f, 0.4f, 0.8f);
    tracker_map[2] = std::make_unique<byte_track::BYTETracker>(30, 30, 0.3f, 0.4f, 0.8f);

    // 3. RTSP 管理（不改）
    RtspManager manager;
    // manager.addStream("rtsp://admin:qwerqaz1022@192.168.70.64:554/Streaming/Channels/101");
    // manager.addStream("rtsp://admin:qwerqaz1022@192.168.70.65:554/Streaming/Channels/101");
    manager.addStream("rtsp://192.168.70.12:554/"
                      "user=admin_password=tlJwpbo6_channel=0_stream=0&onvif=0.sdp?real_stream",
                      "cam0");
    manager.addStream("rtsp://192.168.70.10:554/"
                      "user=admin_password=tlJwpbo6_channel=0_stream=0&onvif=0.sdp?real_stream",
                      "cam1");
    manager.addStream("rtsp://192.168.70.11:554/"
                      "user=admin_password=tlJwpbo6_channel=0_stream=0&onvif=0.sdp?real_stream",
                      "cam2");

    for (int i = 0; i < 3; i++) {
        auto stream = manager.get(i);
        if (stream) {
            task_manager.setRecorder(i, stream->getRecorder());
        }
    }

    //  4. RGA worker（⭐关键新增）
    rga_workers.resize(3);
    for (int i = 0; i < 3; i++) {
        rga_workers[i] = std::make_unique<RgaWorker>();
    }

    // 5. 模型池（3 core）
    std::string model_path = "/home/admin/Rtsp/rknn_model_zoo-2.3.2/install/rk3588_linux_aarch64/"
                             "rknn_yolov8_demo/model/yolov8best.rknn";
    ModelPool modelPool;
    modelPool.init(model_path, 3);

    // 6. 启动推理线程（每路一个）
    std::thread t0(infer_worker, 0, 0, &manager, &modelPool, &task_manager);
    std::thread t1(infer_worker, 1, 1, &manager, &modelPool, &task_manager);
    std::thread t2(infer_worker, 2, 2, &manager, &modelPool, &task_manager);

    // 7. UI窗口
    cv::namedWindow("cam0");
    cv::namedWindow("cam1");
    cv::namedWindow("cam2");

    // 8. 显示线程（主线程）
    while (true) {
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
