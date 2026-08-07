#include "AudioManager.h"
#include "sdkcommand.h"

#include <chrono>
#include <iostream>

AudioManager::AudioManager() {}

AudioManager::~AudioManager() { stop(); }

//==============================
//启动
//==============================
void AudioManager::start() {
    running = true;
    workerThread = std::thread(&AudioManager::workerLoop, this);
}

//==============================
//停止
//==============================
void AudioManager::stop() {
    running = false;
    cv.notify_all();
    if (workerThread.joinable()) {
        workerThread.join();
    }
}

//==============================
//加入播放队列
//==============================
void AudioManager::post(const std::string &cmd) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        audioQueue.push(cmd);
        std::cout << "[AUDIO QUEUE] size=" << audioQueue.size() << std::endl;
    }
    cv.notify_one();
}

//==============================
//播放线程
//==============================
void AudioManager::workerLoop() {
    while (running) {
        std::string cmd;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            cv.wait(lock, [&] { return !audioQueue.empty() || !running; });
            if (!running)
                break;
            cmd = audioQueue.front();
            audioQueue.pop();
        }
        std::cout << "[AUDIO PLAY] " << cmd << std::endl;
        const char *ret = sdkcommand(cmd.c_str());
        std::string result = ret ? ret : "";
        if (result.find("OK") != std::string::npos) {
            waitPlayFinish();
        } else {
            std::cout << "[AUDIO FAILED]" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

//==============================
//等待播放结束
//==============================
bool AudioManager::waitPlayFinish() {
    int disconnectCount = 0;
    while (running) {
        const char *ret = sdkcommand("state -id 1");
        std::string state = ret ? ret : "";
        if (state.find("disconnect") != std::string::npos) {
            disconnectCount++;
        } else {
            disconnectCount = 0;
        }
        if (disconnectCount >= 2) {
            std::cout << "[AUDIO FINISH]" << std::endl;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}