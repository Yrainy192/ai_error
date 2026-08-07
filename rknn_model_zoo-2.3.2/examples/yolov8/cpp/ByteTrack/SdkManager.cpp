#include "SdkManager.h"
#include "sdkcommand.h"

#include <iostream>

SdkManager::SdkManager() {}

SdkManager::~SdkManager() { stop(); }

//================================
// SDK初始化
//================================
bool SdkManager::init(const std::string &cmd) {

    std::lock_guard<std::mutex> lock(sdkMutex);

    const char *ret = sdkcommand(cmd.c_str());

    std::string result = ret ? ret : "";

    std::cout << "[SDK INIT] " << result << std::endl;

    return result.find("OK") != std::string::npos;
}

//================================
//启动线程
//================================
void SdkManager::start() {

    running = true;

    workerThread = std::thread(&SdkManager::workerLoop, this);
}

//================================
//停止
//================================
void SdkManager::stop() {

    running = false;

    cv.notify_all();

    if (workerThread.joinable()) {
        workerThread.join();
    }
}

//================================
//提交SDK任务
//================================
void SdkManager::post(const std::string &cmd) {

    {

        std::lock_guard<std::mutex> lock(queueMutex);

        if (pendingCmd.count(cmd)) {
            std::cout << "[SDK DUP] " << cmd << std::endl;

            return;
        }

        taskQueue.push(cmd);

        pendingCmd.insert(cmd);

        std::cout << "[SDK QUEUE] size=" << taskQueue.size() << std::endl;
    }

    cv.notify_one();
}

//================================
// SDK工作线程
//================================
void SdkManager::workerLoop() {

    while (running) {

        std::string cmd;

        {
            std::unique_lock<std::mutex> lock(queueMutex);

            cv.wait(lock, [&] { return !taskQueue.empty() || !running; });

            if (!running)
                break;

            cmd = taskQueue.front();

            taskQueue.pop();

            pendingCmd.erase(cmd);
        }

        std::string result;

        {

            std::lock_guard<std::mutex> lock(sdkMutex);

            const char *ret = sdkcommand(cmd.c_str());

            result = ret ? ret : "";
        }

        std::cout << "[SDK CMD] " << cmd << " => " << result << std::endl;
    }
}