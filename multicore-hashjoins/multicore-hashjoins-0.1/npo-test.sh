#!/bin/bash

# 定义文件名
FILE="src/npj_params.h"

# 定义数组
PREFETCH_DISTANCE_VALUES=(2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20)
BUCKET_VALUES=(2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20)


    
# 外层循环遍历 BUCKET_SIZE
for BUCKET in "${BUCKET_VALUES[@]}"; do
    # 替换 BUCKET_SIZE 的值
    sed -i "s/#define BUCKET_SIZE .*/#define BUCKET_SIZE $BUCKET/" "$FILE"

        # 内层循环遍历 PREFETCH_DISTANCE
    for PREFETCH in "${PREFETCH_DISTANCE_VALUES[@]}"; do
        # 替换 PREFETCH_DISTANCE 的值
        sed -i "s/#define PREFETCH_DISTANCE .*/#define PREFETCH_DISTANCE $PREFETCH/" "$FILE"

        ./configure --enable-prefetch-npj && make -j96

        # 获取初始内存使用情况
        INITIAL_MEMORY=$(free -m | grep Mem | awk '{print $3}')  # 当前内存使用量（以KB为单位）

        # 启动程序并监控内存使用
        ./src/mchashjoins -a NPO -n 96 > tmp.log &  # 在后台启动程序
        PROGRAM_PID=$!
        MAX_MEMORY=$INITIAL_MEMORY

        # 监控内存使用
        while kill -0 $PROGRAM_PID 2>/dev/null; do
            CURRENT_MEMORY=$(free -m | grep Mem | awk '{print $3}')  # 当前内存使用量（以KB为单位）
            if [ "$CURRENT_MEMORY" -gt "$MAX_MEMORY" ]; then
                MAX_MEMORY=$CURRENT_MEMORY
            fi
            sleep 0.5  # 每秒检查一次
        done

        # 等待程序完成
        wait $PROGRAM_PID

        # 计算最大值与初始值的差
        DIFF=$((MAX_MEMORY - INITIAL_MEMORY))

        time_use=$(cat tmp.log | grep -oP "MY-TOTAL-TIME-USECS:\s*\K[0-9]+\.[0-9]+")

        echo "BUCKET_SIZE: $BUCKET, PREFETCH_DISTANCE: $PREFETCH, TOTAL-TIME-USECS: $time_use, TOTAL_MEM_USE_MB: $DIFF,  R with size = 976.562 MiB, S with size = 976.562 MiB" >> result.log
    done
done