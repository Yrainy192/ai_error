#include "roi_manager.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

bool ROIManager::load(const std::string& path)
{
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

    for (auto& cam_item : j.items())
    {
        int cam_id = std::stoi(cam_item.key());
        cam_count++;
        for (auto& r : cam_item.value())
        {
            ROI roi;
            roi.id = r["id"];
            roi.x1 = r["x1"];
            roi.y1 = r["y1"];
            roi.x2 = r["x2"];
            roi.y2 = r["y2"];

            cam_rois[cam_id].push_back(roi);
        }
    }
    //std::cout << "[ROI LOAD] cam count = " << cam_count << std::endl;
    spdlog::info(
    "[ROI LOAD] {}",
    cam_count
    );
    return true;
}

const std::vector<ROI>& ROIManager::get(int cam_id)
{
    static std::vector<ROI> empty;

    std::lock_guard<std::mutex> lock(mutex);

    auto it = cam_rois.find(cam_id);
    if (it == cam_rois.end())
        return empty;

    return it->second;
}

void ROIManager::print()
{
    for (auto& [cam, rois] : cam_rois)
    {
        std::cout << "cam " << cam << " rois: " << rois.size() << std::endl;
    }
}