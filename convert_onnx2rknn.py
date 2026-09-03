#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
将 yolov8n_combined.onnx 转换为 RKNN 模型 (rk3588)
用法:
    python convert_onnx2rknn.py [--fp16|--i8]
默认 fp16；加 --i8 使用 int8 量化（需 dataset.txt 校准集）

⚠️ 注意: 端到端(combined)模型做 int8 量化后 cls 输出会全变 0（已验证），
    必须用 fp16，不要用 --i8。
"""
import argparse
import os

from rknn.api import RKNN

ONNX = "/home/admin/Rtsp_6model/yolov8n_combined.onnx"
DATASET = "/home/admin/Rtsp_6model/dataset.txt"
OUT_FP16 = "/home/admin/Rtsp_6model/yolov8n_combined_fp16.rknn"
OUT_I8 = "/home/admin/Rtsp_6model/yolov8n_combined_i8.rknn"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fp16", action="store_true", help="转 fp16（默认）")
    parser.add_argument("--i8", action="store_true", help="转 int8 量化")
    args = parser.parse_args()

    quant = args.i8
    out = OUT_I8 if quant else OUT_FP16

    rknn = RKNN(verbose=True)
    # 输入归一化 /255，等价 onnxruntime 测试用的预处理
    rknn.config(mean_values=[[0, 0, 0]], std_values=[[255, 255, 255]],
                target_platform="rk3588")

    if rknn.load_onnx(model=ONNX) != 0:
        print("[ERROR] load_onnx 失败")
        return
    if rknn.build(do_quantization=quant, dataset=DATASET if quant else None) != 0:
        print("[ERROR] build 失败")
        return
    if rknn.export_rknn(out) != 0:
        print("[ERROR] export 失败")
        return
    print("[OK] 转换成功:", out, os.path.getsize(out), "bytes")
    rknn.release()


if __name__ == "__main__":
    main()
