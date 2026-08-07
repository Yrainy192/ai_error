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

struct LetterBox {
    int new_w;
    int new_h;

    int pad_x;
    int pad_y;

    float scale;
};

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
    // source：DMABUF fd 直接导入（零拷贝）
    // ⭐stride 已在 RtspStream 用 GstVideoMeta 修正为真实行距（如 2304），
    //   不能再用 caps 的 PLANE_STRIDE(1920)，否则条纹花屏。
    //===========================

    rga_buffer_t src = wrapbuffer_fd(f.fd, f.width, f.height, RK_FORMAT_YCbCr_420_SP,
                                     f.width_stride, f.height_stride);

    spdlog::debug("[RGA SRC] handle={} fd={} {}x{} stride={}x{}", src.handle, f.fd, src.width,
                  src.height, src.wstride, src.hstride);

    if (src.fd <= 0 || src.width <= 0 || src.height <= 0) {
        spdlog::error("[RGA] invalid src fd={} {}x{}", src.fd, src.width, src.height);
        return false;
    }

    //===========================
    // RGB destination
    //===========================

    rga_buffer_t dst = wrapbuffer_virtualaddr(rgb_.data(), 640, 640, RK_FORMAT_RGB_888);

    if (dst.vir_addr == nullptr) {
        spdlog::error("[RGA] dst create failed");
        return false;
    }

    //===========================
    // rect
    //===========================

    im_rect srect = {0, 0, f.width, f.height};

    im_rect drect = {lb.pad_x, lb.pad_y, lb.new_w, lb.new_h};

    //===========================
    // RGA执行（同步）
    //===========================

    IM_STATUS ret;

    {
        std::lock_guard<std::mutex> lock(rga_mutex);

        ret = improcess(src, dst, {}, srect, drect, {}, IM_SYNC);
    }

    if (ret != IM_STATUS_SUCCESS) {
        spdlog::error("[RGA FAILED] ret={}", ret);
        return false;
    }

    //===========================
    // 输出给RKNN
    //===========================

    out.width = 640;

    out.height = 640;

    out.size = 640 * 640 * 3;

    out.format = IMAGE_FORMAT_RGB888;

    out.virt_addr = rgb_.data();

    return true;
}