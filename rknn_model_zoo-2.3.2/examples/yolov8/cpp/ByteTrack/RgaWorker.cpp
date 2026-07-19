#include "RgaWorker.h"

#include "RgaUtils.h"
#include "im2d.h"
#include "im2d.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>

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

    auto rgb = std::make_shared<std::vector<uint8_t>>();

    rgb->resize(640 * 640 * 3);

    LetterBox lb = get_letterbox(f.width, f.height, 640, 640);

    memset(rgb->data(), 0, rgb->size());

    rga_buffer_t src =
        wrapbuffer_virtualaddr(f.data->data(), f.width, f.height, RK_FORMAT_YCbCr_420_SP);

    rga_buffer_t dst = wrapbuffer_virtualaddr(rgb->data(), 640, 640, RK_FORMAT_RGB_888);

    // rga_buffer_t src = wrapbuffer_virtualaddr(f.data->data(), f.width, f.height, f.width,
    // f.height,
    //                                           RK_FORMAT_YCbCr_420_SP);

    // rga_buffer_t dst = wrapbuffer_virtualaddr(rgb->data(), 640, 640, 640, 640,
    // RK_FORMAT_RGB_888);

    // printf("RGA fail camera=%d\n", f.id);

    im_rect srect = {0, 0, f.width, f.height};

    im_rect drect = {lb.pad_x, lb.pad_y, lb.new_w, lb.new_h};

    {

        std::lock_guard<std::mutex> lock(rga_mutex);

        int ret = improcess(src, dst, {}, srect, drect, {}, 0, nullptr, nullptr, 0);

        if (ret != IM_STATUS_SUCCESS)
            return false;
    }

    out.width = 640;
    out.height = 640;
    out.size = 640 * 640 * 3;

    out.format = IMAGE_FORMAT_RGB888;

    out.virt_addr = rgb->data();

    cache = rgb;

    return true;
}