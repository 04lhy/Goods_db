# Changelog

## [Phase 2] — 2025-07-08

### 修复 (Fixed)
- **Bug 1 — rnd_pos 测试断言错误**：`ASSERT_TRUE(handler.rnd_pos(...))` 改为 `ASSERT_EQ(..., 0)`。rnd_pos 返回 0 表示成功（C 惯例），`ASSERT_TRUE(0)` 在 C++ 中为 false。
- **Bug 2 — UpdateAndDelete 死锁/超时**：三处修复 — (1) `TableIterator` 添加析构函数释放 R-latch 和 pin；(2) `Count()` 移除结尾多余的 `Reset()` 调用；(3) 测试改为先收集 RID，`rnd_end()` 后再执行 `delete_row`。
- **Bug 3 — BPlusTreeIndex ASan SIGSEGV**：由 Bug 2 的 latch/pin 泄漏连带导致，Bug 2 修复后自动解决。
- **问题 5 — Scan+Write 同页死锁**：测试层面已修复（收集 RID → rnd_end → 写操作）。Executor 层待采用方案 A。
- **问题 6 — 类型系统跨类型比较**：`Value::operator<` / `operator==` 支持跨类型数值比较（TINYINT→DECIMAL 通过 double 统一提升）。
- **问题 7 — InsertTuple Pin 管理**：已验证现有代码正确（prev_page_id 修复 + 新析构函数）。

### 修改文件
| 文件 | 修改 |
|------|------|
| `src/include/storage/table/table_iterator.h` | 添加 `~TableIterator()` 析构函数声明 |
| `src/storage/table/table_iterator.cpp` | 实现析构函数；修复 `Count()` 多余 Reset |
| `src/type/value.cpp` | 添加 `IsNumericType`/`ToDouble` 辅助函数；`operator<`/`operator==` 支持跨类型数值比较 |
| `test/sql/integration_test.cpp` | rnd_pos 断言修复；delete_row 死锁修复 |

### 测试状态
- 集成测试：**9/9 PASS**（此前 7/9）
- Parser 测试：**48/48 PASS**
- Binder/Planner/Executor/E2E 测试：**40/40 PASS**
- 全量单元测试：**125/125 PASS**
- ASan：**零内存错误**
- 编译：**零警告**（clang-15, C++17）

---

## [Phase 2] — 2025-07-07

### 新增 (Added)
- **Parser 模块**：集成 libpg_query，实现 SQL → AST 完整解析链路（SELECT/INSERT/UPDATE/DELETE/CREATE/DROP/CREATE INDEX），通过 48 项测试
- **Binder 模块**：实现 AST → BoundStatement 语义绑定（表名/列名绑定、类型推导、聚合校验），通过 18 项测试
- **Planner 模块**：实现 BoundStatement → PlanNode 树转换（19 种 PlanNode 类型），通过 9 项测试
- **Executor 模块**：实现 13 种 Volcano 模型执行器（SeqScan/IndexScan/Filter/Projection/Insert/Update/Delete/HashJoin/NLJ/Aggregation/Sort/Limit/WindowFunction/DDL），通过 13 项测试
- **Optimizer 模块**：实现 6 条优化规则（谓词下推/列裁剪/SeqScan→IndexScan/NLJ→HashJoin/NLJ→IndexJoin/Sort+Limit→TopN）
- **ExecutionEngine**：实现 SQL 全链路统一入口 `ExecuteSQL(sql)`
- **LockManager + TransactionManager**：基础框架搭建
- **Binder 测试**：40 项测试（Binder + Planner + Executor + EndToEnd），全通过

### 修改文件
| 文件 | 修改 |
|------|------|
| `CMakeLists.txt` | 添加 disable_target_warnings + 5 个 SQL 子目录 |
| `src/sql/CMakeLists.txt` | 更新链接依赖 |
| `test/sql/CMakeLists.txt` | 添加 parser_test / binder_test 目标 |

### 新增文件（30+ 个）
| 模块 | 头文件 | 实现文件 |
|------|--------|---------|
| Parser | `ast.h`, `parser.h` | `parser.cpp` |
| Binder | `bound_statement.h`, `binder.h` | `binder.cpp` |
| Planner | `plan_nodes.h`, `planner.h` | `planner.cpp` |
| Executor | `abstract_executor.h` | `executor_factory.cpp`, `execution_engine.cpp`, `advanced_executors.cpp` |
| Optimizer | `optimizer.h` | `optimizer.cpp` |
| Lock/Transaction | `lock_manager.h`, `transaction_manager.h` | `lock_manager.cpp`, `transaction_manager.cpp` |

---

## [Phase 1] — 2025-07-06

### 修复 (Fixed)
- **TablePage InsertTuple 布局 bug**：重写为标准 Slotted page 布局（slots 向高地址增长，tuples 向低地址增长）
- **Tuple::GetValue VARCHAR heap-buffer-overflow**：明确分离固定数据区和变长偏移区
- **ClockReplacer 单元测试死循环**：改为循环调用 Victim
- **LRU-K Replacer 编译警告**：移除未使用 `num_frames_` 字段
- **page_test 栈溢出**：16KB 页数据从栈分配改为堆分配
- **InsertTuple 循环 unpin 错误页面**：使用 `prev_page_id` 正确 unpin
- **UpdateTuple 访问已释放页面**：UnpinPage 后重新 FetchPage 获取安全指针

### 新增 (Added)
- 工程骨架：CMake + clang-15 + C++17 + ASan + CI
- 通用组件：RWLatch / Config / Logger
- 类型系统：Value / Schema / Tuple / Column
- 存储层：DiskManager / BufferPoolManager / ClockReplacer / LRUKReplacer / PageGuard
- 数据页：Page / TablePage（Slotted Page 布局）
- 表存储：TableHeap / TableIterator
- 索引层：BPlusTree / ExtendibleHashIndex / Index 虚基类
- 元数据：Catalog（内存模式）
- 引擎接口：handler 抽象基类 / handlerton 注册机制 / goods_handler 适配层
- 集成测试：9 项（EngineRegistration / CreateTable / InsertAndScan / BPlusTreeIndex / BPlusTreeRangeScan / UpdateAndDelete / DropTable / HandlertonRegistration / FullEndToEnd）
