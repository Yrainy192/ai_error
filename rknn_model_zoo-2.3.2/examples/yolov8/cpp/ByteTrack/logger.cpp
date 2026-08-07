// #include "logger.h"
// #include <filesystem>
// #include <spdlog/sinks/rotating_file_sink.h>
// #include <spdlog/spdlog.h>

// void InitLogger() {
//     std::string log_dir = "./log";
//     std::filesystem::create_directories(log_dir);
//     auto logger = spdlog::rotating_logger_mt("rknn", log_dir + "/rknn.log", 1024 * 1024 * 5, 3);
//     spdlog::set_default_logger(logger);

//     spdlog::set_level(spdlog::level::debug);

//     spdlog::flush_on(spdlog::level::info);

//     spdlog::info("logger init success");
// }
#include "logger.h"

#include <filesystem>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/spdlog.h>

void InitLogger() {

    std::string log_dir = "./log";

    // 创建日志目录
    std::filesystem::create_directories(log_dir);

    // 按日期生成日志
    auto logger = spdlog::daily_logger_mt("rknn", log_dir + "/rknn.log",
                                          0, // 凌晨0点
                                          0  // 0分开始新日志
    );

    spdlog::set_default_logger(logger);

    // 日志等级
    // ⭐默认 info：每帧调试日志(debug)不打印，避免刷屏；排查问题时可临时改回 debug
    spdlog::set_level(spdlog::level::info);

    // info立即刷新
    spdlog::flush_on(spdlog::level::info);

    spdlog::info("logger init success");
}