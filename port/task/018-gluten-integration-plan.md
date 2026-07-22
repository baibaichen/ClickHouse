# Task 018: Gluten Velox-backend FileCache 集成 —— 实施 Plan

> 目标:让真实 Spark/Gluten 查询的 Hive 读路径能走到我们移植的 FileCache
> (`velox/ch/`),经 `FileCacheManager` + 018a 的 `BufferedInputBuilder`,无裸单例。
> 在 `gluten2` worktree(用 filecache2 fork)上做;完事把成果整理回 `filecache2`。

## 地基(已全部坐实,不重验)

- 可行性:`./dev/builddeps-veloxbe.sh --velox_home=/home/chang/OpenSource/velox ...` 让 Gluten 用我们 fork(实测编过、链的是本 fork)。
- 依赖面:IBM/velox `dft-2026_07_10` vs 我们基线 `f4042228a`,`BufferedInputBuilder::create`/`ConnectorQueryCtx`/`FileHandle`/`BufferedInput` ctor/`registerBuilder` 逐字节相同(SAFE)。
- `velox/ch/` 在 GCC13+`-Werror` 全绿;全部 `velox_ch_*` target 编链通过。
- 环境:gluten2 = gluten1 的 worktree,detached @ `origin/main`(36beabd41),tracked clean。

## 接入点(gluten2 内,file:line 已核实)

| 位置 | 现状 | 018 改动 |
|---|---|---|
| `cpp/velox/compute/VeloxBackend.cc:250` | `BufferedInputBuilder::registerBuilder(make_shared<GlutenBufferedInputBuilder>())` | 改注册一个**三分支 builder**(见下) |
| `cpp/velox/compute/VeloxBackend.cc:295` | `initCache()`(建 `asyncDataCache_`,仅当 `kVeloxCacheEnabled`) | 其后加 `initFileCache()` |
| `cpp/velox/compute/VeloxBackend.cc:461-472` `tearDown` | `asyncDataCache_->shutdown()` | 加 `fileCacheManager_->shutdown()` |
| `cpp/velox/compute/VeloxBackend.h` | `asyncDataCache_` 等成员 | 加 `std::shared_ptr<FileCacheManager> fileCacheManager_` |
| `cpp/velox/config/VeloxConfig.h:126+` | `kVeloxCacheEnabled` 等 | 加 `kVeloxFileCache*` 配置键 |
| `cpp/velox/memory/GlutenBufferedInputBuilder.h` | 二分支(cache→cbi / else→direct) | 扩成三分支(见形态 A) |

## 形态 A:三分支 builder(已定,用户选)

`registerBuilder` 是全局单例,只能装一个 builder。不能把我们的 builder 和 `GlutenBufferedInputBuilder` 并列(后注册覆盖前者,丢 cbi/direct)。所以**扩展现有 builder** 成三分支,`create()` 里:

```
1. connectorQueryCtx->cache() != nullptr        → CachedBufferedInput (cbi,Gluten 原行为)
2. 否则 FileCacheManager 有默认 cache(hasDefault)→ FileCacheBufferedInput (我们的 filecache)
3. 否则                                          → GlutenDirectBufferedInput (direct,Gluten 原行为)
```

- 分支 1/3 保持 Gluten 现有 `GlutenBufferedInputBuilder` 行为逐字不变。
- 分支 2 复用 018a 的 `FileCacheBufferedInput` 构造(签名已与 Gluten builder 逐字匹配)。
- builder 需持有 `FileCacheManager&`(和 018a 的 `FileCacheBufferedInputBuilder` 一样);注册时传入。
- **互斥保证**:分支 1 优先——若同时装了 AsyncDataCache 和 FileCache,走 cbi。真正启用 filecache 要求 `kVeloxCacheEnabled=false`(不建 asyncDataCache_)+ filecache 开启,使 `connectorQueryCtx->cache()==nullptr`。这条约束写进配置校验:两者不可同时 enabled,否则 init 期 `VELOX_USER_CHECK` 报错(fail-close,不静默二选一)。

实现选择(worker 择优,二者等价):
- (a) 直接改 `GlutenBufferedInputBuilder` 加第 3 分支 + 持 `FileCacheManager*`(可空,空则退化为原二分支);
- (b) 新建 gluten 侧子类复用 018a builder。
推荐 (a):改动集中、只一个 builder 类、Manager 空时行为与今天完全一致。

## initFileCache()(照 AbBenchmarkMain.cpp:51-88 的 buildFileCacheManager 范例)

```cpp
void VeloxBackend::initFileCache() {
  if (!backendConf_->get<bool>(kVeloxFileCacheEnabled, false)) return;
  VELOX_USER_CHECK(!backendConf_->get<bool>(kVeloxCacheEnabled, false),
      "FileCache and Velox AsyncDataCache cannot both be enabled");
  FileCacheManager::Options opt;
  opt.commonUserId = "gluten";
  opt.localFileSystem = filesystems::getFileSystem("/", nullptr);
  opt.caches = { NamedFileCacheConfig{ name="default", root=<kVeloxFileCacheRoot>,
                   maxSize=<kVeloxFileCacheSize>, ... } };  // 字段照 buildFileCacheManager
  opt.defaultCacheName = "default";
  fileCacheManager_ = FileCacheManager::create(opt);
  FileCacheManager::setInstance(fileCacheManager_.get());   // 018a 需要;进程级
  // 三分支 builder 在 :250 注册时已持有 manager 引用(或此处 register)
}
```
- Manager 生命周期 = backend 成员,`tearDown` 里 `shutdown()` + `setInstance(nullptr)`。
- `NamedFileCacheConfig` 的确切字段以 `buildFileCacheManager` 现成代码为准(worker 逐字对照)。

## 配置键(照 VeloxConfig.h:126 命名规范)

```
spark.gluten.sql.columnar.backend.velox.fileCacheEnabled   (bool, default false)
spark.gluten.sql.columnar.backend.velox.fileCacheRoot      (string, 磁盘缓存目录)
spark.gluten.sql.columnar.backend.velox.fileCacheSize      (uint64, bytes, 磁盘缓存上限)
```
（键名/默认值 worker 按 kVelox* 现有风格定稿；三个键 + 与 cacheEnabled 互斥校验。）

## 文件范围(gluten2 内,均 Gluten 生产代码 —— 018 首次改 Gluten)

```
改:cpp/velox/memory/GlutenBufferedInputBuilder.h   (三分支 + 持 FileCacheManager*)
改:cpp/velox/compute/VeloxBackend.h                (fileCacheManager_ 成员 + initFileCache 声明)
改:cpp/velox/compute/VeloxBackend.cc               (:250 注册三分支;:295 后 initFileCache;tearDown shutdown)
改:cpp/velox/config/VeloxConfig.h                  (3 个 kVeloxFileCache* 键)
可能改:cpp/velox/CMakeLists.txt                     (若需链 velox_ch_filecache 到 libgluten)
```
- 不碰 ClickHouse backend、不碰 velox 主干、不碰我们 fork 的 `velox/ch/`(018a 已就位)。

## 验证路径(gluten2)

1. **编译门槛**:重跑 `builddeps-veloxbe.sh --velox_home=... build_gluten_cpp`,libgluten.so 链出、含 FileCache 符号。
2. **native 单测**(若 Gluten 有 backend C++ 单测框架):构造 `connectorQueryCtx->cache()==nullptr` + FileCache 开启,断言 builder 返回 `FileCacheBufferedInput`、读命中缓存。
3. **E2E(Task 019 的关口,018 冒烟即可)**:一个最小 Spark 查询(如 `backends-velox` 下 VeloxTPCH 套件某条),开 `fileCacheEnabled=true`+`cacheEnabled=false`,跑通、且 FileCache 命中三件套 ProfileEvents 非零(证明真走了我们的缓存)。完整 E2E 归 019。
4. **回归**:`cacheEnabled=true`(原 cbi 路径)仍走 CachedBufferedInput 不变;两者都关走 direct 不变。

## 约束与规矩

- worker/controller file-only;只 Controller 提交;本地 commit,绝不 push;无 `-j`;日志入 gluten2 build 目录。
- gluten2 上开发;成果整理回 filecache2 线(builder/backend 改动属 Gluten,留 gluten2;filecache2 fork 侧无新增,018a 已够)。
- 首次改 Gluten 生产代码 —— 每处改动 worker+RED+Controller 独立复现验证,尤其分支 1/3 的 Gluten 原行为不得回归。
- 互斥校验 fail-close:双缓存同开必须 init 期报错,不静默选一个。

## 风险

- R1:`libgluten.so` 需链到 `velox_ch_filecache`(我们 fork 的 FileCache 库)才能解析 `FileCacheBufferedInput`/`registerFileCacheBufferedInputBuilder` 符号。若 Gluten CMake 没自动带,需在 `cpp/velox/CMakeLists.txt` 显式链 —— 这是 018 最可能的 CMake 工作量。
- R2:`FileCacheManager::setInstance` 是进程级单例;Gluten 多 backend 实例/重复 init 的生命周期需确认(单进程单 backend 应无碍,worker 核实 init 只跑一次)。
- R3:E2E 需要一个能开 `fileCacheEnabled` 的 Spark 配置通路 —— 确认 Gluten 配置从 Spark conf 传到 `backendConf_` 的链路对新键生效。
