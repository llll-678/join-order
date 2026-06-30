# SSB/TPC-H Join Order负载设计

选择率在本文中统一定义为过滤后的保留率(pass rate)：数值越小，过滤越强。

| 负载 | 基准 | 核心设计 | 主要不确定性 |
|---|---|---|---|
| SSB-1 | SSB | PART保留率高但事实表很大 | 维度过滤收益与事实表扫描成本冲突 |
| SSB-2 | SSB | PART保留率低且事实表较小 | 强过滤是否仍值得优先于小事实表扫描 |
| SSB-3 | SSB | LINEORDER低于L3 cache边界 | cache内负载下join order差异是否收敛 |
| SSB-4 | SSB | LINEORDER显著超过L3 cache边界 | cache外负载下早期缩减是否主导 |
| SSB-5 | SSB | PART键分布倾斜 | PRO/NPO面对skew时的partition/build端选择风险 |
| SSB-6 | SSB | PART与SUPPLIER过滤方向冲突 | 大维度强过滤与小维度弱过滤的优先级 |
| TPCH-1 | TPC-H | 链式增长型中间结果爆炸 | 早期小表join可能引发后续膨胀 |
| TPCH-2 | TPC-H | 链式收缩型 | 经典早过滤是否应先靠近高选择性节点 |
| TPCH-3 | TPC-H | 早期1%强过滤 | 强过滤节点不在链端时的join order选择 |
| TPCH-4 | TPC-H | 小表弱过滤与大表强过滤冲突 | 保留率、表规模、连接位置三者冲突 |
| TPCH-5 | TPC-H | ORDERS与LINEITEM早join产生大中间结果 | 避免爆炸型early join是否比局部最小输入更重要 |
| TPCH-6 | TPC-H | cache边界敏感型 | cache内join优先与高缩减join优先的冲突 |

## 算法与查询处理模型映射

| 算法 | 查询处理模型 | 说明 |
|---|---|---|
| Sort-Merge Join | 物化连接查询 | 排序与归并阶段天然形成批量中间结果，适合作为物化模型代表。 |
| Partitioned Radix Hash Join(PRO) | 物化连接查询 | 分区后构建/探测，分区边界形成物化批次，适合cache-aware物化模型。 |
| No-Partition Hash Join(NPO) | 行流水线查询 | 无分区、低启动开销，适合逐元组探测和流水线传播。 |
| Vector Join | 向量化查询 | 以批为单位过滤和探测，适合SIMD/cache友好的向量化执行。 |