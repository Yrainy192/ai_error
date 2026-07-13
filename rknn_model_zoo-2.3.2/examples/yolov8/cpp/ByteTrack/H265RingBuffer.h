#ifndef H265_RING_BUFFER_H
#define H265_RING_BUFFER_H

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

//===========================
// H265单个包
//===========================
struct H265Packet {

    // GStreamer时间戳
    uint64_t pts = 0;

    uint64_t dts = 0;

    // 单调时间(ms)
    // 用于事件定位
    uint64_t real_time_ms = 0;

    // 是否I帧
    bool keyframe = false;

    // H265裸数据
    std::vector<uint8_t> data;
};

//===========================
// H265环形缓存
//===========================
class H265RingBuffer {

  public:
    // keep_time_ms:
    // 保存多少毫秒
    //
    // 例如:
    // 15000 = 保存15秒
    //
    explicit H265RingBuffer(uint64_t keep_time_ms = 15000);

    // 插入一个H265包
    void push(const H265Packet &packet);

    // 获取时间范围
    //
    // start_ms:
    // 开始时间
    //
    // end_ms:
    // 结束时间
    //
    // 自动向前找最近I帧
    //
    std::vector<H265Packet> getRange(uint64_t start_ms, uint64_t end_ms);

    // 当前缓存数量
    size_t size();

    // 清空
    void clear();

  private:
    // 找开始I帧
    size_t findStartIndex(uint64_t start_ms);

  private:
    std::deque<H265Packet> buffer;

    std::mutex mutex;

    // 保存时间
    uint64_t keep_time_ms;
};

#endif