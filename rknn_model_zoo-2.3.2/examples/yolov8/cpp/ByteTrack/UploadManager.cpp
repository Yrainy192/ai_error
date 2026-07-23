#include "UploadManager.h"

#include <chrono>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

UploadManager::UploadManager(const std::string &ready, const std::string &uploaded,
                             const std::string &server)
    : ready_dir_(ready), uploaded_dir_(uploaded), server_(server) {}

UploadManager::~UploadManager() { stop(); }

void UploadManager::start() {

    if (running_)
        return;

    running_ = true;

    thread_ = std::thread(&UploadManager::run, this);
}

void UploadManager::stop() {

    running_ = false;

    if (thread_.joinable())
        thread_.join();
}

void UploadManager::run() {

    while (running_) {

        auto files = scanFiles();

        for (auto &file : files) {

            std::cout << "upload:" << file << std::endl;

            if (uploadFile(file)) {

                moveUploaded(file);

                std::cout << "upload success" << std::endl;

            } else {

                std::cout << "upload failed" << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

std::vector<std::string> UploadManager::scanFiles() {

    std::vector<std::string> result;

    for (auto &p : fs::directory_iterator(ready_dir_)) {

        if (!p.is_regular_file())
            continue;

        auto ext = p.path().extension();

        if (ext == ".ts") {

            result.push_back(p.path().string());
        }
    }

    return result;
}

bool UploadManager::uploadFile(const std::string &file) {

    std::string cmd;

    cmd = "rsync -az " + file + " " + server_ + " >/dev/null 2>&1";

    int ret = system(cmd.c_str());

    return ret == 0;
}

bool UploadManager::moveUploaded(const std::string &file) {

    try {

        fs::path src(file);

        fs::path dst = fs::path(uploaded_dir_) / src.filename();

        fs::rename(src, dst);

        return true;

    } catch (...) {

        return false;
    }
}
