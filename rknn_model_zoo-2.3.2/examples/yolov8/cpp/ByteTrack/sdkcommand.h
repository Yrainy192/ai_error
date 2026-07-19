
#ifdef WIN32

#if defined(DLL_EXPORTS)
#define JSDK_API extern "C" __declspec(dllexport)
#else
#define JSDK_API extern "C" __declspec(dllimport)
#endif

#else
#define JSDK_API extern "C"
#endif

//执行命令(非阻塞)
JSDK_API const char *sdkcommand(const char *command);

//在单独线程读取结果(阻塞)
JSDK_API const char *sdknotify();

//每10ms向虚拟声卡推480个样本(单声道)
JSDK_API int pushstream(int port, const short sample[]);