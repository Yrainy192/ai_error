#pragma once

#include <mutex>
#include <vector>

#include <opencv2/opencv.hpp>

#include "roi_manager.h"
#include "yolov8.h"

struct DisplayFrame {
    cv::Mat img;
    std::mutex mtx;
};

extern DisplayFrame g_display[3];

void draw_rois(cv::Mat &img, const std::vector<ROI> &rois);

void update_display(const image_buffer_t &img, object_detect_result_list &results, int cam_id,
                    const std::vector<ROI> &rois);