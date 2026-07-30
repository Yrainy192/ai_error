#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

class UploadManager {

  public:
    UploadManager(const std::string &source_root, const std::string &target_root);

    ~UploadManager();

    void start();

    void stop();

  private:
    void run();

    std::vector<std::string> scanFiles();

    bool copyFile(const std::string &file);

    bool isFileStable(const std::string &file);

  private:
    std::string source_root_;

    std::string target_root_;

    std::thread thread_;

    std::atomic<bool> running_{false};
};