#include "ModelPool.h"

// int ModelPool::init(const std::string &model_path, int num_models) {

//     models.resize(num_models);

//     for (int i = 0; i < num_models; i++) {

//         models[i] = std::make_shared<ModelItem>();

//         models[i]->core_id = i;

//         int ret = init_yolov8_model(model_path.c_str(), &models[i]->ctx, i);

//         if (ret != 0) {
//             printf("init model %d failed\n", i);

//             return -1;
//         }
//     }

//     return 0;
// }

int ModelPool::init(const std::string &model_path, int num_models) {
    models.resize(num_models);

    for (int i = 0; i < num_models; i++) {
        models[i] = std::make_shared<ModelItem>();

        // 两个模型共享一个NPU core
        int core_id = i / 2;

        // 防止超过NPU核心数量
        if (core_id > 2) {
            core_id = 2;
        }

        models[i]->core_id = core_id;

        int ret = init_yolov8_model(model_path.c_str(), &models[i]->ctx, core_id);

        if (ret != 0) {
            printf("init model %d failed\n", i);
            return -1;
        }

        printf("model %d bind NPU core %d\n", i, core_id);
    }

    return 0;
}

int ModelPool::infer(int model_id, image_buffer_t *img, object_detect_result_list *results) {

    if (model_id < 0 || model_id >= models.size()) {
        return -1;
    }

    auto &model = models[model_id];

    if (!model || model->ctx.rknn_ctx == 0) {
        return -1;
    }

    return inference_yolov8_model(&model->ctx, img, results);
}

void ModelPool::release() {

    for (auto &m : models) {

        if (m && m->ctx.rknn_ctx) {

            release_yolov8_model(&m->ctx);

            m->ctx.rknn_ctx = 0;
        }
    }

    models.clear();
}

int ModelPool::size() { return models.size(); }