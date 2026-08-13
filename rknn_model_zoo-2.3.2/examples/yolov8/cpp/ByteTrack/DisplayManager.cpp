#include "DisplayManager.h"

DisplayFrame g_display[6];

void draw_rois(cv::Mat &img, const std::vector<ROI> &rois) {

    for (const auto &r : rois) {

        if (!r.points.empty()) {
            // 多边形
            std::vector<cv::Point> pts;
            pts.reserve(r.points.size());
            for (const auto &p : r.points)
                pts.emplace_back(p.first, p.second);
            cv::polylines(img, pts, true, cv::Scalar(255, 0, 0), 2);
        } else {
            // 矩形（兼容旧格式）
            cv::rectangle(img, cv::Point(r.x1, r.y1), cv::Point(r.x2, r.y2), cv::Scalar(255, 0, 0),
                          2);
        }

        cv::putText(img, std::to_string(r.id), cv::Point(r.x1, r.y1 - 5), cv::FONT_HERSHEY_SIMPLEX,
                    0.6, cv::Scalar(255, 255, 0), 2);
    }
}

void update_display(const image_buffer_t &img, object_detect_result_list &results, int cam_id,
                    const std::vector<ROI> &rois) {

    cv::Mat display(img.height, img.width, CV_8UC3, img.virt_addr);

    cv::cvtColor(display, display, cv::COLOR_RGB2BGR);

    display = display.clone();

    // 检测框

    for (int i = 0; i < results.count; i++) {

        auto *r = &results.results[i];

        cv::rectangle(display, cv::Point(r->box.left, r->box.top),
                      cv::Point(r->box.right, r->box.bottom), cv::Scalar(0, 255, 0), 2);
    }

    // ROI

    draw_rois(display, rois);

    {
        std::lock_guard<std::mutex> lock(g_display[cam_id].mtx);

        g_display[cam_id].img = display;
    }
}