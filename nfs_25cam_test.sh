#!/bin/bash


# ======================================
# NFS Camera Upload Stress Test
#
# 模拟:
# 30路摄像头
# 每60秒生成60MB视频
# writing -> ready -> rename
#
# 限制上传并发数量
# ======================================


NFS_DIR="/mnt/share/test_upload"


CAM_COUNT=30          # 摄像头数量

FILE_SIZE_MB=60       # 单个TS大小

INTERVAL=60           # 每隔60秒生成一次

TEST_TIME_MIN=10      # 测试时间

MAX_UPLOAD_THREAD=3   # 最大同时上传数量



echo "================================="
echo " NFS Camera Upload Test"
echo " DIR       : $NFS_DIR"
echo " CAMERA    : $CAM_COUNT"
echo " FILE SIZE : ${FILE_SIZE_MB}MB"
echo " INTERVAL  : ${INTERVAL}s"
echo " THREAD    : $MAX_UPLOAD_THREAD"
echo " TIME      : ${TEST_TIME_MIN}min"
echo "================================="



mkdir -p $NFS_DIR



START_TIME=$(date +%s)



# ===============================
# 单摄像头任务
# ===============================

camera_task()
{

    CAM=$1

    COUNT=0
    TOTAL=0


    echo "[CAM$CAM] start"


    while true
    do

        NOW=$(date +%s)


        # 超时退出
        if [ $((NOW-START_TIME)) -ge $((TEST_TIME_MIN*60)) ]
        then
            break
        fi



        TMP_FILE="${NFS_DIR}/cam${CAM}_${COUNT}.ts.uploading"

        FINAL_FILE="${NFS_DIR}/cam${CAM}_${COUNT}.ts"



        echo "[CAM$CAM] upload start $FINAL_FILE"



        # 模拟视频文件上传
        dd if=/dev/zero \
           of=$TMP_FILE \
           bs=1M \
           count=$FILE_SIZE_MB \
           status=none



        # 模拟完成后rename
        mv $TMP_FILE $FINAL_FILE



        SIZE=$(du -m $FINAL_FILE | awk '{print $1}')


        TOTAL=$((TOTAL+SIZE))


        echo "[CAM$CAM] upload finish ${SIZE}MB"



        COUNT=$((COUNT+1))


        sleep $INTERVAL


    done



    echo "[CAM$CAM] finish files=$COUNT total=${TOTAL}MB"

}



# ===============================
# 控制并发
# ===============================


running=0



for ((i=1;i<=CAM_COUNT;i++))
do

    while true
    do

        running=$(jobs -r | wc -l)


        if [ $running -lt $MAX_UPLOAD_THREAD ]
        then
            break
        fi


        sleep 1

    done



    camera_task $i &


done



wait



echo "================================="
echo " ALL CAMERA TEST FINISHED"
echo "================================="