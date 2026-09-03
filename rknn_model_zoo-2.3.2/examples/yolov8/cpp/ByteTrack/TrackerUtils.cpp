#include "TrackerUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>
extern ROIManager roi_manager;

// 鞋带公式：计算多边形面积（结果恒为非负）
static float polygonArea(const std::vector<std::pair<float, float>> &poly) {
    if (poly.size() < 3)
        return 0.0f;
    float sum = 0.0f;
    const size_t n = poly.size();
    for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        sum += poly[i].first * poly[j].second - poly[j].first * poly[i].second;
    }
    return std::fabs(sum) * 0.5f;
}

// Sutherland-Hodgman：用轴对齐矩形窗口 [xmin,xmax]x[ymin,ymax] 裁剪多边形，
// 返回“多边形 ∩ 矩形”的交集多边形顶点（顶点可能为空）
static std::vector<std::pair<float, float>>
clipPolygonByRect(const std::vector<std::pair<float, float>> &subject, float xmin, float ymin,
                  float xmax, float ymax) {
    if (subject.size() < 3 || xmax <= xmin || ymax <= ymin)
        return {};

    auto inside = [&](const std::pair<float, float> &p, int edge) -> bool {
        switch (edge) {
        case 0:
            return p.first >= xmin; // 左边界内侧
        case 1:
            return p.first <= xmax; // 右边界内侧
        case 2:
            return p.second >= ymin; // 下边界内侧
        default:
            return p.second <= ymax; // 上边界内侧
        }
    };
    auto intersect = [&](const std::pair<float, float> &a, const std::pair<float, float> &b,
                         int edge) {
        float t;
        switch (edge) {
        case 0:
            t = (xmin - a.first) / (b.first - a.first);
            break;
        case 1:
            t = (xmax - a.first) / (b.first - a.first);
            break;
        case 2:
            t = (ymin - a.second) / (b.second - a.second);
            break;
        default:
            t = (ymax - a.second) / (b.second - a.second);
            break;
        }
        return std::make_pair(a.first + t * (b.first - a.first),
                              a.second + t * (b.second - a.second));
    };

    std::vector<std::pair<float, float>> input = subject;
    for (int edge = 0; edge < 4; edge++) {
        std::vector<std::pair<float, float>> output;
        const size_t n = input.size();
        for (size_t i = 0; i < n; i++) {
            const auto &cur = input[i];
            const auto &prev = input[(i + n - 1) % n];
            const bool curIn = inside(cur, edge);
            const bool prevIn = inside(prev, edge);
            if (curIn) {
                if (!prevIn)
                    output.push_back(intersect(prev, cur, edge));
                output.push_back(cur);
            } else if (prevIn) {
                output.push_back(intersect(prev, cur, edge));
            }
        }
        if (output.empty())
            return {};
        input.swap(output);
    }
    return input;
}

// 计算轴对齐检测框 (bx1,by1)-(bx2,by2) 与一个 ROI 的相交面积
static float roiOverlapArea(float bx1, float by1, float bx2, float by2, const ROI &roi) {
    if (!roi.points.empty()) {
        // ROI 为多边形：用检测框作为裁剪窗口，求交集多边形面积
        std::vector<std::pair<float, float>> poly;
        poly.reserve(roi.points.size());
        for (const auto &p : roi.points)
            poly.emplace_back(static_cast<float>(p.first), static_cast<float>(p.second));
        return polygonArea(clipPolygonByRect(poly, bx1, by1, bx2, by2));
    }
    // ROI 为矩形（兼容旧格式）：两个轴对齐矩形的交集面积
    const float w =
        std::min(bx2, static_cast<float>(roi.x2)) - std::max(bx1, static_cast<float>(roi.x1));
    const float h =
        std::min(by2, static_cast<float>(roi.y2)) - std::max(by1, static_cast<float>(roi.y1));
    if (w <= 0.0f || h <= 0.0f)
        return 0.0f;
    return w * h;
}

std::vector<byte_track::Object>
convertToByteTrackObjects(const object_detect_result_list &results) {

    std::vector<byte_track::Object> objects;

    objects.reserve(results.count);

    for (int i = 0; i < results.count; i++) {

        const auto &r = results.results[i];

        float x = static_cast<float>(r.box.left);

        float y = static_cast<float>(r.box.top);

        float w = static_cast<float>(r.box.right - r.box.left);

        float h = static_cast<float>(r.box.bottom - r.box.top);

        float tx = x;
        float ty = y;
        float tw = w;
        float th = h;

        float area = w * h;

        if (area < 1500.0f) {

            float pad = 0.10f;

            tx = x - w * pad;
            ty = y - h * pad;

            tw = w * (1 + 2 * pad);
            th = h * (1 + 2 * pad);
        }
        objects.emplace_back(byte_track::Rect<float>(tx, ty, tw, th), r.cls_id, r.prop);
    }

    return objects;
}

std::vector<TrackResult>
handle_tracks(const std::vector<byte_track::BYTETracker::STrackPtr> &tracks, int cam_id) {

    std::vector<TrackResult> results;

    auto &rois = roi_manager.get(cam_id);

    for (auto &t : tracks) {

        const auto &r = t->getRect();

        float x = r.x();
        float y = r.y();

        float w = r.width();
        float h = r.height();

        float cx = x + w * 0.5f;
        float cy = y + h * 0.5f;

        // 包围框与 ROI 的相交面积 ≥ 包围框面积的 30% 即视为进入
        const float kEnterRatio = 0.30f;
        const float box_area = w * h;
        const float bx1 = x, by1 = y;
        const float bx2 = x + w, by2 = y + h;

        int hit_roi = -1;

        for (const auto &roi : rois) {
            const float inter = roiOverlapArea(bx1, by1, bx2, by2, roi);
            if (box_area > 0.0f && inter >= kEnterRatio * box_area) {
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

void print_tracks(const std::vector<byte_track::BYTETracker::STrackPtr> &tracks, int cam_id) {

    printf("\n========== CAM %d TRACK ==========\n", cam_id);

    for (size_t i = 0; i < tracks.size(); i++) {

        auto &t = tracks[i];

        auto &r = t->getRect();

        printf("[%zu] id=%zu score=%.3f box=[%.0f,%.0f,%.0f,%.0f]\n", i, t->getTrackId(),
               t->getScore(), r.x(), r.y(), r.x() + r.width(), r.y() + r.height());
    }
}