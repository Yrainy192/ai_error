// #pragma once

// #include "H265RingBuffer.h"

// #include "AIJsonRecorder.h"
// #include "VideoType.h"
// #include "mysql_client.h"
// #include <mutex>
// #include <string>
// #include <vector>

// class H265Recorder {

//   public:
//     H265Recorder(const std::string &id, H265RingBuffer *cache) : camera_id(id), cache(cache) {
//         ai_json_recorder = std::make_shared<AIJsonRecorder>();
//     }

//     void setMysqlClient(MysqlClient *db);

//     void saveEvent(uint64_t event_time_ms, const std::string &code, VideoType type);

//     void saveAIError(uint64_t event_time_ms, const std::string &grid, const std::string &id);

//   private:
//     void savePackets(const std::vector<H265Packet> &packets, const std::string &filename);

//   private:
//     std::string prefix;
//     std::string camera_id;
//     H265RingBuffer *cache = nullptr;
//     MysqlClient *mysql_client = nullptr;
//     std::string getTimeString(uint64_t time_ms);
//     std::string getDateDir(uint64_t time_ms);
//     std::shared_ptr<AIJsonRecorder> ai_json_recorder;
//     std::shared_ptr<AIFrameCache> ai_cache;
// };

#pragma once

#include "H265RingBuffer.h"

#include "AIFrameCache.h"
#include "AIJsonRecorder.h"

#include "VideoType.h"
#include "mysql_client.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

class H265Recorder {

  public:
    H265Recorder(const std::string &id, H265RingBuffer *cache, AIFrameCache *ai_cache)
        : camera_id(id), cache(cache), ai_cache(ai_cache) {
        ai_json_recorder = std::make_shared<AIJsonRecorder>();
    }

    void setMysqlClient(MysqlClient *db);

    void saveEvent(uint64_t event_time_ms, const std::string &code, VideoType type);

    void saveAIError(uint64_t event_time_ms, const std::string &grid, const std::string &id);

  private:
    void savePackets(const std::vector<H265Packet> &packets, const std::string &filename);

  private:
    std::string prefix;

    std::string camera_id;

    // H265缓存
    H265RingBuffer *cache = nullptr;

    // AI检测缓存
    AIFrameCache *ai_cache = nullptr;

    // JSON保存器
    std::shared_ptr<AIJsonRecorder> ai_json_recorder;

    MysqlClient *mysql_client = nullptr;

    std::string getTimeString(uint64_t time_ms);

    std::string getDateDir(uint64_t time_ms);

    uint64_t h265_dts_counter = 0;
    bool h265_dts_initialized = false;
};