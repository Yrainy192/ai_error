// #pragma once

// #include <string>
// #include <vector>

// #include "AIFrameCache.h"

// class AIJsonRecorder {

//   public:
//     AIJsonRecorder() = default;

//     /**
//      * 保存AI检测结果JSON
//      *
//      * frames:
//      *      AIFrame缓存数据
//      *
//      * filename:
//      *      json完整路径
//      *
//      */
//     // bool saveAIJson(const std::vector<AIFrame> &frames, const std::string &filename);

//     void saveAIJson(const std::vector<H265Packet> &video, const std::vector<AIFrame> &ai,
//                     const std::string &filename);
// };

#pragma once

#include "AIFrameCache.h"
#include "H265RingBuffer.h"

#include <string>
#include <vector>

class AIJsonRecorder {
  public:
    void saveAIJson(const std::vector<H265Packet> &video_frames,
                    const std::vector<AIFrame> &ai_frames, const std::string &filename);
};