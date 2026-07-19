#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

class AudioManager {
  public:
    AudioManager();
    ~AudioManager();
    void start();
    void stop();
    //加入播放队列
    void post(const std::string &cmd);

  private:
    void workerLoop();
    bool waitPlayFinish();

  private:
    std::queue<std::string> audioQueue;
    std::mutex queueMutex;
    std::condition_variable cv;
    std::thread workerThread;
    std::atomic<bool> running{false};
};