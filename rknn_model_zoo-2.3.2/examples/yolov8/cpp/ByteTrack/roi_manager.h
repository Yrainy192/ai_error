#pragma once
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct ROI {
    int id;
    int x1, y1, x2, y2;                      // 外接矩形（兼容旧格式）
    std::vector<std::pair<int, int>> points; // 多边形顶点（非空 → 用多边形点内判断）
};

class ROIManager {
  public:
    bool load(const std::string &path);

    const std::vector<ROI> &get(int cam_id);

    void print();

  private:
    std::map<int, std::vector<ROI>> cam_rois;
    std::mutex mutex;
};