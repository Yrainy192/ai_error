#include "RgaWorker.h"

#include "RgaUtils.h"
#include "im2d.h"
#include "im2d.hpp"

#include <algorithm>
#include <cstring>
#include <gst/gst.h>
#include <gst/video/video.h>
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

    // ⭐需要 gst_buffer 引用，用它 mmap 出 DMABUF 的用户态虚拟地址供 RGA 使用
    if (!f.gst_buffer) {
        spdlog::error("[RGA] no gst_buffer");
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
    // source：DMABUF -> 用户态虚拟地址
    // ⭐平台限制：RK3588 CMA 池在物理 4G 处，mppvideodec 的 DMABUF 物理地址全 >4G，
    //   当前 RGA 内核驱动不支持映射 >4G（内核日志：RGA_MMU unsupported memory larger
    //   than 4G! / src channel map job buffer failed），wrapbuffer_fd 会满屏条纹花屏。
    //   改用 gst_buffer_map 取用户态虚拟地址 + wrapbuffer_virtualaddr，由 RGA MMU
    //   按虚拟地址访问，规避 >4G 限制（实测画面正常）。
    //===========================

    GstMapInfo map;

    if (!gst_buffer_map(f.gst_buffer, &map, GST_MAP_READ)) {
        spdlog::error("[RGA] gst_buffer_map failed");
        return false;
    }

    // ⭐关键：mppvideodec 输出的 NV12 buffer 每行有 padding（例如 1920→2304，64 对齐）。
    //   caps 的 GST_VIDEO_INFO_PLANE_STRIDE 报告 1920，但 buffer 实际行距更大，必须用
    //   GstVideoMeta 的真实 stride，否则 RGA 按错误行距读取 → 条纹花屏。
    int wstride = f.width_stride;
    GstVideoMeta *vmeta = gst_buffer_get_video_meta(f.gst_buffer);
    if (vmeta && vmeta->n_planes > 0) {
        wstride = vmeta->stride[0];
    }

    rga_buffer_t src = wrapbuffer_virtualaddr(map.data, f.width, f.height, RK_FORMAT_YCbCr_420_SP,
                                              wstride, f.height_stride);

    spdlog::debug("[RGA SRC] addr={} fd={} {}x{} stride={}x{}", (void *)src.vir_addr, f.fd,
                  src.width, src.height, src.wstride, src.hstride);

    if (src.vir_addr == nullptr || src.width <= 0 || src.height <= 0) {
        spdlog::error("[RGA] invalid src addr={} {}x{}", (void *)src.vir_addr, src.width,
                      src.height);
        gst_buffer_unmap(f.gst_buffer, &map);
        return false;
    }

    //===========================
    // RGB destination
    //===========================

    rga_buffer_t dst = wrapbuffer_virtualaddr(rgb_.data(), 640, 640, RK_FORMAT_RGB_888);

    if (dst.vir_addr == nullptr) {
        spdlog::error("[RGA] dst create failed");
        gst_buffer_unmap(f.gst_buffer, &map);
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

    // ⭐RGA 同步完成，释放 DMABUF 映射
    gst_buffer_unmap(f.gst_buffer, &map);

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