#include "RtspManager.h"

void RtspManager::addStream(const std::string &url, const std::string &cameraId) {

    // 创建唯一AI缓存
    auto ai_cache = std::make_shared<AIFrameCache>(600);

    ai_caches[cameraId] = ai_cache;

    auto stream = std::make_shared<RtspStream>(url, cameraId, ai_cache.get());

    streams.push_back(stream);

    stream->start();
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