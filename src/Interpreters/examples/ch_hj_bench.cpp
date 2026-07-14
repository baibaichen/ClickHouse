/// task30: CH native HashJoin build+probe driver, timed with Google Benchmark.
/// Layer-1 vs full-join arm is chosen by env CH_HJ_LAYER1_ONLY (read inside the probe
/// hot loop via chHjLayer1Only() in HashJoinMethodsImpl.h). Both arms exist in one dbms
/// build; no separate -DLAYER1_ONLY compile is needed for the debug run-through.
/// gbenchmark_all supplies main(); do NOT define main() here.
#include <cstdlib>
#include <iostream>
#include <memory>

#include <benchmark/benchmark.h>

#include <Columns/ColumnsNumber.h>
#include <Core/Block.h>
#include <Core/ColumnWithTypeAndName.h>
#include <Core/Names.h>
#include <DataTypes/DataTypesNumber.h>
#include <Interpreters/HashJoin/HashJoin.h>
#include <Interpreters/IJoin.h>
#include <Interpreters/TableJoin.h>

using namespace DB;

namespace
{

Block makeBlock(UInt64 begin, UInt64 end, const String & key_name, bool with_payload)
{
    auto key = ColumnUInt64::create();
    for (UInt64 i = begin; i < end; ++i)
        key->insert(i);

    Block block;
    block.insert(ColumnWithTypeAndName{std::move(key), std::make_shared<DataTypeUInt64>(), key_name});

    if (with_payload)
    {
        auto payload = ColumnUInt64::create();
        for (UInt64 i = begin; i < end; ++i)
            payload->insert(i * 10);
        block.insert(ColumnWithTypeAndName{std::move(payload), std::make_shared<DataTypeUInt64>(), "v"});
    }
    return block;
}

constexpr UInt64 BUILD_N = 100000;   /// right keys 0..99999
constexpr UInt64 PROBE_N = 50000;    /// left keys  0..49999 (fanout 1 -> 50000 matches)

std::shared_ptr<TableJoin> makeTableJoin()
{
    auto tj = std::make_shared<TableJoin>(
        SizeLimits{}, /*use_nulls*/ false, JoinKind::Inner, JoinStrictness::All, Names{"k"});
    tj->setLeftKeys(Names{"k"});
    return tj;
}

/// Build a fully-populated HashJoin (build phase finished) ready to probe.
std::shared_ptr<HashJoin> buildJoin()
{
    Block right_sample = makeBlock(0, 0, "k", true);
    auto hj = std::make_shared<HashJoin>(makeTableJoin(), std::make_shared<const Block>(right_sample));
    Block build_block = makeBlock(0, BUILD_N, "k", true);
    hj->addBlockToJoin(build_block, /*check_limits*/ true);
    hj->onBuildPhaseFinish();
    return hj;
}

/// Run one probe pass; return output-row count (materialized rows emitted).
size_t probeOnce(HashJoin & hj)
{
    Block probe_block = makeBlock(0, PROBE_N, "k", false);
    JoinResultPtr result = hj.joinBlock(std::move(probe_block));
    size_t out_rows = 0;
    while (true)
    {
        auto rb = result->next();
        out_rows += rb.block.rows();
        if (rb.is_last)
            break;
    }
    return out_rows;
}

bool layer1Env()
{
    const char * e = std::getenv("CH_HJ_LAYER1_ONLY");
    return e != nullptr && e[0] == 1;
}

/// BUILD timing: fresh HashJoin build+finish each iteration.
void BM_ChHashJoinBuild(benchmark::State & state)
{
    for (auto _ : state)
    {
        auto hj = buildJoin();
        benchmark::DoNotOptimize(hj.get());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(BUILD_N));
}
BENCHMARK(BM_ChHashJoinBuild);

/// PROBE timing: reuse one prebuilt table, time probe only. Arm chosen by env.
void BM_ChHashJoinProbe(benchmark::State & state)
{
    auto hj = buildJoin();
    size_t out_rows = 0;
    for (auto _ : state)
    {
        out_rows = probeOnce(*hj);
        benchmark::DoNotOptimize(out_rows);
    }
    /// Correctness: full arm materializes PROBE_N rows; layer-1 arm materializes 0
    /// (matches confirmed but not expanded). Reported as a counter for the run-through.
    state.counters["out_rows"] = static_cast<double>(out_rows);
    state.counters["layer1_only"] = layer1Env() ? 1.0 : 0.0;
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(PROBE_N));
}
BENCHMARK(BM_ChHashJoinProbe);

}
