#pragma once

#include <memory>
#include <string>
#include <vector>

#include "RtspStream.h"

class RtspManager {

  public:
    void addStream(const std::string &url, const std::string &cameraId);

    void stopAll();

    std::shared_ptr<RtspStream> get(int i);


  private:
    std::vector<std::shared_ptr<RtspStream>> streams;

};
