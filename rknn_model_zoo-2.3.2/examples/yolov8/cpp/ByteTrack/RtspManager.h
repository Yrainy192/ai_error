#pragma once

#include <memory>
#include <string>
#include <vector>

#include "AIFrameCache.h"
#include "RtspStream.h"
#include <unordered_map>
class RtspManager {

  public:
    void addStream(const std::string &url, const std::string &cameraId);

    void stopAll();

    std::shared_ptr<RtspStream> get(int i);

    AIFrameCache *getAICache(const std::string &cam_id) {
        auto it = ai_caches.find(cam_id);
        if (it == ai_caches.end()) {
            return nullptr;
        }
        return it->second.get();
    }

  private:
    std::vector<std::shared_ptr<RtspStream>> streams;

    std::unordered_map<std::string, std::shared_ptr<AIFrameCache>> ai_caches;
};
