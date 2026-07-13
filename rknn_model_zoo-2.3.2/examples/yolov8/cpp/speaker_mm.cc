#include "SdkManager.h"
#include <iostream>
#include <thread>
#include <chrono>

void testPost(SdkManager* sdk)
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(10));

        sdk->post("play -id 1 -s test.wav -d 1 -v 50");
        std::cout << "[TEST] post sent\n";
    }
}

int main()
{
    SdkManager sdk;

    if (!sdk.init("init -node 300 -ip eth1 -path /home/admin/Rtsp/rknn_model_zoo-2.3.2/examples/yolov8/cpp/ -debug -noalsa")) {
        std::cout << "init failed\n";
        return 1;
    }

    std::thread t(testPost, &sdk);
    t.detach();

    // 其他逻辑
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}