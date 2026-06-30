# Join-Order Cost-Model Experiments

面向内存数据库的 **Join Order（连接顺序）优化研究** 实验框架。基于分析型成本模型，在 SSB（Star Schema Benchmark）和 TPC-H 两种经典基准上，对比四种主流内存 Join 算法在不同 Join Order 下的性能表现。

---

## 目录结构

```
join/
├── experiments/
│   ├── join_order_study.py   # 主实验脚本
│   └── README.md             # 本文件
├── results/
│   ├── join_order_results.csv   # 实验结果（CSV）
│   ├── join_order_results.json  # 实验结果（JSON）
│   └── workload_design.md       # 负载设计说明
├── sort-merge-joins/          # Sort-Merge Join 的 C 实现
└── ...                        # 其他 Join 算法实现
```

---

## 研究的四种 Join 算法

| 算法 | 查询处理模型 | 说明 |
|------|-------------|------|
| **Sort-Merge Join** | 物化（materialized） | 排序+归并，天然形成批量中间结果 |
| **Partitioned Radix Hash Join (PRO)** | 物化（materialized） | 分区后构建/探测，cache-aware |
| **No-Partition Hash Join (NPO)** | 行流水线（row-pipeline） | 无分区、低启动开销，逐元组探测 |
| **Vector Join** | 向量化（vectorized） | 批处理过滤和探测，SIMD/cache 友好 |

每种算法通过 `AlgorithmProfile` 定义了成本参数：扫描成本（scan）、准备成本（prep）、连接成本（join）、物化成本（materialize）、cache 敏感度斜率（cache_slope）、倾斜敏感度斜率（skew_slope）和启动成本（startup）。

---

## 12 个研究负载

### SSB 负载（星型 Schema）

| 负载 | 核心设计 | 研究冲突 |
|------|---------|---------|
| SSB-1 | PART 保留率高 + 事实表极大 | 维度过滤收益 vs 事实表扫描成本 |
| SSB-2 | PART 保留率低 + 事实表较小 | 强过滤是否应优先于小事实表扫描 |
| SSB-3 | LINEORDER 低于 L3 cache | cache 内负载下 join order 差异是否收敛 |
| SSB-4 | LINEORDER 远超 L3 cache | cache 外负载下早期缩减是否主导 |
| SSB-5 | PART 键分布倾斜（skew=1.35） | PRO/NPO 面对 skew 时的分区/构建端选择风险 |
| SSB-6 | PART 与 SUPPLIER 过滤方向冲突 | 大维度强过滤 vs 小维度弱过滤的优先级 |

### TPC-H 负载（链式 Schema）

| 负载 | 核心设计 | 研究冲突 |
|------|---------|---------|
| TPCH-1 | 链式增长型，中间结果爆炸 | 早期小表 join 可能引发后续膨胀 |
| TPCH-2 | 链式收缩型 | 经典早过滤是否应先靠近高选择性节点 |
| TPCH-3 | 早期 1% 强过滤 | 强过滤节点不在链端时的 join order 选择 |
| TPCH-4 | 小表弱过滤 + 大表强过滤冲突 | 保留率、表规模、连接位置三者冲突 |
| TPCH-5 | ORDERS 与 LINEITEM 早 join 产生大中间结果 | 避免爆炸型 early join vs 局部最小输入优先 |
| TPCH-6 | cache 边界敏感型 | cache 内 join 优先 vs 高缩减 join 优先 |

---

## 成本模型

### 核心公式

对于 Join 序列中的每一步，成本计算包含：

```
step_cost = (algorithm_specific_cost + materialize_cost × output_rows)
           × cache_multiplier × skew_multiplier
           + startup_cost
```

### Cache 惩罚

当关系大小超过 L3 cache 边界（默认 32 MiB）时，施加对数惩罚：
```
cache_multiplier = 1.0 + cache_slope × log₂(relation_mib / l3_mib)
```

### 算法特异性成本

- **Sort-Merge Join**：排序成本 `n·log₂(n)` + 扫描成本
- **Partitioned Radix Hash Join**：分区成本（prep + join）× 两侧行数
- **No-Partition / Vector Join**：构建端（较小侧）+ 探测端（较大侧）+ 部分扫描成本

---

## 使用方法

### 依赖

- Python 3.8+（仅使用标准库）

### 运行实验

```bash
cd join/experiments
python join_order_study.py
```

### 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--l3-mib` | 32.0 | L3 cache 边界（MiB），影响 cache 惩罚计算 |

示例：
```bash
# 使用 64 MiB 的 L3 cache 边界
python join_order_study.py --l3-mib 64.0
```

### 输出文件

运行后在 `../results/` 目录下生成：

| 文件 | 内容 |
|------|------|
| `join_order_results.csv` | 每个 workload × algorithm 组合的最优 join order、归一化成本、最大中间结果行数/MiB、cache 越界次数、启发式对比 |
| `join_order_results.json` | 同上，JSON 格式 |
| `workload_design.md` | 负载设计说明 Markdown 文档 |

---

## 关键发现（来自实验结果）

1. **Vector Join 在所有场景下成本最低**，其次为 NPO、PRO、Sort-Merge Join
2. **SSB-3（cache 内）**：最优与启发式的 slowdown 仅 ~1.004，说明 cache 内 join order 选择空间收窄
3. **SSB-6**：启发式在大维度强过滤 vs 小维度弱过滤冲突下表现最差，slowdown 可达 2.3×（Sort-Merge Join）
4. **TPCH-2/5/6**：部分场景下启发式即最优（slowdown=1.0），冲突不足以改变贪心选择
5. **TPCH-1（中间结果爆炸）**：最优策略是从 PART（小表 + 强过滤）开始逆向 join，而非从小表 CUSTOMER 开始

---

## 代码结构

```
join_order_study.py
├── TableSpec           # 表规格（行数、元组宽度、过滤保留率）
├── Workload            # 负载定义（benchmark类型、图结构、表集合、边、倾斜因子等）
├── AlgorithmProfile    # 算法成本参数配置
├── ssb_tables()        # 构造 SSB 表集合
├── tpch_tables()       # 构造 TPC-H 表集合
├── build_workloads()   # 创建全部 12 个负载
├── enumerate_orders()  # 枚举所有合法 join order
├── join_output_rows()  # 估算 join 输出行数
├── step_cost()         # 单步 join 成本计算
├── evaluate_order()    # 评估完整 join order 序列
├── heuristic_order()   # 贪心启发式 join order
├── run_experiment()    # 主实验循环
├── write_csv()         # 输出 CSV
├── write_json()        # 输出 JSON
└── main()              # 入口
```
