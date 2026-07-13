#pragma once

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "yolov8.h"

struct ModelItem {
    rknn_app_context_t ctx;
    int core_id;

    ModelItem() {
        memset(&ctx, 0, sizeof(ctx));
        core_id = 0;
    }
};

class ModelPool {

  public:
    int init(const std::string &model_path, int num_models);

    int infer(int model_id, image_buffer_t *img, object_detect_result_list *results);

    void release();

    int size();

  private:
    std::vector<std::shared_ptr<ModelItem>> models;
};