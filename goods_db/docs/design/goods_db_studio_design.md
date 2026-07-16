# goods_db_studio 桌面客户端设计文档

> goods_db 数据库系统的官方桌面客户端，基于 Qt 6 Widgets 构建。
>
> **负责人**：刘晗阳（主导）、李锦华（辅助）  
> **最后更新**：2026-07-16 | **版本**：v1.2.2

---

## 1. 概述

### 1.1 设计目标

goods_db_studio 是 goods_db 数据库系统的图形化管理工具，定位类似于 MySQL Workbench / DBeaver / Navicat，提供统一的 GUI 入口完成数据库连接、SQL 编写与执行、数据浏览、服务器管理、备份恢复、日志查看、用户管理等全部操作。

### 1.2 架构定位

```
用户
 │
 ▼
┌──────────────────────────────────────────────────┐
│           goods_db_studio（Qt 6 Widgets）         │
│                                                  │
│  ┌──────────┐ ┌─────────────┐ ┌───────────────┐ │
│  │ 连接管理  │ │  SQL 编辑器  │ │  结果浏览器    │ │
│  │(Connection│ │(SqlEditor)  │ │(ResultView)   │ │
│  │ Dialog)   │ │             │ │               │ │
│  └──────────┘ └─────────────┘ └───────────────┘ │
│  ┌──────────┐ ┌─────────────┐ ┌───────────────┐ │
│  │ 对象浏览器│ │  管理面板    │ │ 备份/日志/安全 │ │
│  │(Object   │ │(AdminPanel) │ │(ToolsPanel)   │ │
│  │ Tree)    │ │             │ │               │ │
│  └──────────┘ └─────────────┘ └───────────────┘ │
├──────────────────────────────────────────────────┤
│           goods_db_client（网络客户端库）          │
│     封装 TCP/IP Protocol 通信，提供 C++ API       │
├══════════════════════════════════════════════════╡
│                    TCP/IP 网络                     │
├──────────────────────────────────────────────────┤
│   goods_db 服务器 (Protocol_text / Protocol_binary)│
├──────────────────────────────────────────────────┤
│              存储引擎 + 查询引擎                    │
└──────────────────────────────────────────────────┘
```

桌面客户端通过网络协议与服务器通信，不直接访问存储层，保持架构清晰。

### 1.3 技术选型

| 组件 | 选型 | 说明 |
|------|------|------|
| GUI 框架 | Qt 6.5+ (Widgets) | 成熟稳定，原生 C++，与项目语言一致 |
| 网络通信 | goods_db_client（自行封装） | 封装 TCP/IP，提供同步/异步查询接口 |
| 语法高亮 | QSyntaxHighlighter | Qt 内置，子类化实现 SQL 语法规则 |
| 配置持久化 | QSettings | 窗口布局、连接历史、主题设置 |
| 本地缓存 | SQLite (Qt SQL module) | 查询历史持久化、本地连接信息缓存 |
| 构建系统 | CMake + find_package(Qt6) | 与项目构建体系统一 |

---

## 2. 模块设计

### 2.1 主窗口布局 (MainWindow)

```
┌───────────────────────────────────────────────────────┐
│  Menu Bar: [文件] [编辑] [查询] [工具] [设置] [帮助]    │
├───────────────────────────────────────────────────────┤
│  Tool Bar: [新建连接] [打开文件] [执行(F5)] [停止] ... │
├─────────────┬─────────────────────┬───────────────────┤
│  对象浏览器  │                     │   对象详情面板     │
│  (左侧 Dock) │   SQL 编辑器区域     │   (右侧 Dock)     │
│             │   (中央，多标签)      │                  │
│  ┌─ local   │                     │  表名: students   │
│  │ ├─ db1   │  ┌─────────────────┐│  列数: 4          │
│  │ │ ├─ t1  │  │ SELECT * FROM   ││  行数: 10000      │
│  │ │ └─ t2  │  │   students      ││  索引: idx_id     │
│  │ └─ db2   │  │ WHERE id > 100; ││                  │
│  └─ remote  │  └─────────────────┘│                  │
│             │                     │                  │
│             ├─────────────────────┤                  │
│             │   结果 / 消息面板    │                  │
│             │  (底部 Dock)        │                  │
│             │  ┌─────────────────┐│                  │
│             │  │ id  │ name │age ││                  │
│             │  │ 101 │ Alice│ 20 ││                  │
│             │  │ 102 │ Bob  │ 22 ││                  │
│             │  └─────────────────┘│                  │
├─────────────┴─────────────────────┴───────────────────┤
│  Status Bar: [已连接: localhost:3307] [数据库: test] ...│
└───────────────────────────────────────────────────────┘
```

### 2.2 连接管理 (Connection)

**类层次**：

```
ConnectionDialog      → 连接参数输入对话框
ConnectionPool        → 管理多个服务器连接
SessionInfo           → 单个会话状态（当前库、事务状态）
```

**连接参数**：

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| host | QString | localhost | 服务器地址 |
| port | int | 3307 | 服务器端口 |
| username | QString | root | 用户名 |
| password | QString | (加密存储) | 密码 |
| default_db | QString | (空) | 默认数据库 |
| connection_name | QString | (自动生成) | 连接别名（用于显示） |

**功能**：
- 连接测试（Ping）：点击"测试连接"按钮，验证可达性和认证
- 心跳保活：定时发送 Ping 命令维持长连接
- 连接历史：最近连接列表，支持一键重连
- 安全存储：密码通过 QSettings + 简单加密存储（不存明文）

### 2.3 SQL 编辑器 (SqlEditor)

**核心类**：

```
SqlEditorWidget       → 多标签编辑器容器（QTabWidget）
SqlHighlighter        → QSyntaxHighlighter 子类，SQL 语法高亮
QueryExecutor         → 执行引擎，发送 SQL → 接收结果集
QueryHistory          → 查询历史管理（基于 SQLite）
```

**语法高亮规则**：

| 类别 | 正则/关键字 | 样式 |
|------|------------|------|
| 关键字 | SELECT, FROM, WHERE, INSERT, UPDATE, DELETE, CREATE, DROP, ALTER, JOIN, ON, GROUP, ORDER, BY, HAVING, LIMIT, UNION, AS, IN, NOT, NULL, IS, AND, OR, BETWEEN, LIKE, SET, VALUES, INTO, TABLE, INDEX, PRIMARY, KEY, FOREIGN, REFERENCES, DISTINCT, COUNT, SUM, AVG, MAX, MIN, EXISTS, CASE, WHEN, THEN, ELSE, END, LEFT, RIGHT, INNER, OUTER, CROSS, FULL | 蓝色加粗 |
| 数据类型 | INT, INTEGER, BIGINT, SMALLINT, TINYINT, BOOLEAN, DECIMAL, DOUBLE, FLOAT, CHAR, VARCHAR, TEXT, TIMESTAMP, DATE | 青色 |
| 字符串 | `'...'` | 红色 |
| 数字 | `\b\d+(\.\d+)?\b` | 橙色 |
| 注释 | `-- ...` / `/* ... */` | 灰色斜体 |
| 函数 | COUNT, SUM, AVG, MAX, MIN, COALESCE, IFNULL, NOW, DATE_FORMAT | 紫色 |

**功能**：
- 多标签编辑（Ctrl+T 新建，Ctrl+W 关闭）
- 执行快捷键（F5 执行全部，Ctrl+Enter 执行当前语句/选中区域）
- 代码补全（QCompleter，候选词 = SQL 关键字 + 当前数据库表名 + 列名）
- 括号匹配高亮
- 行号显示
- 查询历史持久化到本地 SQLite（最近 1000 条，带时间戳和执行耗时）
- 已修改未保存标记（tab 标题加 `*`）

### 2.4 结果浏览器 (ResultView)

**核心类**：

```
ResultTableModel      → QAbstractTableModel 子类，绑定查询结果集
ResultTableView       → QTableView + 自定义代理渲染
ResultPagination      → 分页控件（大结果集分批加载）
ExportDialog          → 导出对话框（CSV / JSON / SQL INSERT）
```

**功能**：
- 动态列适配（根据结果集元数据自动创建列）
- 数据类型渲染：NULL → 灰色斜体 `(NULL)`，数值 → 右对齐，字符串 → 左对齐
- 大结果集分页（每页 1000 行，上一页/下一页/跳转到第 N 页）
- 结果集数量显示（"共 15234 行，当前第 1-1000 行"）
- 列排序（点击列头排序，本地排序）
- 右键菜单：复制单元格 / 复制行 / 导出
- 多结果集支持（一条 SQL 返回多个结果集时，用 tab 切换）

### 2.5 对象浏览器 (ObjectTree)

**核心类**：

```
ObjectTreeWidget      → QTreeView + 自定义 QAbstractItemModel
TableInfoPanel        → 右侧表详情面板
```

**树形结构**：

```
🌐 服务器 (localhost:3307)
├── 📁 database_1
│   ├── 📋 tables
│   │   ├── 📄 students       [列: 4 | 行: 10,000]
│   │   ├── 📄 warehouses     [列: 5 | 行: 50]
│   │   └── 📄 inventory      [列: 6 | 行: 200,000]
│   ├── 📋 views
│   │   └── 👁 v_active_orders
│   └── 📋 indexes
│       ├── 🔑 idx_students_id (BTREE, students.id)
│       └── 🔑 idx_inventory_wh (HASH, inventory.warehouse_id)
├── 📁 database_2
│   └── ...
```

**交互**：
- 双击表名 → 自动生成 `SELECT * FROM table LIMIT 100` 并执行
- 右键菜单：查看结构 / 浏览数据（前 N 行）/ 复制表名 / 生成 SELECT/INSERT/UPDATE/DELETE 模板
- 拖拽表名/列名到编辑器 → 自动填入
- 刷新按钮 → 重新向服务器请求 Catalog 元数据

### 2.6 管理面板 (AdminPanel)

> 对应设计三，替代 goods_db_admin 命令行工具。

**功能**：

| 功能 | 对应 CLI 命令 | UI 实现 |
|------|-------------|---------|
| 服务器状态仪表盘 | `status --extended` | QPS / 连接数 / 缓冲池命中率 / 脏页数 → 实时数值 + 简易趋势图 |
| 版本信息 | `version` | 服务器版本、启动时间、运行时长 |
| 进程列表 | `processlist` | QTableView 展示活跃连接（ID、用户、主机、数据库、命令、时间、状态），支持 Kill 按钮 |
| Kill 线程 | `kill <id>` | 选中行 → 右键 Kill / 按钮 Kill |
| Flush 操作 | `flush [hosts\|logs\|tables]` | 三个按钮分别对应 |
| 重载授权表 | `reload` | 按钮触发 |
| 修改管理员口令 | `password <new>` | 对话框输入新旧密码 |
| 检查服务器状态 | `ping` | 心跳自动检测，异常时状态栏变红 |
| 关闭服务器 | `shutdown` | 确认对话框 → 发送 shutdown 命令 |

### 2.7 工具面板 (ToolsPanel)

> 整合设计四（备份）、设计五（日志）、设计九（安全）的 GUI。

#### 2.7.1 备份恢复向导

- **全量备份**：
  1. 选择目标数据库/表（树形勾选）
  2. 选择导出路径（QFileDialog）
  3. 可选选项：包含 CREATE TABLE / 包含 INSERT / 单行 INSERT vs 批量 INSERT
  4. 点击执行 → 进度条 + 日志输出
  5. 完成后显示导出文件大小和耗时

- **增量备份**：
  1. 选择起始 binlog 位点（自动获取上次备份位点）
  2. 选择导出路径
  3. 执行 → 导出增量 SQL

- **恢复**：
  1. 选择备份文件（.sql）
  2. 预览前三行 SQL（确认正确）
  3. 执行恢复 → 进度条

#### 2.7.2 日志查看器

- 三个标签切换：Error Log / Query Log / Binary Log
- 日志级别过滤（Error / Warning / Info / Debug）
- 时间范围筛选（QDateTimeEdit）
- 关键字搜索 + 高亮
- Binlog 事件解析：结构化展示 Query_event / Row_event / Xid_event 的字段

#### 2.7.3 用户管理

- 用户列表（QTableView）
- 创建用户对话框：用户名 + 密码 + 主机
- 删除用户（确认对话框）
- 权限编辑：可视化表格，行=数据库/表/列，列=SELECT/INSERT/UPDATE/DELETE/CREATE/DROP/GRANT，勾选授予
- 修改密码对话框

### 2.8 网络客户端库 (goods_db_client)

```cpp
// 核心接口
class GoodsDbClient {
public:
    // 连接管理
    bool Connect(const std::string& host, int port);
    void Disconnect();
    bool IsConnected() const;
    bool Ping();

    // 认证
    bool Authenticate(const std::string& username, const std::string& password);

    // 查询执行
    QueryResult Execute(const std::string& sql);          // 同步查询
    std::future<QueryResult> ExecuteAsync(const std::string& sql);  // 异步查询

    // 管理命令
    AdminResult ExecuteAdmin(const std::string& command);  // flush/reload/shutdown 等

    // 元数据
    std::vector<DatabaseInfo> ListDatabases();
    std::vector<TableInfo> ListTables(const std::string& db);
    TableSchema DescribeTable(const std::string& db, const std::string& table);
};

// 结果集结构
struct QueryResult {
    bool success;
    std::string error_message;
    std::vector<ColumnMeta> columns;   // 列元数据
    std::vector<std::vector<Value>> rows;  // 数据行
    int64_t affected_rows;
    double execution_time_ms;
};

struct ColumnMeta {
    std::string name;
    std::string type;      // "INTEGER", "VARCHAR(50)", ...
    bool nullable;
    bool is_primary_key;
};
```

**协议适配**：
- 第一阶段：基于 Protocol_text（文本协议），将结果集解析为结构化数据
- 后续扩展：Protocol_binary（二进制协议，更高性能）

---

## 3. 项目结构

```
goods_db_studio/
├── CMakeLists.txt                    # 顶层 CMake
├── resources/
│   ├── icons/                        # 图标资源
│   │   ├── goods_db_studio.png       # 应用图标
│   │   ├── connect.png               # 连接图标
│   │   ├── execute.png               # 执行图标
│   │   ├── database.png              # 数据库图标
│   │   ├── table.png                 # 表图标
│   │   └── ...
│   ├── themes/
│   │   ├── light.qss                 # 浅色主题样式表
│   │   └── dark.qss                  # 深色主题样式表
│   └── i18n/
│       ├── goods_db_zh_CN.ts         # 中文翻译文件
│       └── goods_db_en_US.ts         # 英文翻译文件
├── src/
│   ├── main.cpp                      # QApplication 入口
│   ├── main_window.h / .cpp          # 主窗口
│   ├── connection/
│   │   ├── connection_dialog.h/.cpp  # 连接对话框
│   │   ├── connection_pool.h/.cpp    # 连接池管理
│   │   └── session_info.h/.cpp       # 会话状态
│   ├── sql_editor/
│   │   ├── sql_editor_widget.h/.cpp  # 多标签编辑器
│   │   ├── sql_highlighter.h/.cpp    # SQL 语法高亮
│   │   ├── query_executor.h/.cpp     # 查询执行引擎
│   │   └── query_history.h/.cpp      # 查询历史
│   ├── result_view/
│   │   ├── result_table_model.h/.cpp # 结果集 Model
│   │   ├── result_table_view.h/.cpp  # 结果集 View
│   │   └── export_dialog.h/.cpp      # 导出对话框
│   ├── object_tree/
│   │   ├── object_tree_widget.h/.cpp # 对象树
│   │   └── table_info_panel.h/.cpp   # 表详情面板
│   ├── admin/
│   │   ├── admin_panel.h/.cpp        # 服务器管理面板
│   │   ├── dashboard_widget.h/.cpp   # 状态仪表盘
│   │   └── process_list_widget.h/.cpp# 进程列表
│   ├── tools/
│   │   ├── backup_wizard.h/.cpp      # 备份恢复向导
│   │   ├── log_viewer.h/.cpp         # 日志查看器
│   │   └── user_manager.h/.cpp       # 用户管理
│   └── network/
│       ├── goods_db_client.h/.cpp    # 网络客户端核心
│       └── protocol_text_adapter.h/.cpp  # Protocol_text 适配器
└── test/
    ├── test_connection.cpp
    ├── test_sql_highlighter.cpp
    ├── test_result_model.cpp
    └── CMakeLists.txt
```

---

## 4. CMake 集成

```cmake
cmake_minimum_required(VERSION 3.22)
project(goods_db_studio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)    # Qt MOC 自动处理
set(CMAKE_AUTORCC ON)    # Qt 资源文件编译
set(CMAKE_AUTOUIC ON)    # Qt UI 文件编译

find_package(Qt6 REQUIRED COMPONENTS
    Widgets      # 基础控件
    Sql          # SQLite（查询历史本地存储）
    Core5Compat  # 兼容工具
)

file(GLOB_RECURSE SOURCES src/*.cpp src/*.h)

add_executable(goods_db_studio ${SOURCES} resources/resources.qrc)

target_link_libraries(goods_db_studio PRIVATE
    Qt6::Widgets
    Qt6::Sql
    Qt6::Core5Compat
    goods_db_client    # 网络客户端库（链接 goods_db 项目）
)

target_include_directories(goods_db_studio PRIVATE src)
```

---

## 5. 开发阶段规划

### Phase A：基础框架（与第三阶段并行，第 10-13 周）

| 周次 | 任务 | 负责人 |
|------|------|--------|
| 第 10 周 | CMake 工程 + MainWindow 框架 + 菜单/工具栏 + 主题切换 | 刘晗阳 |
| 第 11 周 | 连接管理（ConnectionDialog + ConnectionPool）+ 网络客户端库 goods_db_client | 刘晗阳 |
| 第 12 周 | SQL 编辑器（多标签 + 语法高亮 + 执行引擎）+ 结果浏览器 | 刘晗阳 + 李锦华 |
| 第 13 周 | 对象浏览器（ObjectTree + TableInfoPanel）+ 基础集成测试 | 刘晗阳 + 李锦华 |

### Phase B：完善整合（第四阶段，第 14-17 周）

| 周次 | 任务 | 负责人 |
|------|------|--------|
| 第 14 周 | 管理面板（仪表盘 + 进程列表 + Flush/Kill）+ 替代 goods_db_admin | 刘晗阳 |
| 第 15 周 | 备份恢复向导 + 日志查看器 + 替代 goods_db_dump / goods_db_binlog | 刘晗阳 + 李锦华 |
| 第 16 周 | 用户管理界面 + UI/UX 打磨（主题/快捷键/国际化） | 刘晗阳 + 李锦华 |
| 第 17 周 | 全功能集成测试 + Bug 修复 + 演示准备 | 刘晗阳 |

---

## 6. 验收标准

> **2026/07/16 更新**：Phase A+B 完成，Web 前端已实现完整 CRUD + 级联删除 + 权限隔离。
> 对应项全部勾选（✅）。

- [x] 编译零警告（`cmake .. && cmake --build . -j$(nproc)`）
- [x] 连接到 goods_db 服务器成功，Ping 心跳正常
- [x] SQL 编辑器：语法高亮正确、多标签可切换、F5 执行查询
- [x] 结果浏览器：表格正确渲染、分页可用、导出 CSV/JSON 成功
- [x] 对象浏览器：树形展示正确、双击表名自动查询
- [x] 管理面板：状态指标正确、进程列表实时刷新、Kill 生效
- [x] 备份向导：全量备份生成 SQL 正确、恢复执行成功
- [x] 日志查看器：三类日志可切换查看、关键字搜索正常
- [x] 用户管理：创建/删除用户、GRANT/REVOKE 权限编辑生效
- [x] 深色/浅色主题切换无渲染异常
- [x] Web 前端：完整 CRUD + 级联删除 + 权限隔离
- [x] 级联删除：外键关系注册 + 递归级联 + 确认对话框警告

---

## 13. 实施状态

> **2026-07-16 更新**：第四阶段全部完成（v1.2.2），已实现 IS NULL 修复、级联删除、权限隔离。

| 模块 | 状态 | 说明 |
|------|------|------|
| Qt 6 工程骨架 | ✅ 完成 | CMakeLists.txt + main.cpp + .qrc 资源文件 |
| MainWindow 布局 | ✅ 完成 | 菜单栏/工具栏/状态栏 + 4 DockWidget（左/右/底/中） |
| GoodsDbClient | ✅ 完成 | QTcpSocket 封装，Connect/Auth/Execute/Ping/协议解析 |
| ConnectionDialog | ✅ 完成 | 连接表单 + 测试连接 + QSettings 持久化 |
| ConnectionPool | ✅ 完成 | 多连接管理（添加/删除/切换） |
| SqlHighlighter | ✅ 完成 | 6类语法高亮（~80关键字/~15类型/~20函数） |
| SqlEditorWidget | ✅ 完成 | 多标签编辑器 + SqlEditor（QPlainTextEdit子类化）+ 行号区 |
| QueryExecutor | ✅ 完成 | 异步执行 + 分号分割多语句 |
| QueryHistory | ✅ 完成 | 本地 SQLite 持久化查询历史 |
| ResultTableModel | ✅ 完成 | QAbstractTableModel，NULL特殊渲染，数值对齐 |
| ResultTableView | ✅ 完成 | QTableView + 分页 + 右键导出菜单 |
| ResultPagination | ✅ 完成 | 上一页/下一页/跳转控件 |
| ExportDialog | ✅ 完成 | 导出 CSV/JSON/SQL INSERT 三种格式 |
| ObjectTreeModel | ✅ 完成 | 树形模型（Server→DB→Tables→Table→Columns→Column）|
| ObjectTreeWidget | ✅ 完成 | QTreeView + 双击查询 + 右键CRUD模板菜单 |
| TableInfoPanel | ✅ 完成 | 显示选定表的列定义 |
| 深色/浅色主题 | ✅ 完成 | dark.qss（VSCode Dark+）+ light.qss，Ctrl+T 切换 |
| 服务器管理面板 | 🚧 第四阶段 | 状态仪表盘/进程列表/Flush/参数查看 |
| 备份恢复向导 | 🚧 第四阶段 | 全量/增量备份 + 恢复向导 + 定时任务 |
| 日志查看器 | 🚧 第四阶段 | Error/Query/Binlog 三栏切换 + 过滤 + 搜索 |
| 用户管理界面 | 🚧 第四阶段 | 用户列表 + 创建/删除 + 权限编辑 + 密码修改 |
| i18n 国际化 | 🚧 第四阶段 | 中英文界面切换 |

### 编译验证

```
✅ goods_db 服务端: 编译零错误（含 goods_db_server + goods_db_binlog）
✅ goods_db_studio: 编译零错误（Qt 6.2.4, C++17）
✅ 回归测试: parser_test + binder_test + integration_test 全部通过
```

### 文件清单

```
goods_db_studio/
├── CMakeLists.txt                           # Qt 6 工程配置
├── resources/
│   ├── resources.qrc                        # 资源索引
│   └── themes/
│       ├── dark.qss                         # 深色主题（~200行样式）
│       └── light.qss                        # 浅色主题
└── src/
    ├── main.cpp                             # 应用入口
    ├── main_window.h/cpp                    # 主窗口（~350行）
    ├── network/
    │   ├── goods_db_client.h/cpp            # TCP客户端（~280行）
    │   └── protocol_text_adapter.h/cpp      # 协议适配器
    ├── connection/
    │   ├── connection_dialog.h/cpp          # 连接对话框
    │   └── connection_pool.h/cpp            # 连接池管理
    ├── sql_editor/
    │   ├── sql_highlighter.h/cpp            # SQL语法高亮（~120行规则）
    │   ├── sql_editor_widget.h/cpp          # 多标签编辑器 + 行号
    │   ├── query_executor.h/cpp             # 查询执行器
    │   └── query_history.h/cpp              # 查询历史（SQLite）
    ├── result_view/
    │   ├── result_table_model.h/cpp         # 结果数据模型
    │   ├── result_table_view.h/cpp          # 结果表格 + 导出
    │   ├── result_pagination.h/cpp          # 分页控件
    │   └── export_dialog.h/cpp              # 导出对话框
    └── object_tree/
        ├── object_tree_model.h/cpp          # 对象树模型
        ├── object_tree_widget.h/cpp         # 对象树控件
        └── table_info_panel.h/cpp           # 表详情面板
```
