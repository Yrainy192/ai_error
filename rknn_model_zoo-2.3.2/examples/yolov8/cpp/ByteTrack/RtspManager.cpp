#include "RtspManager.h"

void RtspManager::addStream(const std::string &url, const std::string &cameraId) {

    auto stream = std::make_shared<RtspStream>(url, cameraId);

    stream->start();

    streams.push_back(stream);
}

void RtspManager::stopAll() {

    for (auto &s : streams) {

        if (s)
            s->stop();
    }

    streams.clear();
}

std::shared_ptr<RtspStream> RtspManager::get(int i) {

    if (i < 0 || i >= static_cast<int>(streams.size())) {
        return nullptr;
    }

    return streams[i];
}