
#include <iostream>
#include <thread>
#include <stdlib.h>
#include "sdkcommand.h"
#include "speaker_manager.h"

#ifdef _MSC_VER
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif
#include <Windows.h>
#else
#include <string.h>
#endif

std::string unicodetoutf8(std::wstring wsrc)
{
#ifdef _MSC_VER
    char* m_char;
    int len = WideCharToMultiByte(CP_UTF8, 0, wsrc.c_str(), (int)wsrc.length(), NULL, 0, NULL, NULL);
    m_char = new char[len + 1];            //checked
    WideCharToMultiByte(CP_UTF8, 0, wsrc.c_str(), (int)wsrc.length(), m_char, len, NULL, NULL);
    m_char[len] = '\0';
    std::string out = m_char;
    delete[]m_char;
    return out;
#else
    std::wstring wstr = wsrc;

    size_t len = wstr.size() * 4;
    setlocale(LC_CTYPE, "");
    char* buf = new char[len];             //checked

    wcstombs(buf, wstr.c_str(), len);
    std::string out(buf);
    delete[] buf;
    return out;
#endif
}



//使用独立线程监视状态消息
void notify()
{
	while (true)
	{
        std::string strNotify = sdknotify();
        std::cout << strNotify.c_str() << std::endl;
	}
}

void playX()
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        std::cout << sdkcommand("play -id 1 -s test.wav -d 1 -v 50");
        // 如果是“播放音频x”，就在这里换成播放函数
    }
}


int main()
{   
    //g_sdk.call("play -id 1 -s test.wav -d 1");
    std::thread t1(notify);

    // 新增：10秒定时线程
    std::thread t2(playX);

    const char *ret = sdkcommand("init -node 300 -ip eth1 -path /home/admin/Rtsp/rknn_model_zoo-2.3.2/examples/yolov8/cpp/ -debug -noalsa");

    std::string strRet = ret;
    std::cout << strRet.c_str() << std::endl;

    if (strRet != "OK")
    {
        std::cout << "SDK init error." << std::endl;

        t2.join();
        return 1;
    }
    std::cout << sdkcommand("play -id 1 -s test.wav -d 1 -v 50");
    std::cout << "#";
    char cmd[1024];

    while (std::cin.getline(cmd, 1024))
    {   
        if (strlen(cmd) == 0)
        {
            std::cout << "#";
            continue;
        }
        auto ret = sdkcommand(cmd);
        std::cout << "ret ptr=" << (void*)ret << std::endl;
        std::cout << ret << std::endl;
        std::cout << sdkcommand(cmd) << std::endl;
        std::cout << "#";
    }

    t2.join();

    return 0;
}