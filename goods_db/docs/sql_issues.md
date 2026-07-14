# goods_db SQL 解析器问题跟踪

> **整理时间**：2026/07/13  
> **最后更新**：2026/07/14（Phase 3 Hotfix 完成后复核）  
> **状态**：P0/P1 问题已在 Phase 3 Hotfix 中全部修复，剩余 P2 问题及长期优化项可供后续参考。

---

## 架构概述

goods_db 的 SQL 处理流程：

```
SQL 文本 → execution_engine.cpp (预检查 SHOW/DESCRIBE/CREATE DATABASE 等)
         → parser.cpp (libpg_query，PostgreSQL 解析器封装)
         → binder.cpp (语义绑定：解析列名、表名、类型)
         → planner.cpp (执行计划生成)
         → executor (执行)
```

关键发现：**parser 基于 libpg_query，解析能力本身很强**。问题主要集中在 binder 和 execution_engine 的 SHOW 命令处理上。

---

## ✅ 已修复（Phase 3 Hotfix — 2026/07/13）

### P0-1 ✅ `db.table` 格式的表名解析 {#p0-1}

**修复**：Binder 各 Bind 方法新增 `ResolveTableName()` 辅助函数，自动拆分 `db.table` 格式为实际表名。

### P0-2 ✅ `SHOW TABLES FROM <db>` 忽略 FROM 子句 {#p0-2}

**修复**：`ExecuteShowTables` 现在解析 `FROM <db>` 并过滤数据库，支持 SHOW TABLES FROM goods_db。

### P0-3 ✅ `SHOW COLUMNS` 反引号处理 {#p0-3}

**修复**：新增 `TrimIdentifier()` 去除反引号包裹，修复 `FROM` 子句偏移量计算。

### P1-4 ✅ 不支持 `CREATE DATABASE` / `DROP DATABASE` {#p1-4}

**修复**：在 ExecutionEngine 预检查中添加两个命令的处理，支持 IF NOT EXISTS / IF EXISTS。

### P1-5 ✅ 只支持单数据库 {#p1-5}

**修复**：Catalog 新增 `DatabaseInfo` 层（Database → Table → Index），支持多数据库命名空间。

### P1-6 ✅ `affected_rows` 始终返回 0 {#p1-6}

**修复**：ExecutorContext 新增 `affected_rows` 字段，DML executor（Insert/Update/Delete）自动计数。

### P1-7 ✅ `db.table` 前缀处理不完整 {#p1-7}

**修复**：同 P0-1 的 `ResolveTableName()` 统一处理。

### P2-8 ✅ `default` 作为数据库名是 SQL 保留字 {#p2-8}

**修复**：默认数据库名从 SQL 保留字 `default` 改为 `goods_db`。

### P2-9 ✅ 多语句支持不完善 {#p2-9}

**修复**：HandleQuery 中循环调用 SplitSql → ExecuteSQL，支持分号分隔的多条 SQL。

### P2-11 ✅ JOIN 的表名解析不处理 db 前缀 {#p2-11}

**修复**：同 P0-1，Binder 层统一处理 `db.table` 拆分。

---

## 🟡 仍待完善

### 10. 聚合函数语义检查（原 P2-10）

**现状**：[binder.cpp:222-250](goods_db/src/sql/binder/binder.cpp#L222-L250)：`BoundFunctionCall::Evaluate()` 直接返回 `Value()` (INVALID)，实际聚合计算在专门的 `AggregationExecutor` 中完成。

**影响**：如果同一个查询中既有聚合又有非聚合列，行为可能不正确（缺少 GROUP BY 验证）。

**建议**：在 Binder 中添加聚合查询的语义检查——非聚合列必须出现在 GROUP BY 中。

### 12. 关键字/保留字处理统一性（原 P2-12）

**现状**：libpg_query 的关键字保护比 goods_db 的上层处理更严格。系统内部生成的 SQL 需要反引号包裹标识符。

**建议**：所有系统内部生成的 SQL（如 API handler 中的 `SHOW TABLES FROM <db>`）统一使用反引号包裹标识符。

---

## 📋 长期优化

以下方向不阻塞基本功能，但在生产环境中需要关注：

| 方向 | 说明 |
|------|------|
| 聚合函数 + GROUP BY 语义校验 | Binder 层加强语义检查 |
| 标识符引用统一 | 内部 SQL 生成统一添加反引号 |
| 错误消息国际化 | 用户可见的错误消息结构化，支持 i18n |
| SQL_MODE 支持 | 兼容不同 SQL 方言（ANSI / PostgreSQL / MySQL） |
| Prepared Statement | Protocol_binary 已有基础，需补充 Binder/Executor 链路支持 |

---

## 修复历史

| 时间 | 版本 | 修复内容 |
|------|------|---------|
| 2026/07/13 | Phase 3 Hotfix | P0-1~3、P1-4~7、P2-8~9,11 全部修复 |
| 2026/07/10 | Phase 3 | 发现并整理全部 12 项问题 |
