#!/usr/bin/env python3
import subprocess
import os


# ===== 文件路径配置 =====
INPUT_FILE = "/home/admin/Rtsp/rknn_model_zoo-2.3.2/examples/python/three_come.wav"
OUTPUT_FILE = "/home/admin/Rtsp/rknn_model_zoo-2.3.2/examples/python/ves_three_come.wav"


def convert_to_wav(input_file, output_file):

    if not os.path.exists(input_file):
        print("输入文件不存在:", input_file)
        return

    cmd = [
        "ffmpeg",
        "-y",
        "-i", input_file,

        # ARM兼容格式
        "-ar", "16000",      # 16kHz
        "-ac", "1",          # 单声道
        "-c:a", "pcm_s16le", # 16bit PCM WAV

        output_file
    ]

    print("开始转换...")
    
    result = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    if result.returncode == 0:
        print("转换成功:")
        print(output_file)

        size = os.path.getsize(output_file)
        print("文件大小:", size, "bytes")

    else:
        print("转换失败:")
        print(result.stderr.decode())


if __name__ == "__main__":
    convert_to_wav(
        INPUT_FILE,
        OUTPUT_FILE
    )