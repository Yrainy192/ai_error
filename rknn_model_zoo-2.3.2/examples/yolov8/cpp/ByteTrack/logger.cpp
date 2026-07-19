#include "logger.h"
#include <filesystem>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

void InitLogger() {
    std::string log_dir = "/home/admin/Rtsp_6model/rknn_model_zoo-2.3.2/examples/yolov8/cpp/log";
    std::filesystem::create_directories(log_dir);
    auto logger = spdlog::rotating_logger_mt("rknn", log_dir + "/rknn.log", 1024 * 1024 * 5, 3);
    spdlog::set_default_logger(logger);

    spdlog::set_level(spdlog::level::debug);

    spdlog::flush_on(spdlog::level::info);

    spdlog::info("logger init success");
}