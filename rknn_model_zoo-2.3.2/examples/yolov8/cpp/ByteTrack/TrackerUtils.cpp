#include "TrackerUtils.h"

#include <cstdio>

extern ROIManager roi_manager;

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

            float pad = 0.25f;

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

        int hit_roi = -1;

        for (const auto &roi : rois) {

            if (cx >= roi.x1 && cx <= roi.x2 && cy >= roi.y1 && cy <= roi.y2) {
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