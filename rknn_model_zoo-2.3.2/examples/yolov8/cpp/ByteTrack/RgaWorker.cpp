#include "RgaWorker.h"

#include "RgaUtils.h"
#include "im2d.h"
#include "im2d.hpp"

#include <algorithm>
#include <cstring>
#include <gst/gst.h>
#include <mutex>
#include <spdlog/spdlog.h>

static std::mutex rga_mutex;

static LetterBox get_letterbox(int w, int h, int dst_w, int dst_h) {

    LetterBox lb;

    float r = std::min((float)dst_w / w, (float)dst_h / h);

    lb.scale = r;

    lb.new_w = (int)(w * r);

    lb.new_h = (int)(h * r);

    lb.pad_x = (dst_w - lb.new_w) / 2;

    lb.pad_y = (dst_h - lb.new_h) / 2;

    return lb;
}

//===========================
// RGB 输出填充（RGA 与 CPU 兜底共用）
//===========================

static void fill_rgb_output(image_buffer_t &out, uint8_t *rgb) {
    out.width = 640;
    out.height = 640;
    out.size = 640 * 640 * 3;
    out.format = IMAGE_FORMAT_RGB888;
    out.virt_addr = rgb;
}

bool RgaWorker::process(const Frame &f, image_buffer_t &out) {

    spdlog::debug("[RGA INPUT] fd={} {}x{} stride={}x{}", f.fd, f.width, f.height, f.width_stride,
                  f.height_stride);

    // ⭐零拷贝：直接用 DMABUF fd 导入 RGA，不 mmap、不 CPU 拷贝
    //   （frame 持有 gst_buffer 引用，保证使用期间 fd 有效）
    if (f.fd < 0) {
        spdlog::error("[RGA] invalid dmabuf fd");
        return false;
    }

    //===========================
    // 创建RGB buffer（复用）
    //===========================

    rgb_.resize(640 * 640 * 3);

    memset(rgb_.data(), 0, rgb_.size());

    //===========================
    // letterbox
    //===========================

    LetterBox lb = get_letterbox(f.width, f.height, 640, 640);

    spdlog::debug("[LETTERBOX] new={}x{} pad={} {}", lb.new_w, lb.new_h, lb.pad_x, lb.pad_y);

    //===========================
    // 先走 RGA 零拷贝
    //===========================

    if (!try_rga(f, lb)) {
        // ⭐RGA 偶发失败（物理内存 >4G，驱动 MMU 无法映射）→ CPU 兜底，保证不漏帧
        spdlog::warn("[RGA] RGA failed, fallback CPU frame={}", f.frame_id);

        if (!cpu_fallback(f, lb)) {
            spdlog::error("[RGA] process failed (RGA + CPU fallback) frame={}", f.frame_id);
            return false;
        }
    }

    fill_rgb_output(out, rgb_.data());

    return true;
}

//===========================
// RGA 零拷贝路径
//===========================

bool RgaWorker::try_rga(const Frame &f, const LetterBox &lb) {

    // source：DMABUF fd 直接导入（零拷贝）
    // ⭐stride 已在 RtspStream 用 GstVideoMeta 修正为真实行距（如 2304），
    //   不能再用 caps 的 PLANE_STRIDE(1920)，否则条纹花屏。
    rga_buffer_t src = wrapbuffer_fd(f.fd, f.width, f.height, RK_FORMAT_YCbCr_420_SP,
                                     f.width_stride, f.height_stride);

    spdlog::debug("[RGA SRC] handle={} fd={} {}x{} stride={}x{}", src.handle, f.fd, src.width,
                  src.height, src.wstride, src.hstride);

    if (src.fd <= 0 || src.width <= 0 || src.height <= 0) {
        spdlog::error("[RGA] invalid src fd={} {}x{}", src.fd, src.width, src.height);
        return false;
    }

    // RGB destination
    rga_buffer_t dst = wrapbuffer_virtualaddr(rgb_.data(), 640, 640, RK_FORMAT_RGB_888);

    if (dst.vir_addr == nullptr) {
        spdlog::error("[RGA] dst create failed");
        return false;
    }

    im_rect srect = {0, 0, f.width, f.height};

    im_rect drect = {lb.pad_x, lb.pad_y, lb.new_w, lb.new_h};

    IM_STATUS ret;

    {
        std::lock_guard<std::mutex> lock(rga_mutex);

        ret = improcess(src, dst, {}, srect, drect, {}, IM_SYNC);
    }

    if (ret != IM_STATUS_SUCCESS) {
        spdlog::error("[RGA FAILED] ret={}", ret);
        return false;
    }

    return true;
}

//===========================
// CPU 兜底：NV12(带 stride) → RGB888 + letterbox
//===========================

static inline uint8_t rga_clamp_u8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

bool RgaWorker::cpu_fallback(const Frame &f, const LetterBox &lb) {

    if (!f.gst_buffer) {
        spdlog::error("[RGA-CPU] no gst_buffer for fallback");
        return false;
    }

    GstMapInfo map = {};

    if (!gst_buffer_map(f.gst_buffer, &map, GST_MAP_READ)) {
        spdlog::error("[RGA-CPU] gst_buffer_map failed");
        return false;
    }

    const int sw = f.width;
    const int sh = f.height;
    const int sstride = f.width_stride > 0 ? f.width_stride : f.width; // 真实行距(2304)
    const int hstride = f.height_stride > 0 ? f.height_stride : f.height;

    const uint8_t *y_plane = map.data;
    const uint8_t *uv_plane = map.data + (size_t)sstride * hstride;

    const float scale = lb.scale;
    const int pad_x = lb.pad_x;
    const int pad_y = lb.pad_y;
    const int new_w = lb.new_w;
    const int new_h = lb.new_h;

    const int dst_w = 640;
    const int dst_h = 640;

    for (int dy = 0; dy < dst_h; dy++) {
        uint8_t *dst_row = rgb_.data() + (size_t)dy * dst_w * 3;

        for (int dx = 0; dx < dst_w; dx++) {
            uint8_t *p = &dst_row[dx * 3];

            // 黑边 padding
            if (dx < pad_x || dx >= pad_x + new_w || dy < pad_y || dy >= pad_y + new_h) {
                p[0] = 0;
                p[1] = 0;
                p[2] = 0;
                continue;
            }

            // 映射回源坐标（Y 双线性插值）
            float sx = (dx - pad_x) / scale;
            float sy = (dy - pad_y) / scale;

            int x0 = (int)sx;
            int y0 = (int)sy;
            int x1 = std::min(x0 + 1, sw - 1);
            int y1 = std::min(y0 + 1, sh - 1);
            float fx = sx - x0;
            float fy = sy - y0;

            float y00 = y_plane[(size_t)y0 * sstride + x0];
            float y10 = y_plane[(size_t)y0 * sstride + x1];
            float y01 = y_plane[(size_t)y1 * sstride + x0];
            float y11 = y_plane[(size_t)y1 * sstride + x1];
            float Y = (y00 * (1 - fx) + y10 * fx) * (1 - fy) + (y01 * (1 - fx) + y11 * fx) * fy;

            // UV：最近邻（每 2x2 亮度块共享一个色度像素）
            int ux = x0 & ~1;
            int uy = y0 & ~1;
            const uint8_t *uv = uv_plane + (size_t)(uy / 2) * sstride + ux;
            float U = uv[0] - 128.0f;
            float V = uv[1] - 128.0f;

            // BT.601 limited range
            int r = (int)(Y + 1.402f * V);
            int g = (int)(Y - 0.344136f * U - 0.714136f * V);
            int b = (int)(Y + 1.772f * U);

            p[0] = rga_clamp_u8(r);
            p[1] = rga_clamp_u8(g);
            p[2] = rga_clamp_u8(b);
        }
    }

    gst_buffer_unmap(f.gst_buffer, &map);

    spdlog::warn("[RGA-CPU] fallback done {}x{} stride={}", f.width, f.height, sstride);

    return true;
}