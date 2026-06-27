s_size=$((2 ** 30)) 

for i in {10..30}
do
  r_size=$((2 ** i))
  # ./src/mchashjoins --r-size=$r_size --s-size=$s_size -a PRO -n 96 > tmp.log
   INDEX=$i

  # 获取初始内存使用情况
        INITIAL_MEMORY=$(free -m | grep Mem | awk '{print $3}')  # 当前内存使用量（以KB为单位）

        # 启动程序并监控内存使用
         ./src/sortmergejoins --r-size=$r_size --s-size=$s_size -n 32 > tmp.log &  # 在后台启动程序
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
        time_use=$(cat tmp.log | grep -oP "MY-TOTAL-TIME-USECS4:\s*\K[0-9]+\.[0-9]+" | tail -n 1)

        result=$(echo "scale=6; ($r_size + $s_size) / (2^17)" | bc)

        mem_utilization=$(echo "scale=3; $result/$DIFF" | bc)
        mem_utilization=$(printf "%.3f\n" "$mem_utilization")
        # echo "INDEX: $INDEX, TOTAL_MEM_USE_MB: $DIFF, MEM_UTILIZATION: $mem_utilization"
        echo "INDEX: $INDEX, TOTAL-TIME-USECS: $time_use, TOTAL_MEM_USE_MB: $DIFF, MEM_UTILIZATION: $mem_utilization" >> sort-rs-result.log

done

# ./src/mchashjoins --r-size=16777216 --s-size=268435456 -a PRO