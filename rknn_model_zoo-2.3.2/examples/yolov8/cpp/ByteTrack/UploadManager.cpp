// #include "UploadManager.h"

// #include <chrono>
// #include <filesystem>
// #include <thread>

// #include <spdlog/spdlog.h>

// namespace fs = std::filesystem;

// UploadManager::UploadManager(const std::string &source_root, const std::string &target_root)
//     : source_root_(source_root), target_root_(target_root) {}

// UploadManager::~UploadManager() { stop(); }

// void UploadManager::start() {

//     if (running_)
//         return;

//     running_ = true;

//     thread_ = std::thread(&UploadManager::run, this);

//     spdlog::info("[Upload] thread started");
// }

// void UploadManager::stop() {

//     running_ = false;

//     if (thread_.joinable()) {
//         thread_.join();
//     }

//     spdlog::info("[Upload] thread stopped");
// }

// void UploadManager::run() {

//     while (running_) {

//         try {

//             auto files = scanReadyFiles();

//             for (auto &file : files) {

//                 if (!running_)
//                     break;

//                 if (copyFile(file)) {

//                     if (moveUploaded(file)) {
//                         spdlog::info("[Upload] success {}", file);
//                     } else {
//                         spdlog::warn("[Upload] move uploaded failed {}", file);
//                     }

//                 } else {

//                     spdlog::warn("[Upload] copy failed {}", file);
//                 }
//             }

//         } catch (const std::exception &e) {

//             spdlog::error("[Upload] exception {}", e.what());
//         }

//         /*
//          * 不要一直扫描
//          */
//         std::this_thread::sleep_for(std::chrono::seconds(5));
//     }
// }

// std::vector<std::string> UploadManager::scanReadyFiles() {

//     std::vector<std::string> result;

//     if (!fs::exists(source_root_)) {
//         return result;
//     }

//     for (auto &p : fs::recursive_directory_iterator(source_root_)) {

//         if (!p.is_regular_file())
//             continue;

//         if (p.path().extension() != ".ts")
//             continue;

//         /*
//          * 只上传 ready目录
//          */
//         if (p.path().parent_path().filename() != "ready") {
//             continue;
//         }

//         /*
//          * 检查json是否存在
//          */

//         auto json = p.path();

//         json.replace_extension(".json");

//         if (!fs::exists(json)) {

//             spdlog::warn("[Upload] json missing {}", json.string());

//             continue;
//         }

//         result.push_back(p.path().string());
//     }

//     return result;
// }

// bool UploadManager::copyFile(const std::string &src) {

//     try {

//         /*
//          *
//          * /home/admin/Videos之后的路径
//          *
//          * 例如:
//          *
//          * /home/admin/Videos/cam0/20260723/AI_error/ready/a.ts
//          *
//          *
//          * 变成:
//          *
//          * /mnt/share/cam0/20260723/AI_error/ready/a.ts
//          *
//          */

//         auto pos = src.find("/Videos/");

//         if (pos == std::string::npos) {

//             spdlog::error("[Upload] invalid path {}", src);

//             return false;
//         }

//         std::string relative = src.substr(pos + 8);

//         fs::path dst = fs::path(target_root_) / relative;

//         fs::create_directories(dst.parent_path());

//         fs::copy_file(src, dst, fs::copy_options::overwrite_existing);

//         /*
//          * 同步json
//          */

//         fs::path json_src(src);

//         json_src.replace_extension(".json");

//         fs::path json_dst(dst);

//         json_dst.replace_extension(".json");

//         if (fs::exists(json_src)) {

//             fs::copy_file(json_src, json_dst, fs::copy_options::overwrite_existing);
//         }

//         return true;

//     } catch (const std::exception &e) {

//         spdlog::error("[Upload] copy exception {}", e.what());

//         return false;
//     }
// }

// bool UploadManager::moveUploaded(const std::string &file) {

//     try {

//         fs::path src(file);

//         fs::path ready_dir = src.parent_path();

//         fs::path uploaded_dir = ready_dir.parent_path() / "uploaded";

//         fs::create_directories(uploaded_dir);

//         /*
//          * ts移动
//          */

//         fs::path dst = uploaded_dir / src.filename();

//         fs::rename(src, dst);

//         /*
//          * json移动
//          */

//         auto json = src;

//         json.replace_extension(".json");

//         if (fs::exists(json)) {

//             fs::path json_dst = uploaded_dir / json.filename();

//             fs::rename(json, json_dst);
//         }

//         return true;

//     } catch (const std::exception &e) {

//         spdlog::error("[Upload] move exception {}", e.what());

//         return false;
//     }
// }
#include "UploadManager.h"

#include <chrono>
#include <filesystem>
#include <thread>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

UploadManager::UploadManager(const std::string &source_root, const std::string &target_root)
    : source_root_(source_root), target_root_(target_root) {}

UploadManager::~UploadManager() { stop(); }

void UploadManager::start() {

    if (running_)
        return;

    running_ = true;

    thread_ = std::thread(&UploadManager::run, this);

    spdlog::info("[Upload] thread started");
}

void UploadManager::stop() {

    running_ = false;

    if (thread_.joinable()) {
        thread_.join();
    }

    spdlog::info("[Upload] thread stopped");
}

void UploadManager::run() {

    while (running_) {

        try {

            auto files = scanFiles();

            for (auto &file : files) {

                if (!running_)
                    break;

                if (copyFile(file)) {

                    spdlog::info("[Upload] success {}", file);

                } else {

                    spdlog::warn("[Upload] failed {}", file);
                }
            }

        } catch (const std::exception &e) {

            spdlog::error("[Upload] exception {}", e.what());
        }

        /*
         * 5秒扫描一次
         */
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

std::vector<std::string> UploadManager::scanFiles() {

    std::vector<std::string> result;

    if (!fs::exists(source_root_)) {
        return result;
    }

    for (auto &p : fs::recursive_directory_iterator(source_root_)) {

        if (!p.is_regular_file())
            continue;

        auto path = p.path();

        /*
         * 跳过正在写入目录
         */
        if (path.string().find("/writing/") != std::string::npos) {
            continue;
        }

        /*
         * 只上传ts/json
         */
        auto ext = path.extension();

        if (ext != ".ts" && ext != ".json") {
            continue;
        }

        result.push_back(path.string());
    }

    return result;
}

bool UploadManager::isFileStable(const std::string &file) {

    try {

        if (!fs::exists(file))
            return false;

        auto size1 = fs::file_size(file);

        /*
         * 等待10秒
         */
        std::this_thread::sleep_for(std::chrono::seconds(10));

        if (!fs::exists(file))
            return false;

        auto size2 = fs::file_size(file);

        if (size1 == size2) {

            spdlog::info("[Upload] stable {} size={}", file, size2);

            return true;
        }

        spdlog::info("[Upload] writing {} {}->{}", file, size1, size2);

    } catch (...) {
    }

    return false;
}

bool UploadManager::copyFile(const std::string &src) {

    try {

        /*
         * 判断文件是否写完
         */
        if (!isFileStable(src)) {
            return false;
        }

        /*
         * 保留Videos后的目录结构
         *
         * /home/admin/Videos/cam0/xxx.ts
         *
         * =>
         *
         * /mnt/share/cam0/xxx.ts
         */

        auto pos = src.find("/Videos/");

        if (pos == std::string::npos) {

            spdlog::error("[Upload] invalid path {}", src);

            return false;
        }

        std::string relative = src.substr(pos + 8);

        fs::path dst = fs::path(target_root_) / relative;

        fs::create_directories(dst.parent_path());

        /*
         * copy ts/json
         */

        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);

        /*
         * 校验大小
         */
        if (fs::file_size(src) != fs::file_size(dst)) {

            spdlog::error("[Upload] size mismatch {}", src);

            return false;
        }

        /*
         * 删除源文件
         */
        fs::remove(src);

        return true;

    } catch (const std::exception &e) {

        spdlog::error("[Upload] copy exception {}", e.what());

        return false;
    }
}