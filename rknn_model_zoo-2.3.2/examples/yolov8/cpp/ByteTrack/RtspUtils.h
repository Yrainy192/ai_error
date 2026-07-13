#pragma once

#include "Frame.h"
#include "RtspManager.h"

Frame get_frame_from_rtsp(RtspManager &manager, int stream_id);