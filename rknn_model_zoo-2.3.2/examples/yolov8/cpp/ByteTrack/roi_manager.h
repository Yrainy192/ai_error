#pragma once
#include <map>
#include <vector>
#include <string>
#include <mutex>
#include <iostream>

struct ROI {
    int id;
    int x1, y1, x2, y2;
};


class ROIManager {
public:
    bool load(const std::string& path);

    const std::vector<ROI>& get(int cam_id);

    void print();

private:
    std::map<int, std::vector<ROI>> cam_rois;
    std::mutex mutex;
};