#pragma once

#include <memory>
#include <string>
#include <vector>

//#include "H265RingBuffer.h"
#include "RtspStream.h"

class RtspManager {

  public:
    void addStream(const std::string &url, const std::string &cameraId);

    void stopAll();

    std::shared_ptr<RtspStream> get(int i);

    // H265RingBuffer *getH265Cache() { return &h265_cache; }

  private:
    std::vector<std::shared_ptr<RtspStream>> streams;

    // H265RingBuffer h265_cache{15000};

    // GstElement *h265_sink = nullptr;
};
