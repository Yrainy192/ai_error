#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class UploadManager {

  public:
    UploadManager(const std::string &ready_dir, const std::string &uploaded_dir,
                  const std::string &server);

    ~UploadManager();

    void start();

    void stop();

  private:
    void run();

    std::vector<std::string> scanFiles();

    bool uploadFile(const std::string &file);

    bool moveUploaded(const std::string &file);

  private:
    std::string ready_dir_;

    std::string uploaded_dir_;

    std::string server_;

    std::thread thread_;

    std::atomic<bool> running_{false};
};
