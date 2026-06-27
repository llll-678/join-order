#!/bin/bash

# 定义文件名
FILE="src/prj_params.h"

# 定义数组

VAR="L1_CACHE_SIZE"
VALUES=(4096 8192 16384 32768 65536 131072 262144 524288 1048576)
    
for VAL in "${VALUES[@]}"; do

        sed -i "s/#define $VAR .*/#define $VAR $VAL/" "$FILE"

        ./configure --enable-prefetch-npj && make -j96

        # 获取初始内存使用情况
        INITIAL_MEMORY=$(free -m | grep Mem | awk '{print $3}')  # 当前内存使用量（以KB为单位）

        # 启动程序并监控内存使用
        ./src/mchashjoins -a PRO -n 96 > tmp.log &  # 在后台启动程序
        PROGRAM_PID=$!
        MAX_MEMORY=$INITIAL_MEMORY

        # 监控内存使用
        while kill -0 $PROGRAM_PID 2>/dev/null; do
            CURRENT_MEMORY=$(free -m | grep Mem | awk '{print $3}')  # 当前内存使用量（以KB为单位）
            if [ "$CURRENT_MEMORY" -gt "$MAX_MEMORY" ]; then
                MAX_MEMORY=$CURRENT_MEMORY
            fi
            sleep 0.2  # 每秒检查一次
        done

        # 等待程序完成
        wait $PROGRAM_PID

        # 计算最大值与初始值的差
        DIFF=$((MAX_MEMORY - INITIAL_MEMORY))

        time_use=$(cat tmp.log | grep -oP "MY-TOTAL-TIME-USECS:\s*\K[0-9]+\.[0-9]+")
        mem_utilization=$(echo "scale=3; 2048/$DIFF" | bc)
        mem_utilization=$(printf "%.3f\n" "$mem_utilization")

        echo "$VAR: $VAL, TOTAL-TIME-USECS: $time_use, TOTAL_MEM_USE_MB: $DIFF, MEM_UTILIZATION: $mem_utilization" >> $VAR-result.log
done
