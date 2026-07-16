# goods_db — 新零售场景数据库系统 v1.2.2

> 数据库系统设计课程项目 · 湖南大学

## 项目简介

goods_db 是一个面向新零售场景的完整数据库系统，参考 MySQL 插件式存储引擎架构设计。支持完整的 CRUD 操作、**级联删除（外键递归级联）**、B+Tree 和 Extendible Hash 双索引、LRU-K/Clock 缓存替换策略、slotted page 磁盘管理、SQL 查询引擎（Parser → Binder → Planner → Optimizer → Executor）、网络协议层、线程池、日志子系统（Error/Query/Binary）、安全管理（用户认证 + SHA-256 + ACL + GRANT/REVOKE + 权限隔离），以及基于 Qt 6 的桌面客户端（goods_db_studio）和 Web 前端（单文件零依赖 SPA）。

## 团队成员

| 成员 | 分工 |
|------|------|
| **刘晗阳**（组长） | 索引层（B+Tree / ExtendibleHashIndex）+ **桌面客户端 + Web 前端** + **服务端全模块** + **级联删除** + **全系统联调** |
| 肖清峰 | 引擎接口层 / 集成联调（handlerton, handler, goods_handler） |
| 李锦华 | 磁盘与缓冲层（DiskManager, BufferPoolManager）+ 桌面客户端辅助（SVG 图标/QSS 主题） |
| 杨玉山 | 表与元组层（TablePage, Tuple, TableHeap, TableIterator） |
| 唐瑞涛 | 项目骨架 / 工具链 / 通用组件（CMake, Catalog, RWLatch, Config, Logger） |
| 王宇航 | 缓冲池替换策略 + 页保护（LRU-K, ClockReplacer, PageGuard） |

## 项目结构

```
Goods_db/
├── goods_db/                    # 主项目（数据库服务器）
│   ├── src/
│   │   ├── include/             # 头文件
│   │   │   ├── common/          # RID, RWLatch, Config, Logger
│   │   │   ├── type/            # Value, Schema
│   │   │   ├── storage/         # DiskManager, Page, TablePage, Tuple, Index
│   │   │   ├── buffer/          # BufferPoolManager, LRU-K/ClockReplacer
│   │   │   ├── catalog/         # Catalog
│   │   │   └── sql/             # handler, handlerton, goods_handler
│   │   │       ├── parser/      # SQL 解析 (libpg_query → AST)
│   │   │       ├── binder/      # 语义绑定 (AST → BoundStatement)
│   │   │       ├── planner/     # 计划生成 (BoundStatement → PlanNode)
│   │   │       ├── optimizer/   # 优化规则 (谓词下推/列裁剪/索引选择等)
│   │   │       ├── executor/    # Volcano 模型执行器 (13 种算子)
│   │   │       ├── protocol/    # 网络协议 (Protocol/Protocol_text/Protocol_binary)
│   │   │       ├── network/     # 网络层 (Connection/SocketServer)
│   │   │       ├── server/      # 服务器 (ThreadPool/ConnectionHandler/main)
│   │   │       ├── log/         # 日志 (ErrorLog/QueryLog/BinaryLog/LogManager)
│   │   │       └── security/    # 安全 (AuthManager/SHA-256/ACL)
│   │   └── ...                  # 各模块实现文件
│   ├── test/                    # 单元测试 & 集成测试
│   ├── third_party/             # 第三方库（符号链接 → bustub/third_party）
│   └── CMakeLists.txt
├── goods_db_studio/             # 桌面客户端 + Web 前端（Qt 6 Widgets + cpp-httplib）
│   ├── src/
│   │   ├── main_window.h/cpp    # 主窗口布局（QTabWidget 中央容器）
│   │   ├── network/             # 网络客户端 (GoodsDbClient)
│   │   ├── connection/          # 连接管理 (ConnectionDialog/ConnectionPool)
│   │   ├── sql_editor/          # SQL 编辑器（语法高亮 + 多标签执行）
│   │   ├── result_view/         # 结果表格浏览 + 分页 + 多格式导出
│   │   ├── object_tree/         # 数据库/表/列对象树
│   │   ├── ui/                  # 管理面板 (AdminPanel/LogViewer/UserManager/BackupWizard/WelcomeWidget)
│   │   ├── server/              # Web 服务器 (main_web/web_server/api_handler)
│   │   └── resources/           # SVG 图标 / QSS 主题
│   ├── web/                     # Web 前端 (index.html)
│   └── CMakeLists.txt
├── bustub/                      # CMU BusTub 参考实现（MIT License）
├── 计划/                         # 团队和个人周计划（第1-4周）
├── goods_db/scripts/            # 辅助脚本（demo 数据加载）
├── goods_db/docs/               # 项目文档（含设计文档和演示指南）
├── goods_db/tools/              # 管理工具（goods_db_admin 命令行客户端）
├── Dockerfile                   # 开发环境镜像
├── docker-compose.yml           # 容器编排
├── README.md                    # 本文件
└── CHANGELOG.md                 # 变更日志
```

## 已实现模块

### 第一阶段：核心存储层 ✅

| 模块 | 测试状态 |
|------|----------|
| 类型系统 (Value / Schema / Tuple) | ✅ 7/7 pass |
| Page / TablePage | ✅ 7/7 pass |
| BufferPoolManager + LRU-K / Clock | ✅ 6/6 pass |
| TableHeap + TableIterator | ✅ 2/2 pass |
| B+Tree Index | ✅ 6/6 pass |
| Extendible Hash Index | ✅ 6/6 pass |
| Catalog + handlerton + handler | ✅ pass |
| 集成测试 (FullEndToEnd) | ✅ 9/9 pass |

### 第二阶段：查询引擎层 ✅

| 模块 | 测试状态 |
|------|----------|
| SQL Parser (libpg_query) | ✅ 48/48 pass |
| Binder (语义绑定) | ✅ 18/18 pass |
| Planner (PlanNode 树) | ✅ 9/9 pass |
| Executor (13 种 Volcano 算子) | ✅ 13/13 pass |
| Optimizer (6 条优化规则) | ✅ pass |
| LockManager + TransactionManager | ✅ pass |
| 集成测试 (EndToEnd) | ✅ 9/9 pass |
| 全量单元测试 | ✅ 125/125 pass |

### 第三阶段：系统服务层 ✅

| 模块 | 状态 |
|------|------|
| **网络协议层** (Protocol + Protocol_text + Protocol_binary + Connection + SocketServer) | ✅ 编译通过 |
| **线程管理** (ThreadPool + ConnectionHandler + goods_db_main) | ✅ 编译通过 |
| **日志子系统** (ErrorLog + QueryLog + BinaryLog + LogManager + goods_db_binlog) | ✅ 编译通过 |
| **安全管理** (AuthManager + SHA-256 + ACL + GRANT/REVOKE + 主机封锁) | ✅ 编译通过 |
| **12 个管理命令** (SHUTDOWN/SHOW STATUS/SHOW PROCESSLIST/SHOW USERS/RELOAD/FLUSH HOSTS/FLUSH TABLES/KILL/SHOW GRANTS FOR/SHOW ERRORLOG/SHOW QUERYLOG/SHOW BINARYLOG) | ✅ 服务器端实现 |

### 第四阶段：桌面客户端 + Web 前端 ✅

| 模块 | 状态 |
|------|------|
| **主窗口布局** (MainWindow + QTabWidget + Dock Widgets + 状态栏) | ✅ 编译零警告 |
| **工具栏 + 菜单栏** (8 个工具栏按钮 + 35 个 SVG 图标) | ✅ 集成完成 |
| **SQL 编辑器** (语法高亮 + 多标签 + 查询历史) | ✅ 完成 |
| **对象浏览器** (树形展示 + 数据库/表/列图标 + 右键菜单) | ✅ 完成 |
| **结果浏览器** (分页 + 排序 + CSV/JSON/SQL INSERT 导出) | ✅ 完成 |
| **AdminPanel** (仪表盘 + 进程列表 + 6 个运维操作 + 自动刷新) | ✅ 完成 |
| **BackupWizard** (4 步备份恢复向导 + DDL/DATA 导出 + 进度条) | ✅ 完成 |
| **LogViewer** (3 标签日志 + 过滤/搜索 + 自动刷新) | ✅ 完成 |
| **UserManager** (用户 CRUD + 8 权限 CHECKBOX + GRANT/REVOKE) | ✅ 完成 |
| **WelcomeWidget** (欢迎首页 + 快捷操作 + 最近连接) | ✅ 完成 |
| **Splash Screen** (启动画面 + 程序化绘制 Logo) | ✅ 完成 |
| **QSS 主题** (深色/浅色双主题 + Ctrl+T 切换) | ✅ 完成 |
| **Web 前端** (SPA + REST API + 增删改查 + 筛选排序分页 + 多标签 SQL + 导出 + 级联删除警告 + 权限隔离) | ✅ 完成 |
| **级联删除** (FK 元数据 + 递归级联 + REGISTER_FK 命令 + 前端确认警告) | ✅ 新增（v1.2.2） |
| **权限隔离** (root/admin/guest 三级用户 + Web 用户管理面板) | ✅ 新增（v1.2.2） |

## 快速开始

### 环境要求

- **编译器**: GCC 11+ 或 Clang 15+
- **CMake**: 3.16+
- **Qt**: 6.2+（桌面客户端需要 Widgets, Sql, Network 组件）
- **操作系统**: Linux (Ubuntu 22.04 推荐)

### 编译

```bash
# ===== 编译服务端 =====
cd goods_db/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(nproc)

# ===== 编译桌面客户端 + Web 服务器 =====
cd goods_db_studio/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(nproc)
```

### 运行

```bash
# 启动数据库服务器
./goods_db/build/src/sql/goods_db_server --port 3307

# 启动桌面客户端（另一个终端）
./goods_db_studio/build/goods_db_studio

# 启动 Web 前端服务器（另一个终端）
./goods_db_studio/build/goods_db_web
# 浏览器打开 http://localhost:8080
```

### 🎯 可视化演示（推荐）

```bash
# 终端1：数据库服务器
cd goods_db/build && rm -rf ./data && ./src/sql/goods_db_server --port 3307

# 终端2：Web 服务器
cd goods_db_studio/build && ./goods_db_web

# 终端3：一键加载电商演示数据（7张表，60+条数据）
cd 项目根目录
./goods_db/scripts/setup_demo_data.sh

# 浏览器打开 http://localhost:8080 → 全程点击操作
```

> **演示亮点**：对象树浏览 → 表单增删改（主键自增）→ 类型感知筛选 → 排序 → **级联删除** → 多表 JOIN → 权限隔离（guest 只读）→ CSV/JSON/SQL 导出，全程可视化。详见 [Web 电商演示指南](goods_db/docs/web_demo_guide.md)

### Docker 环境（可选）

```bash
docker compose build
docker compose run --rm bustub
```

### 运行测试

```bash
cd goods_db/build
ctest --output-on-failure
```

## 最近修复（v1.2.2）

| 日期 | 问题 | 根因 | 修复 |
|------|------|------|------|
| 2026-07-16 | 缺少级联删除，删父表导致孤儿数据 | 无外键/级联基础设施 | 新增 FK 元数据 + 递归级联删除 + REGISTER_FK 命令 + 前端级联警告 |
| 2026-07-16 | IS NULL / IS NOT NULL 永远返回 FALSE | `BoundUnaryOp::Evaluate` 中 NULL 传播检查在 IS_NULL 之前，列值为 NULL 时提前返回 INVALID | IS_NULL/IS_NOT_NULL 移到 NULL 传播之前，NULL 输入对这两个操作符是合法的 |
| 2026-07-16 | 服务端内存泄漏 | `goods_db_main.cpp` 中 `new handlerton()` 未释放 | 改为 `static` 局部变量，生命周期跟随进程 |
| 2026-07-15 | 中文数据乱码 | Python `json.dumps` 默认 `ensure_ascii=True` 把中文转成 `\uXXXX`，C++ 服务端未解码直接存库 | shell 脚本加 `ensure_ascii=False` + 服务端 `\uXXXX→UTF-8` 解码 |
| 2026-07-15 | 表格中文显示差 | table 字体用 `var(--mono)` 无中文字符，回退字体汉字宽度为英文 2 倍导致参差不齐 | 改为 `var(--font)`（含 Noto Sans SC），全局居中对齐 |
| 2026-07-15 | 筛选功能无效 | 所有列类型都用 `LIKE '%value%'`，数值列子串匹配结果完全不对 | 类型感知筛选：数值用 `=` 精确匹配，文本用 `LIKE` 模糊匹配 |
| 2026-07-15 | 新增记录 id=0 | 表单空值被过滤导致 INSERT 不含 id 列，tuple 序列化 TypeId::INVALID 默认写 0 | 主键自增 `max(id)+1` + null bitmap 正确存储 + 表单校验 |
| 2026-07-15 | 所有 INT 值显示为 0 | Parser 产 BIGINT，列类型 INTEGER，序列化类型不匹配导致值被丢弃 | 增加数值类型互转（BIGINT↔INT/SMALLINT/TINYINT/DECIMAL） |
| 2026-07-15 | 编辑/删除不生效（含 TIMESTAMP 的表） | `CoerceToCommonType` 缺少 TIMESTAMP↔VARCHAR 互转，WHERE 含日期列时类型不匹配返回 false | 增加 TIMESTAMP↔VARCHAR（ParseTimestamp）和 TIMESTAMP↔INT 互转 |

## 文档

| 文档 | 说明 |
|------|------|
| [用户手册](goods_db/docs/goods_db_studio_manual.md) | goods_db_studio 桌面客户端完整操作指南 |
| [🎯 Web 电商演示指南](goods_db/docs/web_demo_guide.md) | **演示用**：Web 前端操作电商数据库全流程（建表→插入→查询→更新→删除→导出） |
| [操作手册](goods_db/docs/operations_guide.md) | 全阶段操作指南（协议/线程/日志/安全/级联删除/权限隔离/桌面客户端） |
| [部署指南](goods_db/docs/deployment_guide.md) | 生产环境部署（Docker / systemd / 配置调优 / 安全加固） |
| [SQL 问题跟踪](goods_db/docs/sql_issues.md) | 已知 SQL 兼容性问题及修复状态 |
| [启动指令](启动指令.md) | 编译/启动/加载数据一站式命令速查 |
| [桌面客户端设计](goods_db/docs/design/goods_db_studio_design.md) | goods_db_studio 架构设计 |
| [集成测试](goods_db/test/sql/integration_test.cpp) | 17 个端到端集成测试用例 |
| [性能基准](goods_db/test/benchmark/) | 批量插入/查询/索引性能基准测试 |

## 参考资料

- [CMU 15-445/645 Database Systems](https://15445.courses.cs.cmu.edu/)
- [BusTub](https://github.com/cmu-db/bustub) (MIT License)

## License

本项目 goods_db 为课程项目，bustub 部分遵循 MIT License（Copyright (c) 2019 CMU Database Group）。
