# goods_db — 新零售场景数据库系统

> 数据库系统设计课程项目 · 湖南大学

## 项目简介

goods_db 是一个面向新零售场景的完整数据库系统，参考 MySQL 插件式存储引擎架构设计。支持完整的 CRUD 操作、B+Tree 和 Extendible Hash 双索引、LRU-K/Clock 缓存替换策略、slotted page 磁盘管理，以及基于 Qt 6 的桌面客户端（goods_db_studio）。

## 团队成员

| 成员 | 分工 |
|------|------|
| **刘晗阳**（组长） | 索引层（B+Tree / ExtendibleHashIndex）+ **桌面客户端（goods_db_studio）** + **服务端全模块** |
| 肖清峰 | 引擎接口层 / 集成联调（handlerton, handler, goods_handler） |
| 李锦华 | 磁盘与缓冲层（DiskManager, BufferPoolManager）+ 桌面客户端辅助 |
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
├── goods_db_studio/             # 桌面客户端（Qt 6 Widgets）
│   ├── src/
│   │   ├── main_window.h/cpp    # 主窗口布局
│   │   ├── network/             # 网络客户端 (GoodsDbClient)
│   │   ├── connection/          # 连接管理 (ConnectionDialog/ConnectionPool)
│   │   ├── sql_editor/          # SQL 编辑器（语法高亮 + 执行）
│   │   ├── result_view/         # 结果表格浏览 + 分页 + 导出
│   │   ├── object_tree/         # 数据库/表/索引对象树
│   │   └── resources/           # 图标/主题
│   └── CMakeLists.txt
├── bustub/                      # CMU BusTub 参考实现（MIT License）
├── 计划/                         # 团队和个人周计划（第1-4周）
├── 模板/                         # 报告/计划模板
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
| **桌面客户端** (Qt 6: MainWindow + GoodsDbClient + SQL编辑器 + 结果浏览器 + 对象浏览器 + 主题) | ✅ 编译通过 |

## 快速开始

### 环境要求

- **编译器**: clang-15+
- **CMake**: 3.22+
- **Qt**: 6.2+（桌面客户端需要 Widgets, Sql, Network 组件）
- **操作系统**: Linux (Ubuntu 22.04/24.04 推荐)

### 编译

```bash
# ===== 编译服务端 =====
cd goods_db/build
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_C_COMPILER=clang-15 \
         -DCMAKE_CXX_COMPILER=clang++-15
make -j$(nproc)

# ===== 编译桌面客户端 =====
cd goods_db_studio/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### 运行

```bash
# 启动数据库服务器
./goods_db/build/src/sql/goods_db_server --port 3307

# 启动桌面客户端（另一个终端）
./goods_db_studio/build/goods_db_studio

# 查看 binlog
./goods_db/build/src/sql/goods_db_binlog --verbose goods_db_binlog.000001
```

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

## 文档

- [操作手册](goods_db/docs/operations_guide.md) — 第三阶段完整操作指南（协议/线程/日志/安全/桌面客户端）
- [部署指南](goods_db/docs/deployment_guide.md) — 生产环境部署（Docker / systemd / 配置调优 / 安全加固）
- [SQL 问题跟踪](goods_db/docs/sql_issues.md) — 已知 SQL 兼容性问题及修复状态
- [桌面客户端设计文档](goods_db/docs/design/goods_db_studio_design.md) — goods_db_studio 架构设计
- [CHANGELOG](CHANGELOG.md) — 变更日志

## 参考资料

- [CMU 15-445/645 Database Systems](https://15445.courses.cs.cmu.edu/)
- [BusTub](https://github.com/cmu-db/bustub) (MIT License)

## License

本项目 goods_db 为课程项目，bustub 部分遵循 MIT License（Copyright (c) 2019 CMU Database Group）。
