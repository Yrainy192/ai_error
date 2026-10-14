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
    /**
     * 保存AI检测结果JSON
     *
     * 先写 <filename>.part，fsync 落盘后再 rename 成 filename，
     * 因此 filename 一旦出现就是完整可解析的JSON。
     *
     * 返回 false 表示保存失败（不会留下半截文件）。
     */
    bool saveAIJson(const std::vector<H265Packet> &video_frames,
                    const std::vector<AIFrame> &ai_frames, const std::string &filename);
};