// #pragma once

// #include <atomic>
// #include <condition_variable>
// #include <mutex>
// #include <queue>
// #include <set>
// #include <string>
// #include <thread>

// class SdkManager {

//   public:
//     SdkManager();

//     ~SdkManager();

//     bool init(const std::string &cmd);

//     void start();

//     void stop();

//     // 异步播放
//     void post(const std::string &cmd);

//   private:
//     void workerLoop();

//     bool waitPlayFinish();

//   private:
//     //播放队列
//     std::queue<std::string> taskQueue;

//     //已经在队列里的命令
//     std::set<std::string> pendingCmd;

//     std::mutex queueMutex;

//     std::condition_variable cv;

//     std::thread workerThread;

//     std::atomic<bool> running{false};

//     //保护sdkcommand
//     std::mutex sdkMutex;
// };

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_set>

class SdkManager {

  public:
    SdkManager();

    ~SdkManager();

    // SDK初始化
    bool init(const std::string &cmd);

    //启动线程
    void start();

    //停止线程
    void stop();

    //发送SDK命令
    void post(const std::string &cmd);

  private:
    void workerLoop();

  private:
    std::queue<std::string> taskQueue;

    std::unordered_set<std::string> pendingCmd;

    std::mutex queueMutex;

    std::mutex sdkMutex;

    std::condition_variable cv;

    std::thread workerThread;

    std::atomic<bool> running{false};
};