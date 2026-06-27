#!/usr/bin/env python3
"""Join-order workload design and reproducible cost-model experiments.

The original folders in this workspace provide two-relation implementations of
sort-merge joins and multicore hash joins.  This script adds a multi-relation
research layer for SSB/TPC-H style join-order exploration.  It intentionally
keeps the model explicit: each workload lists the conflict being studied, and
each algorithm profile captures the intended query processing model.
"""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


ROOT = Path(__file__).resolve().parents[1]
RESULT_DIR = ROOT / "results"
DEFAULT_L3_MIB = 32.0


@dataclass(frozen=True)
class TableSpec:
    rows: float
    width: int = 32
    pass_rate: float = 1.0

    @property
    def effective_rows(self) -> float:
        return self.rows * self.pass_rate


@dataclass(frozen=True)
class Workload:
    wid: str
    benchmark: str
    graph: str
    description: str
    conflict: str
    tables: Dict[str, TableSpec]
    fact_table: str | None
    edges: Dict[Tuple[str, str], float]
    skew: float = 1.0


@dataclass(frozen=True)
class AlgorithmProfile:
    name: str
    model: str
    scan: float
    prep: float
    join: float
    materialize: float
    cache_slope: float
    skew_slope: float
    startup: float


ALGORITHMS: Tuple[AlgorithmProfile, ...] = (
    AlgorithmProfile(
        name="Sort-Merge Join",
        model="materialized",
        scan=1.05,
        prep=0.34,
        join=0.24,
        materialize=0.18,
        cache_slope=0.09,
        skew_slope=0.02,
        startup=15_000.0,
    ),
    AlgorithmProfile(
        name="Partitioned Radix Hash Join",
        model="materialized",
        scan=0.82,
        prep=0.22,
        join=0.19,
        materialize=0.16,
        cache_slope=0.045,
        skew_slope=0.09,
        startup=11_000.0,
    ),
    AlgorithmProfile(
        name="No-Partition Hash Join",
        model="row-pipeline",
        scan=0.62,
        prep=0.13,
        join=0.15,
        materialize=0.045,
        cache_slope=0.16,
        skew_slope=0.11,
        startup=6_000.0,
    ),
    AlgorithmProfile(
        name="Vector Join",
        model="vectorized",
        scan=0.45,
        prep=0.10,
        join=0.105,
        materialize=0.035,
        cache_slope=0.055,
        skew_slope=0.045,
        startup=8_000.0,
    ),
)


def edge_key(left: str, right: str) -> Tuple[str, str]:
    return tuple(sorted((left, right)))


def ssb_tables(
    lineorder_rows: float,
    part_pass: float,
    supplier_pass: float,
    customer_pass: float,
    date_pass: float,
    part_rows: float = 200_000,
    supplier_rows: float = 20_000,
    customer_rows: float = 3_000_000,
    date_rows: float = 2_556,
    lineorder_width: int = 32,
) -> Dict[str, TableSpec]:
    return {
        "LINEORDER": TableSpec(lineorder_rows, lineorder_width, 1.0),
        "PART": TableSpec(part_rows, 32, part_pass),
        "SUPPLIER": TableSpec(supplier_rows, 32, supplier_pass),
        "CUSTOMER": TableSpec(customer_rows, 32, customer_pass),
        "DATE": TableSpec(date_rows, 16, date_pass),
    }


def tpch_tables(
    customer_rows: float,
    orders_rows: float,
    lineitem_rows: float,
    part_rows: float,
    customer_pass: float,
    orders_pass: float,
    lineitem_pass: float,
    part_pass: float,
) -> Dict[str, TableSpec]:
    return {
        "CUSTOMER": TableSpec(customer_rows, 48, customer_pass),
        "ORDERS": TableSpec(orders_rows, 40, orders_pass),
        "LINEITEM": TableSpec(lineitem_rows, 56, lineitem_pass),
        "PART": TableSpec(part_rows, 32, part_pass),
    }


def build_workloads() -> List[Workload]:
    ssb_edges = {
        edge_key("LINEORDER", "PART"): 1.0,
        edge_key("LINEORDER", "SUPPLIER"): 1.0,
        edge_key("LINEORDER", "CUSTOMER"): 1.0,
        edge_key("LINEORDER", "DATE"): 1.0,
    }
    tpch_chain = {
        edge_key("CUSTOMER", "ORDERS"): 1.0 / 150_000,
        edge_key("ORDERS", "LINEITEM"): 4.0 / 1_500_000,
        edge_key("LINEITEM", "PART"): 0.7 / 200_000,
    }

    return [
        Workload(
            "SSB-1",
            "SSB",
            "star",
            "PART保留率高但事实表很大",
            "维度过滤收益与事实表扫描成本冲突",
            ssb_tables(60_000_000, 0.50, 1.00, 1.00, 1.00),
            "LINEORDER",
            ssb_edges,
        ),
        Workload(
            "SSB-2",
            "SSB",
            "star",
            "PART保留率低且事实表较小",
            "强过滤是否仍值得优先于小事实表扫描",
            ssb_tables(2_000_000, 0.05, 1.00, 1.00, 1.00),
            "LINEORDER",
            ssb_edges,
        ),
        Workload(
            "SSB-3",
            "SSB",
            "star",
            "LINEORDER低于L3 cache边界",
            "cache内负载下join order差异是否收敛",
            ssb_tables(700_000, 0.30, 0.40, 0.60, 0.20),
            "LINEORDER",
            ssb_edges,
        ),
        Workload(
            "SSB-4",
            "SSB",
            "star",
            "LINEORDER显著超过L3 cache边界",
            "cache外负载下早期缩减是否主导",
            ssb_tables(120_000_000, 0.30, 0.40, 0.60, 0.20),
            "LINEORDER",
            ssb_edges,
        ),
        Workload(
            "SSB-5",
            "SSB",
            "star",
            "PART键分布倾斜",
            "PRO/NPO面对skew时的partition/build端选择风险",
            ssb_tables(30_000_000, 0.20, 0.80, 0.70, 0.50),
            "LINEORDER",
            ssb_edges,
            skew=1.35,
        ),
        Workload(
            "SSB-6",
            "SSB",
            "star",
            "PART与SUPPLIER过滤方向冲突",
            "大维度强过滤与小维度弱过滤的优先级",
            ssb_tables(60_000_000, 0.08, 0.75, 0.45, 0.90),
            "LINEORDER",
            ssb_edges,
        ),
        Workload(
            "TPCH-1",
            "TPC-H",
            "chain",
            "链式增长型中间结果爆炸",
            "早期小表join可能引发后续膨胀",
            tpch_tables(150_000, 1_500_000, 6_000_000, 200_000, 0.90, 0.80, 0.85, 0.70),
            None,
            {
                edge_key("CUSTOMER", "ORDERS"): 1.0 / 150_000,
                edge_key("ORDERS", "LINEITEM"): 5.2 / 1_500_000,
                edge_key("LINEITEM", "PART"): 0.9 / 200_000,
            },
        ),
        Workload(
            "TPCH-2",
            "TPC-H",
            "chain",
            "链式收缩型",
            "经典早过滤是否应先靠近高选择性节点",
            tpch_tables(150_000, 1_500_000, 6_000_000, 200_000, 0.20, 0.35, 0.08, 0.10),
            None,
            tpch_chain,
        ),
        Workload(
            "TPCH-3",
            "TPC-H",
            "chain",
            "早期1%强过滤",
            "强过滤节点不在链端时的join order选择",
            tpch_tables(150_000, 1_500_000, 6_000_000, 200_000, 1.00, 0.01, 0.10, 0.50),
            None,
            tpch_chain,
        ),
        Workload(
            "TPCH-4",
            "TPC-H",
            "chain",
            "小表弱过滤与大表强过滤冲突",
            "保留率、表规模、连接位置三者冲突",
            tpch_tables(150_000, 1_500_000, 6_000_000, 200_000, 0.90, 0.90, 0.10, 0.80),
            None,
            tpch_chain,
        ),
        Workload(
            "TPCH-5",
            "TPC-H",
            "chain",
            "ORDERS与LINEITEM早join产生大中间结果",
            "避免爆炸型early join是否比局部最小输入更重要",
            tpch_tables(150_000, 1_500_000, 6_000_000, 200_000, 0.80, 0.95, 0.95, 0.15),
            None,
            {
                edge_key("CUSTOMER", "ORDERS"): 1.0 / 150_000,
                edge_key("ORDERS", "LINEITEM"): 6.0 / 1_500_000,
                edge_key("LINEITEM", "PART"): 0.18 / 200_000,
            },
        ),
        Workload(
            "TPCH-6",
            "TPC-H",
            "chain",
            "cache边界敏感型",
            "cache内join优先与高缩减join优先的冲突",
            tpch_tables(80_000, 600_000, 8_000_000, 120_000, 0.70, 0.55, 0.35, 0.20),
            None,
            tpch_chain,
        ),
    ]


def relation_mib(rows: float, width: int) -> float:
    return rows * width / (1024.0 * 1024.0)


def cache_multiplier(rows: float, width: int, l3_mib: float, slope: float) -> Tuple[float, bool]:
    mib = relation_mib(rows, width)
    if mib <= l3_mib:
        return 1.0, False
    return 1.0 + slope * math.log2(max(mib / l3_mib, 1.0)), True


def enumerate_orders(workload: Workload) -> List[Tuple[str, ...]]:
    names = list(workload.tables.keys())
    if workload.graph == "star":
        assert workload.fact_table is not None
        dims = [name for name in names if name != workload.fact_table]
        return [(workload.fact_table, *perm) for perm in itertools.permutations(dims)]

    orders: List[Tuple[str, ...]] = []

    def adjacent_candidates(joined: set[str]) -> List[str]:
        candidates = []
        for name in names:
            if name in joined:
                continue
            if any(edge_key(name, old) in workload.edges for old in joined):
                candidates.append(name)
        return candidates

    def rec(prefix: Tuple[str, ...], joined: set[str]) -> None:
        if len(prefix) == len(names):
            orders.append(prefix)
            return
        for candidate in adjacent_candidates(joined):
            rec((*prefix, candidate), joined | {candidate})

    for name in names:
        rec((name,), {name})
    return orders


def join_output_rows(workload: Workload, joined: set[str], current_rows: float, nxt: str) -> float:
    if workload.graph == "star":
        return current_rows * workload.tables[nxt].pass_rate * workload.edges[edge_key(workload.fact_table or "", nxt)]

    possible = [old for old in joined if edge_key(old, nxt) in workload.edges]
    if not possible:
        raise ValueError(f"Illegal disconnected join: {joined} -> {nxt}")
    selectivity = min(workload.edges[edge_key(old, nxt)] for old in possible)
    return current_rows * workload.tables[nxt].effective_rows * selectivity


def step_cost(
    left_rows: float,
    left_width: int,
    right_rows: float,
    right_width: int,
    out_rows: float,
    profile: AlgorithmProfile,
    workload: Workload,
    l3_mib: float,
) -> Tuple[float, bool]:
    left_cache, left_cross = cache_multiplier(left_rows, left_width, l3_mib, profile.cache_slope)
    right_cache, right_cross = cache_multiplier(right_rows, right_width, l3_mib, profile.cache_slope)
    cache = max(left_cache, right_cache)
    skew = 1.0 + profile.skew_slope * max(workload.skew - 1.0, 0.0)

    if profile.name == "Sort-Merge Join":
        sort_left = left_rows * max(math.log2(max(left_rows, 2.0)), 1.0)
        sort_right = right_rows * max(math.log2(max(right_rows, 2.0)), 1.0)
        raw = profile.prep * (sort_left + sort_right) + profile.scan * (left_rows + right_rows)
    elif profile.name == "Partitioned Radix Hash Join":
        raw = profile.prep * (left_rows + right_rows) + profile.join * (left_rows + right_rows)
    else:
        build = min(left_rows, right_rows)
        probe = max(left_rows, right_rows)
        raw = profile.prep * build + profile.join * probe + profile.scan * (left_rows + right_rows) * 0.25

    raw += profile.materialize * out_rows
    return raw * cache * skew + profile.startup, left_cross or right_cross


def evaluate_order(
    workload: Workload,
    order: Sequence[str],
    profile: AlgorithmProfile,
    l3_mib: float,
) -> Dict[str, object]:
    first = order[0]
    current_rows = workload.tables[first].effective_rows
    current_width = workload.tables[first].width
    joined = {first}
    total_cost = profile.startup + profile.scan * current_rows
    intermediates: List[float] = [current_rows]
    cache_crossings = 0

    for nxt in order[1:]:
        right = workload.tables[nxt]
        out_rows = max(join_output_rows(workload, joined, current_rows, nxt), 1.0)
        cost, crossed = step_cost(
            current_rows,
            current_width,
            right.effective_rows,
            right.width,
            out_rows,
            profile,
            workload,
            l3_mib,
        )
        total_cost += cost
        current_rows = out_rows
        current_width = min(current_width + right.width, 128)
        joined.add(nxt)
        intermediates.append(current_rows)
        cache_crossings += int(crossed)

    max_rows = max(intermediates)
    max_mib = max(relation_mib(rows, current_width) for rows in intermediates)
    return {
        "order": " -> ".join(order),
        "cost": total_cost,
        "max_intermediate_rows": max_rows,
        "max_intermediate_mib": max_mib,
        "cache_crossings": cache_crossings,
    }


def heuristic_order(workload: Workload) -> Tuple[str, ...]:
    if workload.graph == "star":
        assert workload.fact_table is not None
        dims = [name for name in workload.tables if name != workload.fact_table]
        dims.sort(key=lambda name: workload.tables[name].effective_rows)
        return (workload.fact_table, *dims)

    names = list(workload.tables.keys())
    start = min(names, key=lambda name: workload.tables[name].effective_rows)
    prefix = (start,)
    joined = {start}
    while len(prefix) < len(names):
        candidates = [
            name
            for name in names
            if name not in joined and any(edge_key(name, old) in workload.edges for old in joined)
        ]
        nxt = min(candidates, key=lambda name: workload.tables[name].effective_rows)
        prefix = (*prefix, nxt)
        joined.add(nxt)
    return prefix


def run_experiment(l3_mib: float) -> List[Dict[str, object]]:
    records: List[Dict[str, object]] = []
    for workload in build_workloads():
        orders = enumerate_orders(workload)
        h_order = heuristic_order(workload)
        for profile in ALGORITHMS:
            evaluated = [evaluate_order(workload, order, profile, l3_mib) for order in orders]
            best = min(evaluated, key=lambda item: float(item["cost"]))
            h_eval = evaluate_order(workload, h_order, profile, l3_mib)
            records.append(
                {
                    "workload_id": workload.wid,
                    "benchmark": workload.benchmark,
                    "description": workload.description,
                    "conflict": workload.conflict,
                    "algorithm": profile.name,
                    "query_model": profile.model,
                    "best_order": best["order"],
                    "normalized_cost": round(float(best["cost"]) / 1_000_000.0, 4),
                    "max_intermediate_rows": int(float(best["max_intermediate_rows"])),
                    "max_intermediate_mib": round(float(best["max_intermediate_mib"]), 2),
                    "cache_crossings": int(best["cache_crossings"]),
                    "skew_factor": workload.skew,
                    "heuristic_order": h_eval["order"],
                    "heuristic_cost": round(float(h_eval["cost"]) / 1_000_000.0, 4),
                    "heuristic_slowdown": round(float(h_eval["cost"]) / float(best["cost"]), 3),
                }
            )
    return records


def write_csv(records: List[Dict[str, object]]) -> Path:
    path = RESULT_DIR / "join_order_results.csv"
    with path.open("w", newline="", encoding="utf-8-sig") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(records[0].keys()))
        writer.writeheader()
        writer.writerows(records)
    return path


def write_json(records: List[Dict[str, object]]) -> Path:
    path = RESULT_DIR / "join_order_results.json"
    path.write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding="utf-8")
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description="Run SSB/TPC-H join-order study.")
    parser.add_argument("--l3-mib", type=float, default=DEFAULT_L3_MIB, help="L3 cache boundary in MiB.")
    args = parser.parse_args()

    RESULT_DIR.mkdir(exist_ok=True)

    records = run_experiment(args.l3_mib)
    csv_path = write_csv(records)
    json_path = write_json(records)
    design_path = write_workload_design()

    print(f"records={len(records)}")
    print(f"csv={csv_path}")
    print(f"json={json_path}")
    print(f"design={design_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())