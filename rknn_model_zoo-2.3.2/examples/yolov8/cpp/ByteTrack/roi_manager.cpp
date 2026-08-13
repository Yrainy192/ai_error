#include "roi_manager.h"
#include <algorithm>
#include <climits>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

bool ROIManager::load(const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[ROI] open failed: " << path << std::endl;
        return false;
    }

    json j;
    f >> j;
    std::lock_guard<std::mutex> lock(mutex);
    cam_rois.clear();
    int cam_count = 0;

    for (auto &cam_item : j.items()) {
        int cam_id = std::stoi(cam_item.key());
        cam_count++;
        for (auto &r : cam_item.value()) {
            ROI roi;
            roi.id = r["id"];

            // 新格式：多边形顶点 points: [[x,y], ...]
            if (r.contains("points") && r["points"].is_array() && !r["points"].empty()) {
                for (auto &p : r["points"]) {
                    roi.points.emplace_back(p[0].get<int>(), p[1].get<int>());
                }

                // 由顶点计算外接矩形（x1..y2 仍填充，兼容旧格式读取方）
                int x1 = INT_MAX, y1 = INT_MAX, x2 = INT_MIN, y2 = INT_MIN;
                for (auto &[px, py] : roi.points) {
                    x1 = std::min(x1, px);
                    y1 = std::min(y1, py);
                    x2 = std::max(x2, px);
                    y2 = std::max(y2, py);
                }
                roi.x1 = x1;
                roi.y1 = y1;
                roi.x2 = x2;
                roi.y2 = y2;
            } else {
                // 旧格式：矩形
                roi.x1 = r["x1"];
                roi.y1 = r["y1"];
                roi.x2 = r["x2"];
                roi.y2 = r["y2"];
            }

            cam_rois[cam_id].push_back(roi);
        }
    }
    // std::cout << "[ROI LOAD] cam count = " << cam_count << std::endl;
    spdlog::info("[ROI LOAD] {}", cam_count);
    return true;
}

const std::vector<ROI> &ROIManager::get(int cam_id) {
    static std::vector<ROI> empty;

    std::lock_guard<std::mutex> lock(mutex);

    auto it = cam_rois.find(cam_id);
    if (it == cam_rois.end())
        return empty;

    return it->second;
}

void ROIManager::print() {
    for (auto &[cam, rois] : cam_rois) {
        std::cout << "cam " << cam << " rois: " << rois.size() << std::endl;
    }
}