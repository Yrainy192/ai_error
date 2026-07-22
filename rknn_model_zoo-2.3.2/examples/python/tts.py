# #!/usr/bin/env python3
# import subprocess
# import os


# # ===== 文件路径配置 =====
# INPUT_FILE = "/home/admin/Rtsp/rknn_model_zoo-2.3.2/examples/python/three_come.wav"
# OUTPUT_FILE = "/home/admin/Rtsp/rknn_model_zoo-2.3.2/examples/yolov8/cpp/speaker/ves_three_come.wav"


# def convert_to_wav(input_file, output_file):

#     if not os.path.exists(input_file):
#         print("输入文件不存在:", input_file)
#         return

#     cmd = [
#         "ffmpeg",
#         "-y",
#         "-i", input_file,

#         # ARM兼容格式
#         "-ar", "16000",      # 16kHz
#         "-ac", "1",          # 单声道
#         "-c:a", "pcm_s16le", # 16bit PCM WAV

#         output_file
#     ]

#     print("开始转换...")
    
#     result = subprocess.run(
#         cmd,
#         stdout=subprocess.PIPE,
#         stderr=subprocess.PIPE
#     )

#     if result.returncode == 0:
#         print("转换成功:")
#         print(output_file)

#         size = os.path.getsize(output_file)
#         print("文件大小:", size, "bytes")

#     else:
#         print("转换失败:")
#         print(result.stderr.decode())


# if __name__ == "__main__":
#     convert_to_wav(
#         INPUT_FILE,
#         OUTPUT_FILE
#     )

#!/usr/bin/env python3
import subprocess
import os


# ===== 文件路径配置 =====
INPUT_DIR = "/home/admin/Rtsp_6model/rknn_model_zoo-2.3.2/examples/python"
OUTPUT_DIR = "/home/admin/Rtsp_6model/rknn_model_zoo-2.3.2/examples/yolov8/cpp/speaker"


# 支持转换的音频格式
AUDIO_EXTS = [
    ".mp3",
    ".aac",
    ".m4a",
    ".flac",
    ".ogg",
    ".wav"
]


def convert_to_wav(input_file, output_file):

    cmd = [
        "ffmpeg",
        "-y",
        "-i", input_file,

        # ARM兼容格式
        "-ar", "16000",       # 16kHz
        "-ac", "1",           # 单声道
        "-c:a", "pcm_s16le",  # 16bit PCM WAV

        output_file
    ]

    print("\n开始转换:")
    print(input_file)

    result = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    if result.returncode == 0:
        size = os.path.getsize(output_file)
        print("转换成功:")
        print(output_file)
        print("大小:", size, "bytes")

    else:
        print("转换失败:")
        print(result.stderr.decode())


def batch_convert():

    if not os.path.exists(INPUT_DIR):
        print("输入目录不存在:", INPUT_DIR)
        return

    # 创建输出目录
    os.makedirs(OUTPUT_DIR, exist_ok=True)


    files = os.listdir(INPUT_DIR)

    count = 0

    for file in files:

        input_file = os.path.join(INPUT_DIR, file)

        # 跳过目录
        if not os.path.isfile(input_file):
            continue

        ext = os.path.splitext(file)[1].lower()

        if ext not in AUDIO_EXTS:
            continue


        # 保持原文件名，只改后缀
        filename = os.path.splitext(file)[0] + ".wav"

        output_file = os.path.join(
            OUTPUT_DIR,
            filename
        )


        convert_to_wav(
            input_file,
            output_file
        )

        count += 1


    print("\n===================")
    print("全部转换完成")
    print("转换数量:", count)
    print("===================")



if __name__ == "__main__":
    batch_convert()