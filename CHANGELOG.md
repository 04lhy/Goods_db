# Changelog

## [Phase 4 Hotfix 4] — 2026-07-16

### 新增 (Added)
- **级联删除 (CASCADE DELETE)**：删除父表行时自动级联删除所有子表关联行，支持多层递归
  - 新增 `ForeignKeyInfo` 结构体 + `FkAction` 枚举（CASCADE / RESTRICT / SET_NULL）
  - Catalog 新增 `RegisterForeignKey`、`GetChildRelations`、`GetAllForeignKeys` 方法
  - DeleteExecutor 增加 Phase 3 递归级联删除逻辑
  - 新增 `REGISTER_FK` 管理命令（格式：`REGISTER_FK parent.col child.col [CASCADE|RESTRICT]`）
  - `setup_demo_data.sh` 自动注册 7 对 FK 关系
  - Web 前端确认对话框展示级联删除警告

### 级联关系（7 对）
| 父表 | 子表 | 关联列 |
|------|------|--------|
| warehouses | inventory | id → warehouse_id |
| warehouses | shipments | id → warehouse_id |
| products | inventory | id → product_id |
| products | order_items | id → product_id |
| customers | orders | id → customer_id |
| orders | order_items | id → order_id |
| orders | shipments | id → order_id |

### 修改文件（9 个）
| 文件 | 修改 |
|------|------|
| `src/include/catalog/catalog.h` | 新增 `FkAction` + `ForeignKeyInfo` + Catalog FK 方法声明 |
| `src/catalog/catalog.cpp` | 实现 FK 注册/查询方法 |
| `src/sql/executor/executor_factory.cpp` | DeleteExecutor 增加递归级联删除 + `CascadeDelete` 辅助函数 |
| `src/include/sql/executor/abstract_executor.h` | DeleteExecutor 增加 `plan_` 成员 |
| `src/sql/executor/execution_engine.h` | 声明 `ExecuteRegisterFk` |
| `src/sql/executor/execution_engine.cpp` | 路由 + 实现 `REGISTER_FK` 管理命令 |
| `goods_db/scripts/setup_demo_data.sh` | Step 11 注册 7 对 FK 关系 |
| `goods_db_studio/web/index.html` | 确认对话框展示级联删除信息 |
| `CHANGELOG.md` | 本条目 |

### 验证
- **编译**：✅ 零错误
- **单级级联**：DELETE warehouse(1) → 5 inventory + 3 shipments ✅
- **递归级联**：DELETE customer(1001) → 3 orders + 6 order_items (depth 2) ✅
- **三级级联**：DELETE parent → 2 child + 2 grandchild (depth 3) ✅

---

## [Phase 4 Hotfix 3] — 2026-07-16

### 修复 (Fixed)
- **IS NULL / IS NOT NULL 永远返回 FALSE**：`BoundUnaryOp::Evaluate` 中 NULL 传播检查 `if (val.GetTypeId() == TypeId::INVALID) return Value()` 在 `IS_NULL`/`IS_NOT_NULL` 检查之前执行。当列值为 NULL 时函数提前返回 INVALID，IS_NULL 永远不可能返回 TRUE。修复方法：将 IS_NULL/IS_NOT_NULL 检查移到 NULL 传播之前——因为这两个操作符本身就用来检测 NULL，NULL 输入是合法的。

### 影响范围
- 所有含 NULL 列的表的 DELETE/UPDATE/SELECT WHERE IS NULL 操作
- warehouses 表（status 列未在 INSERT 中指定时为 NULL）
- inventory 表（last_updated 列可为 NULL）
- shipments 表（arrive_date 列可为 NULL）
- customers 表（address/phone 列可为 NULL）

### 修改文件（3 个）
| 文件 | 修改 |
|------|------|
| `src/sql/binder/binder.cpp` | `BoundUnaryOp::Evaluate` 中 IS_NULL/IS_NOT_NULL 检查移到 NULL 传播之前 |
| `src/sql/server/goods_db_main.cpp` | `new handlerton()` → `static` 变量，修复内存泄漏 |
| `启动指令.md` | 新增启动/编译指令速查文档 |

### 编译状态
- **服务端**：✅ 编译零错误
- **Web 前端**：✅ 无需重新编译

---

## [Phase 4 Hotfix 2] — 2026-07-15

### 新增 (Added)
- **主键自增**：INSERT 时主键留空自动分配 `max(id)+1`，前端表单显示"自动分配"提示
- **null bitmap 支持**：Tuple 序列化正确使用 null bitmap 标记 NULL 列，不再静默转 0
- **数值类型互转**：Tuple 序列化支持 BIGINT↔INT/SMALLINT/TINYINT/DECIMAL 互转，修复 Parser 产 BIGINT 但列类型为 INT 时值被丢弃的 bug
- **Unicode 转义解码**：C++ 服务端 JSON 解析器支持 `\uXXXX→UTF-8` 转换
- **NOT NULL 校验**：API `/api/columns` 返回 `nullable` + `is_primary_key`，前端表单标记必填字段

### 修复 (Fixed)
- **中文乱码**：`setup_demo_data.sh` Python `json.dumps` 加 `ensure_ascii=False`，服务端加 `\uXXXX` 解码
- **筛选无效**：`buildSelectSQL` 改为类型感知（数值 `=` 精确匹配，文本 `LIKE` 模糊匹配）
- **新增记录 id=0**：INSERT 不再过滤空值列，配合主键自增解决
- **表格样式**：字体改为含中文的 `var(--font)`，全局居中对齐
- **表格数据乱码**：`load_demo.sh` / `setup_demo_data.sh` 全部重写为真实电商数据（真实姓名、地址、商品名、SKU、快递单号、日期格式）
- **TIMESTAMP LIKE 匹配**：`binder.cpp` 中 LIKE 的 `valueToRawString` 对 TIMESTAMP 使用 `FormatTimestamp()` 返回日期字符串
- **TIMESTAMP 传输**：`connection_handler.cpp` 改为 `StoreString(FormatTimestamp(...))` 发送格式化日期
- **编辑/删除不生效**：`CoerceToCommonType` 缺少 TIMESTAMP↔VARCHAR 互转，WHERE 子句含日期列时类型不匹配永远返回 false

### 修改文件（15 个）
| 文件 | 修改 |
|------|------|
| `src/include/type/schema.h` | Column 加 `is_primary_key` |
| `src/include/type/value.h` | 加 `FormatTimestamp()` 声明 |
| `src/type/value.cpp` | 实现 `FormatTimestamp()` + `ToString()` 用格式化日期 |
| `src/storage/table/tuple.cpp` | null bitmap 读写 + 数值类型互转 |
| `src/sql/binder/binder.cpp` | `is_primary_key` 传递 + LIKE 用 `FormatTimestamp` + **CoerceToCommonType 加 TIMESTAMP↔VARCHAR/INT 互转** |
| `src/sql/server/connection_handler.cpp` | TIMESTAMP 用 `StoreString` 发送日期 |
| `src/sql/executor/executor_factory.cpp` | InsertExecutor 主键自增 |
| `goods_db_studio/src/server/api_handler.h` | ColumnInfo 加 `nullable` + `is_primary_key` |
| `goods_db_studio/src/server/api_handler.cpp` | GetColumns 提取 Null/Key 字段 + ToJson 输出 |
| `goods_db_studio/src/server/web_server.cpp` | JSON 解析加 `\uXXXX` 解码 |
| `goods_db_studio/web/index.html` | 类型感知筛选 + 字体居中 + 表单校验 + 自增提示 |
| `goods_db/scripts/setup_demo_data.sh` | 真实电商数据 + `ensure_ascii=False` |
| `goods_db/scripts/load_demo.sh` | 完整重写为真实电商数据（~350 条） |
| `goods_db/docs/web_demo_guide.md` | 同步数据说明和 SQL 示例 |

### 编译状态
- **服务端**：✅ 编译零错误
- **Web 前端**：✅ 编译零错误
- **全量测试**：✅ 编译通过

---

## [Phase 3 Hotfix] — 2026-07-13

### 修复 (Fixed) — SQL 兼容性问题（参考 docs/sql_issues.md）

#### P0 阻塞性问题
- **`db.table` 格式表名解析**：Binder 各 Bind 方法新增 `ResolveTableName()` 辅助函数，自动拆分 `db.table` 格式，解决 web 前端所有 CRUD 操作报 "Table not found" 的问题
- **SHOW COLUMNS 反引号处理**：修复 `FROM` 子句偏移量，新增 `TrimIdentifier()` 去除反引号包裹，正确处理 `` SHOW COLUMNS FROM `default`.`students` `` 等格式
- **SHOW TABLES FROM 子句**：解析 `FROM <db>` 并过滤数据库，支持 SHOW TABLES FROM goods_db

#### P1 功能性缺陷
- **CREATE DATABASE / DROP DATABASE**：在 ExecutionEngine 预检查中添加两个命令的处理，支持 IF NOT EXISTS / IF EXISTS
- **多数据库架构**：Catalog 新增 `DatabaseInfo` 层（Database → Table → Index），支持多数据库命名空间，默认数据库名改为 `goods_db`
- **affected_rows 传递**：ExecutorContext 新增 affected_rows 字段，DML executor（Insert/Update/Delete）自动计数，ConnectionHandler 正确返回受影响行数

#### P2 兼容性改进
- **多语句支持**：HandleQuery 中循环调用 SplitSql → ExecuteSQL，支持分号分隔的多条 SQL
- **默认数据库名**：从 SQL 保留字 `default` 改为 `goods_db`，避免反引号包裹问题

## [Phase 3] — 2026-07-10

### 新增 (Added) — 系统服务层

#### 网络协议层（设计六）
- **Protocol 抽象基类**：定义统一的结果集序列化接口（StartResultMetadata / SendColumnDefinition / Store* / SendOk / SendError / SendEOF）
- **Protocol_text**：文本协议实现（人类可读，支持 telnet/nc 调试）。格式：`Columns: N` + `ColDef: name type len` + `Row: val1\tval2...` + `OK/ERR/EOF`
- **Protocol_binary**：二进制协议实现（紧凑编码，LE int + 长度前缀字符串 + 类型标签）
- **Connection**：TCP socket 封装，ReadPacket/WritePacket（4字节头 + payload），生命周期状态机（INIT→AUTH→READY→QUERYING→SENDING）
- **SocketServer**：TCP 监听器，CreateSocket/Listen/Accept/SetNonBlocking

#### 线程与服务器管理（设计七）
- **ThreadPool**：固定大小工作线程池（默认 = CPU 核心数），Submit(task)→future，支持 Shutdown/ShutdownNow
- **ConnectionHandler**：连接完整生命周期管理（握手→认证→命令分发→SQL执行→结果返回）
- **goods_db_server**：服务器主入口，完整启动流程（解析参数→初始化Logger→注册引擎→初始化子系统→启动线程池→监听accept循环→优雅关闭）

#### 日志子系统（设计五）
- **ErrorLog**：服务器错误日志，格式 `[时间戳] [级别] [线程ID] [文件:行] 消息`，级别（DEBUG/INFO/WARN/ERROR/FATAL），双输出（文件+stderr）
- **QueryLog**：SQL 查询日志，格式 `[时间戳] [user@host] [database] [耗时ms] [影响行数] sql`，自动轮转（100MB切割）
- **BinaryLog**：WAL binlog，4种事件类型（QUERY/ROW_INSERT/ROW_UPDATE/ROW_DELETE/XID），事件包含时间戳+tid+payload+CRC32，支持文件轮转和索引文件
- **LogManager**：统一管理三层日志的初始化和关闭
- **goods_db_binlog**：命令行 binlog 解析工具，支持 --start-position / --stop-position / --verbose

#### 安全管理（设计九）
- **AuthManager**：用户认证与 ACL 权限控制
- **SHA-256**：自包含实现（无外部依赖），密码存储为 SHA-256(password + 16字节随机salt)
- **ACL 三张系统表**：goods_db.user（用户认证）/ goods_db.db（库级权限）/ goods_db.tables_priv（表级权限），位掩码权限模型（SELECT|INSERT|UPDATE|DELETE|CREATE|DROP|INDEX|ALTER|GRANT）
- **认证流程**：handshake→password_hash 比对→加载权限缓存→返回OK/ERR
- **主机封锁**：同一IP连续失败10次→封锁5分钟
- **SQL 命令**：CREATE USER / DROP USER / ALTER USER / SET PASSWORD / GRANT / REVOKE / FLUSH PRIVILEGES

#### 桌面客户端（设计十）
- **工程骨架**：CMakeLists.txt（Qt6 Widgets+Sql+Network）+ main.cpp + QRC资源文件
- **MainWindow**：菜单栏（6个菜单）+ 工具栏（6个按钮）+ 4 DockWidget（左对象树/右表详情/底结果/中编辑器）+ 状态栏（连接状态+数据库+事务）
- **GoodsDbClient**：QTcpSocket 封装，Connect/Authenticate/Execute/Ping，协议适配（解析 Columns/ColDef/Row/OK/ERR/EOF）
- **ConnectionDialog**：连接表单（主机/端口/用户/密码/数据库/连接名）+ 测试连接 + QSettings 持久化
- **ConnectionPool**：多连接管理（添加/删除/切换），信号通知连接状态变化
- **SqlHighlighter**：6类语法高亮（~80关键字蓝色/15类型青色/字符串橙红/数字浅绿/注释灰色/20+函数紫色），多行注释支持
- **SqlEditorWidget**：QTabWidget 多标签 + SqlEditor（QPlainTextEdit子类化暴露protected方法）+ LineNumberArea 行号区 + Ctrl+T/W快捷键
- **QueryExecutor**：异步执行（QTimer），分号分割多语句，非阻塞UI
- **QueryHistory**：本地 SQLite 持久化查询历史（最近1000条，支持搜索）
- **ResultTableModel**：QAbstractTableModel，NULL特殊渲染（灰色斜体），数值右对齐，tooltip显示长文本
- **ResultTableView**：QTableView + 分页（>1000行自动分页）+ 右键导出菜单
- **ResultPagination**：上一页/下一页/跳转控件 + 总行数显示
- **ExportDialog**：导出 CSV / JSON / SQL INSERT 三种格式
- **ObjectTreeModel**：树形模型（Server→Database→Tables→Table→Columns→Column），支持动态添加节点
- **ObjectTreeWidget**：QTreeView + 双击表名自动SELECT + 右键菜单（打开/查看结构/复制名称/生成CRUD模板）
- **TableInfoPanel**：显示选定表的列定义（列名+类型）
- **主题**：深色主题（VSCode Dark+风格，默认）+ 浅色主题，Ctrl+T 切换

### 新增文件（60+ 个）

```
服务端 (35 个):
  include/sql/protocol/{protocol,protocol_text,protocol_binary}.h
  include/sql/network/{connection,net_serv}.h
  include/sql/server/{thread_pool,connection_handler}.h
  include/sql/log/{error_log,query_log,binary_log,log_manager}.h
  include/sql/security/auth_manager.h
  sql/protocol/{protocol,protocol_text,protocol_binary}.cpp
  sql/network/{connection,net_serv}.cpp
  sql/server/{thread_pool,connection_handler,goods_db_main}.cpp
  sql/log/{error_log,query_log,binary_log,log_manager}.cpp
  sql/security/auth_manager.cpp
  sql/binlog/goods_db_binlog.cpp

桌面客户端 (38 个):
  goods_db_studio/CMakeLists.txt
  resources/{resources.qrc,themes/dark.qss,themes/light.qss}
  src/{main.cpp,main_window.h,main_window.cpp}
  src/network/{goods_db_client.h/.cpp,protocol_text_adapter.h/.cpp}
  src/connection/{connection_dialog.h/.cpp,connection_pool.h/.cpp}
  src/sql_editor/{sql_highlighter,sql_editor_widget,query_executor,query_history}.{h,cpp}
  src/result_view/{result_table_model,result_table_view,result_pagination,export_dialog}.{h,cpp}
  src/object_tree/{object_tree_model,object_tree_widget,table_info_panel}.{h,cpp}
```

### 文档更新
- 新增 [操作手册](goods_db/docs/operations_guide.md)
- 更新 README.md（模块状态、项目结构、快速开始）
- 更新 CHANGELOG.md

### 编译状态
- **服务端**：✅ 编译零错误（clang-15, C++17）
- **桌面客户端**：✅ 编译零错误（Qt 6.2.4, C++17）
- **测试**：✅ 回归测试全部通过（parser_test / binder_test / integration_test PASS）

---

## [Phase 2 Hotfix] — 2026-07-08

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

## [Phase 2] — 2026-07-07

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

## [Phase 1] — 2026-07-06

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
