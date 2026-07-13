// #include "SdkManager.h"
// #include "sdkcommand.h"

// #include <chrono>
// #include <iostream>
// #include <thread>

// SdkManager::SdkManager() {}

// SdkManager::~SdkManager() { stop(); }

// // ===============================
// // SDK初始化
// // ===============================
// bool SdkManager::init(const std::string &cmd) {

//     std::lock_guard<std::mutex> lock(sdkMutex);

//     const char *ret = sdkcommand(cmd.c_str());

//     std::string result = ret ? ret : "";

//     std::cout << "[SDK INIT] " << result << std::endl;

//     return result.find("OK") != std::string::npos;
// }

// // ===============================
// // 启动播放线程
// // ===============================
// void SdkManager::start() {

//     running = true;

//     workerThread = std::thread(&SdkManager::workerLoop, this);
// }

// // ===============================
// // 停止
// // ===============================
// void SdkManager::stop() {

//     running = false;

//     cv.notify_all();

//     if (workerThread.joinable()) {
//         workerThread.join();
//     }
// }

// // ===============================
// // 投递播放任务
// // ===============================
// void SdkManager::post(const std::string &cmd) {

//     {

//         std::lock_guard<std::mutex> lock(queueMutex);

//         //========================
//         // 队列去重
//         //========================

//         if (pendingCmd.count(cmd)) {

//             std::cout << "[QUEUE DUP] " << cmd << std::endl;

//             return;
//         }

//         taskQueue.push(cmd);

//         pendingCmd.insert(cmd);

//         std::cout << "[QUEUE PUSH] size=" << taskQueue.size() << std::endl;
//     }
//     cv.notify_one();
// }

// // 播放线程
// void SdkManager::workerLoop() {
//     while (running) {
//         std::string cmd;
//         {
//             std::unique_lock<std::mutex> lock(queueMutex);
//             cv.wait(lock, [&] { return !taskQueue.empty() || !running; });
//             if (!running)
//                 break;
//             cmd = taskQueue.front();
//             taskQueue.pop();
//             // 当前任务已经离开队列
//             pendingCmd.erase(cmd);
//         }

//         std::string result;
//         // 调用SDK
//         {
//             std::lock_guard<std::mutex> lock(sdkMutex);
//             const char *ret = sdkcommand(cmd.c_str());
//             result = ret ? ret : "";
//         }
//         std::cout << "[PLAY]" << cmd << " => " << result << std::endl;
//         // 播放成功
//         // 等待结束
//         if (result.find("OK") != std::string::npos) {
//             waitPlayFinish();
//             // SDK释放时间
//             std::this_thread::sleep_for(std::chrono::milliseconds(500));
//         } else {
//             std::cout << "[PLAY FAILED]" << std::endl;
//             //失败不要立即狂刷
//             std::this_thread::sleep_for(std::chrono::milliseconds(500));
//         }
//     }
// }

// // ===============================
// // 等待播放结束
// // ===============================
// bool SdkManager::waitPlayFinish() {
//     int disconnectCount = 0;
//     while (running) {
//         std::string state;
//         {
//             std::lock_guard<std::mutex> lock(sdkMutex);
//             const char *ret = sdkcommand("state -id 1");
//             state = ret ? ret : "";
//         }
//         // std::cout << "[STATE]" << state << std::endl;
//         if (state.find("disconnect") != std::string::npos) {
//             disconnectCount++;
//         } else {
//             disconnectCount = 0;
//         }
//         //连续两次disconnect
//         //确认释放完成
//         if (disconnectCount >= 2) {
//             std::cout << "[PLAY FINISH]" << std::endl;
//             return true;
//         }
//         std::this_thread::sleep_for(std::chrono::milliseconds(200));
//     }
//     return false;
// }

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