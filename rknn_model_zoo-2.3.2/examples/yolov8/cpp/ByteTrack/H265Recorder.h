#pragma once

#include "H265RingBuffer.h"

#include "VideoType.h"
#include "mysql_client.h"
#include <mutex>
#include <string>
#include <vector>

class H265Recorder {

  public:
    H265Recorder(const std::string &id, H265RingBuffer *cache) : camera_id(id), cache(cache) {}

    void setMysqlClient(MysqlClient *db);

    void saveEvent(uint64_t event_time_ms, const std::string &code, VideoType type);

    void saveAIError(uint64_t event_time_ms, const std::string &grid);

  private:
    void savePackets(const std::vector<H265Packet> &packets, const std::string &filename);

  private:
    std::string prefix;
    std::string camera_id;
    H265RingBuffer *cache = nullptr;
    MysqlClient *mysql_client = nullptr;
    std::string getTimeString(uint64_t time_ms);
    std::string getDateDir(uint64_t time_ms);
};
