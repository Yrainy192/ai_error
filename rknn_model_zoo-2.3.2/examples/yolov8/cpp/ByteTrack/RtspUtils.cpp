#include "RtspUtils.h"

Frame get_frame_from_rtsp(RtspManager &manager, int stream_id) {

    auto stream = manager.get(stream_id);

    if (!stream)
        return {};

    Frame f;

    if (!stream->getLatest(f))
        return {};

    return f;
}