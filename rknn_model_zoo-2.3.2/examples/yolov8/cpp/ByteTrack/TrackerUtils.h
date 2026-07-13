#pragma once

#include <vector>

#include "BYTETracker.h"
#include "roi_manager.h"
#include "yolov8.h"

struct TrackResult {
    int track_id;

    float score;

    float x;
    float y;
    float w;
    float h;

    float cx;
    float cy;

    int roi_id;
};

std::vector<byte_track::Object> convertToByteTrackObjects(const object_detect_result_list &results);

std::vector<TrackResult>
handle_tracks(const std::vector<byte_track::BYTETracker::STrackPtr> &tracks, int cam_id);

void print_tracks(const std::vector<byte_track::BYTETracker::STrackPtr> &tracks, int cam_id);