/// task30/task32: CH native HashJoin build+probe driver, timed with Google Benchmark.
/// Layer-1 vs full-join arm chosen by env CH_HJ_LAYER1_ONLY (read inside probe hot loop
/// via chHjLayer1Only() in HashJoinMethodsImpl.h). Both arms in one dbms build.
/// gbenchmark_all supplies main(); do NOT define main() here.
///
/// task32 extends the single fixed-BIGINT case into a suite sweep aligned to the
/// velox-side layer-1 benchmark (ChHashTableLayerBenchmark.cpp): fixed key64
/// sequential+uniform, multi-column (2xBIGINT, BIGINT+2xINT32), and strings
/// (short/long x low/high cardinality). Data gen mirrors the velox splitMix64 uniform
/// / row sequential distributions so the three-way boundary is aligned.
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include <Columns/ColumnsNumber.h>
#include <Columns/ColumnString.h>
#include <Core/Block.h>
#include <Core/ColumnWithTypeAndName.h>
#include <Core/Names.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeString.h>
#include <Interpreters/HashJoin/HashJoin.h>
#include <Interpreters/HashJoin/HashJoinMethodsImpl.h>
#include <Interpreters/IJoin.h>
#include <Interpreters/TableJoin.h>

using namespace DB;

namespace
{

const UInt64 BUILD_N = []{ const char* e=std::getenv("CH_HJ_N"); return e?std::strtoull(e,nullptr,10):100000ULL; }();
const UInt64 PROBE_N = BUILD_N;

enum class Layout
{
    Bigint,
    TwoBigint,
    BigintTwoInt,
    Varchar,
};

struct SuiteSpec
{
    Layout layout;
    bool sequential;
    bool stringLong;
    bool stringLowCard;
};

UInt64 splitMix64(UInt64 value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

Int64 keyAt(UInt64 row, bool sequential)
{
    if (sequential)
        return static_cast<Int64>(row);
    return static_cast<Int64>(splitMix64(row));
}

std::string stringKeyAt(UInt64 row, bool longStr, bool lowCard)
{
    const UInt64 value = lowCard ? (row % 1024) : static_cast<UInt64>(keyAt(row, false));
    std::string key = std::to_string(value);
    if (longStr && key.size() < 128)
        key.append(128 - key.size(), static_cast<char>('a' + value % 26));
    return key;
}

Names keyNames(const SuiteSpec & s)
{
    switch (s.layout)
    {
        case Layout::Bigint:
        case Layout::Varchar: return Names{"k0"};
        case Layout::TwoBigint: return Names{"k0", "k1"};
        case Layout::BigintTwoInt: return Names{"k0", "k1", "k2"};
    }
    return Names{"k0"};
}

Block makeBlock(UInt64 begin, UInt64 end, const SuiteSpec & s, bool with_payload)
{
    const UInt64 n = end - begin;
    Block block;

    if (s.layout == Layout::Varchar)
    {
        auto k0 = ColumnString::create();
        for (UInt64 i = begin; i < end; ++i)
        {
            std::string v = stringKeyAt(i, s.stringLong, s.stringLowCard);
            k0->insertData(v.data(), v.size());
        }
        block.insert(ColumnWithTypeAndName{std::move(k0), std::make_shared<DataTypeString>(), "k0"});
    }
    else
    {
        auto k0 = ColumnInt64::create();
        for (UInt64 i = begin; i < end; ++i)
            k0->insert(keyAt(i, s.sequential));
        block.insert(ColumnWithTypeAndName{std::move(k0), std::make_shared<DataTypeInt64>(), "k0"});

        if (s.layout == Layout::TwoBigint)
        {
            auto k1 = ColumnInt64::create();
            for (UInt64 i = begin; i < end; ++i)
                k1->insert(keyAt(i, s.sequential) ^ 0x5a5a5a5a5a5a5a5aLL);
            block.insert(ColumnWithTypeAndName{std::move(k1), std::make_shared<DataTypeInt64>(), "k1"});
        }
        else if (s.layout == Layout::BigintTwoInt)
        {
            auto k1 = ColumnInt32::create();
            auto k2 = ColumnInt32::create();
            for (UInt64 i = begin; i < end; ++i)
            {
                k1->insert(static_cast<Int32>(i));
                k2->insert(static_cast<Int32>(i * 17));
            }
            block.insert(ColumnWithTypeAndName{std::move(k1), std::make_shared<DataTypeInt32>(), "k1"});
            block.insert(ColumnWithTypeAndName{std::move(k2), std::make_shared<DataTypeInt32>(), "k2"});
        }
    }

    if (with_payload)
    {
        auto payload = ColumnUInt64::create();
        for (UInt64 i = 0; i < n; ++i)
            payload->insert(i * 10);
        block.insert(ColumnWithTypeAndName{std::move(payload), std::make_shared<DataTypeUInt64>(), "v"});
    }
    return block;
}

std::shared_ptr<TableJoin> makeTableJoin(const SuiteSpec & s)
{
    Names ks = keyNames(s);
    auto tj = std::make_shared<TableJoin>(
        SizeLimits{}, /*use_nulls*/ false, JoinKind::Inner, JoinStrictness::All, ks);
    tj->setLeftKeys(ks);
    return tj;
}

std::shared_ptr<HashJoin> buildJoinFromBlock(const SuiteSpec & s, const Block & build_block)
{
    Block right_sample = makeBlock(0, 0, s, true);
    auto hj = std::make_shared<HashJoin>(makeTableJoin(s), std::make_shared<const Block>(right_sample));
    hj->addBlockToJoin(build_block, /*check_limits*/ true);
    hj->onBuildPhaseFinish();
    return hj;
}

std::shared_ptr<HashJoin> buildJoin(const SuiteSpec & s)
{
    return buildJoinFromBlock(s, makeBlock(0, BUILD_N, s, true));
}

size_t probeOnce(HashJoin & hj, Block probe_block)
{
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
    return e != nullptr && e[0] == '1';
}

void runBuild(benchmark::State & state, SuiteSpec s)
{
    /// Generate build data once, outside the timing loop, so the ~512MB string
    /// materialization (4M x stringKeyAt+insertData for the long-varchar suites) plus
    /// the payload column are not counted as build time. addBlockToJoin takes a
    /// const Block &, so passing the pre-built block by reference is safe and cheap
    /// (no re-materialization). Each iteration still constructs a fresh HashJoin and
    /// re-runs addBlockToJoin + onBuildPhaseFinish, which is what the build benchmark
    /// measures. This mirrors the velox-side layer-1 benchmark, which builds data once.
    const Block build_block = makeBlock(0, BUILD_N, s, true);
    for (auto _ : state)
    {
        auto hj = buildJoinFromBlock(s, build_block);
        benchmark::DoNotOptimize(hj.get());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(BUILD_N));
    /// Emit the row count so the three-way analysis script can attach each CH suite
    /// to the matching (distribution, rows, key_layout) cell without inference.
    state.counters["build_n"] = static_cast<double>(BUILD_N);
}

void runProbe(benchmark::State & state, SuiteSpec s)
{
    auto hj = buildJoin(s);
    /// Generate probe data once, outside the timing loop, so the ~512MB string
    /// materialization (4M x stringKeyAt+insertData for the long-varchar suites) is
    /// not counted as probe time. joinBlock consumes its Block by value, so each
    /// iteration hands it a cheap COW shallow copy of the pre-built probe block
    /// (ColumnPtr shared_ptrs are copied, not the underlying data). This mirrors the
    /// velox-side layer-1 benchmark, which builds probe data once and reuses it.
    const Block probe_block = makeBlock(0, PROBE_N, s, false);
    size_t out_rows = 0;
    for (auto _ : state)
    {
        out_rows = probeOnce(*hj, probe_block);
        benchmark::DoNotOptimize(out_rows);
    }
    state.counters["out_rows"] = static_cast<double>(out_rows);
    state.counters["layer1_only"] = layer1Env() ? 1.0 : 0.0;
    /// Consume the layer-1 observability sink (task T-chbench-deadcode-fix): pull the volatile
    /// value the probe hot loop XORed the found cells into, and hand it to the benchmark so the
    /// whole bucket-walk chain stays observable end-to-end (belt-and-suspenders vs the volatile).
    std::uintptr_t sink = DB::g_ch_hj_layer1_sink;
    benchmark::DoNotOptimize(sink);
    state.counters["l1_sink"] = static_cast<double>(sink & 0xffffu);
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(PROBE_N));
    /// Emit the row count so the three-way analysis script can attach each CH suite
    /// to the matching (distribution, rows, key_layout) cell without inference.
    state.counters["build_n"] = static_cast<double>(BUILD_N);
}

struct Suite { const char * name; SuiteSpec spec; };

const std::vector<Suite> & suites()
{
    static const std::vector<Suite> v = {
        {"bigint_seq",         {Layout::Bigint,       true,  false, false}},
        {"bigint_uniform",     {Layout::Bigint,       false, false, false}},
        {"2xbigint",           {Layout::TwoBigint,    false, false, false}},
        {"bigint_2xint",       {Layout::BigintTwoInt, false, false, false}},
        {"varchar_short_low",  {Layout::Varchar,      false, false, true}},
        {"varchar_short_high", {Layout::Varchar,      false, false, false}},
        {"varchar_long_low",   {Layout::Varchar,      false, true,  true}},
        {"varchar_long_high",  {Layout::Varchar,      false, true,  false}},
    };
    return v;
}

const int register_all = []
{
    for (const auto & su : suites())
    {
        benchmark::RegisterBenchmark((std::string("BM_Build/") + su.name).c_str(),
            [spec = su.spec](benchmark::State & st) { runBuild(st, spec); });
        benchmark::RegisterBenchmark((std::string("BM_ProbeL1/") + su.name).c_str(),
            [spec = su.spec](benchmark::State & st) { runProbe(st, spec); });
    }
    return 0;
}();

}
