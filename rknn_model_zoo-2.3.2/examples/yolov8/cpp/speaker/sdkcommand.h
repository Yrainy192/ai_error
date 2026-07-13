
//***********************************广州市智邦电子工作室*******************************
//微信号：zhibang68
//版权所有: 广州市智邦电子工作室
//此头文件导出C函数，参数和返回值均为命令字符串
//**************************************************************************************

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
JSDK_API const char* sdkcommand(const char *command);

//在单独线程读取结果(阻塞)
JSDK_API const char* sdknotify();

//每10ms向虚拟声卡推480个样本(单声道)
JSDK_API int pushstream(int port,const short sample[]);