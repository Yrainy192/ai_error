#pragma once

#include <memory>
#include <vector>

#include "Frame.h"
#include "image_utils.h"

// letterbox 参数（RGA 与 CPU 兜底共用）
struct LetterBox {
    int new_w;
    int new_h;
    int pad_x;
    int pad_y;
    float scale;
};

class RgaWorker {

  public:
    bool process(const Frame &f, image_buffer_t &out);

  private:
    // ⭐RGA 零拷贝路径（可能因驱动 >4G 物理内存限制偶发失败）
    bool try_rga(const Frame &f, const LetterBox &lb);

    // ⭐CPU 兜底：RGA 失败时 NV12→RGB888 + letterbox，保证不漏帧
    bool cpu_fallback(const Frame &f, const LetterBox &lb);

    // ⭐复用目标 buffer，避免每帧分配
    std::vector<uint8_t> rgb_;
};