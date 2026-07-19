import ctypes
import time

# 3. 封装调用函数
def send(cmd: str):
    res = sdk.sdkcommand(cmd.encode("utf-8"))
    if res:
        return res.decode("utf-8", errors="ignore")
    return None
if __name__ == "__main__":
# 1. 加载SDK
    sdk = ctypes.CDLL("/home/admin/Rtsp/rknn_model_zoo-2.3.2/examples/python/libsdk.so")

# 2. 返回值类型
    sdk.sdkcommand.restype = ctypes.c_char_p
# =========================
# 1️⃣ 初始化（非常重要）
# =========================
    print("INIT:", send("init -ip 192.168.0.110 -debug"))

    time.sleep(1)

# =========================
# 2️⃣ 测试播放文字
# =========================
    text = "欢迎使用北猎语音广播系统"

    cmd = f"tts -id 1 -v 5 -r 3 -s @$ {text} @$"

    print("TTS:", send(cmd))