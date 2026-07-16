# goods_db_studio 用户手册

> goods_db 桌面客户端 + Web 前端 v1.2.2 — 用户操作指南

## 目录

1. [安装和编译](#1-安装和编译)
2. [连接配置](#2-连接配置)
3. [主界面概览](#3-主界面概览)
4. [SQL 编辑器](#4-sql-编辑器)
5. [对象浏览器](#5-对象浏览器)
6. [结果浏览器](#6-结果浏览器)
7. [管理面板 (AdminPanel)](#7-管理面板-adminpanel)
8. [备份恢复向导 (BackupWizard)](#8-备份恢复向导-backupwizard)
9. [日志查看器 (LogViewer)](#9-日志查看器-logviewer)
10. [用户管理 (UserManager)](#10-用户管理-usermanager)
11. [主题切换](#11-主题切换)
12. [Web 前端](#12-web-前端)
13. [常见问题](#13-常见问题)

---

## 1. 安装和编译

### 1.1 环境要求

| 组件 | 最低版本 |
|------|----------|
| 操作系统 | Ubuntu 22.04+ / Debian 12+ |
| C++ 编译器 | GCC 11+ 或 Clang 15+ |
| CMake | 3.16+ |
| Qt 6 | 6.2+ (Widgets, Sql, Network 模块) |
| 依赖库 | pthread |

### 1.2 编译步骤

```bash
# 1. 编译数据库服务器
cd goods_db/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(nproc)

# 2. 编译桌面客户端
cd goods_db_studio/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(nproc)

# 3. 编译 Web 服务器（自动随步骤2编译）
```

### 1.3 安装 Qt6 依赖

```bash
# Ubuntu 22.04
sudo apt install qt6-base-dev libqt6sql6-sqlite libqt6network6
```

---

## 2. 连接配置

### 2.1 启动服务器

```bash
# 启动 goods_db 数据库服务器（默认端口 3307）
./goods_db/build/src/sql/goods_db_server --port 3307
```

### 2.2 从桌面客户端连接

1. 启动 `goods_db_studio`
2. 点击工具栏 **New Connection** 按钮或菜单 **File → New Connection** (Ctrl+N)
3. 在弹出的对话框中填写：
   - **Connection Name**: 自定义名称（如 "Local Dev"）
   - **Host**: 服务器地址（本地为 `127.0.0.1`）
   - **Port**: 服务器端口（默认 `3307`）
   - **User**: 用户名
   - **Password**: 密码
4. 点击 **Connect** 测试连接
5. 状态栏显示绿色 "Connected" 表示连接成功

### 2.3 从 Web 前端连接

1. 启动 Web 服务器：`./goods_db_studio/build/goods_db_web`
2. 浏览器打开 `http://localhost:8080`
3. 在顶部连接栏填写 Host/Port/User/Password
4. 点击 **Connect** 按钮

---

## 3. 主界面概览

桌面客户端主界面采用经典 IDE 布局：

```
┌─────────────────────────────────────────────────────┐
│ Menu Bar   (File | Edit | Query | Tools | Settings) │
│ Tool Bar   (New Conn | Open | Save | ▶ | ■ | ✓ | ↩ )│
├──────┬──────────────────────────────┬───────────────┤
│Object│   Central QTabWidget         │   Table Info  │
│Tree  │ ┌────┬────┬────┬────┬────┐  │   Panel       │
│      │ │Home│SQL │Adm │Logs│User│  │               │
│      │ ├────┴────┴────┴────┴────┤  │               │
│      │ │   SQL Editor / Panel   │  │               │
│      │ │                        │  │               │
│      │ ├────────────────────────┤  │               │
│      │ │   Result Table         │  │               │
│      │ └────────────────────────┘  │               │
├──────┴──────────────────────────────┴───────────────┤
│ Status Bar  (Connected: 127.0.0.1:3307 | Auto-commit)│
└─────────────────────────────────────────────────────┘
```

### 3.1 中央标签页

| 标签 | 内容 | 说明 |
|------|------|------|
| **Home** | 欢迎首页 | 快捷操作按钮、最近连接列表 |
| **SQL** | SQL 编辑器 | 多标签 SQL 编辑，语法高亮 |
| **Admin** | 管理面板 | 服务器状态仪表盘和操作 |
| **Logs** | 日志查看器 | Error/Query/Binary 三标签日志 |
| **Users** | 用户管理 | 用户 CRUD + 权限编辑 |

Home 和 SQL 标签不可关闭；Admin/Logs/Users 标签可以关闭后从 **Tools** 菜单重新打开。

### 3.2 工具栏

| 按钮 | 图标 | 功能 | 快捷键 |
|------|------|------|--------|
| New Connection | 🔌 | 新建数据库连接 | Ctrl+N |
| Open File | 📂 | 打开 SQL 文件 | Ctrl+O |
| Save | 💾 | 保存 SQL 文件 | Ctrl+S |
| Execute | ▶ | 执行 SQL | F5 |
| Stop | ■ | 停止查询 | Esc |
| Commit | ✓ | 提交事务 | Ctrl+Shift+C |
| Rollback | ↩ | 回滚事务 | Ctrl+Shift+R |
| Export | ↑ | 导出结果 | — |
| Toggle Theme | ⚙ | 切换深色/浅色主题 | Ctrl+T |

---

## 4. SQL 编辑器

### 4.1 基本操作

- **执行 SQL**: 点击 Execute 按钮 (F5) 或菜单 **Query → Execute All**
- **执行选中部分**: 选中要执行的 SQL 文本，按 F5
- **停止查询**: 点击 Stop 按钮 (Esc)
- **新建编辑器标签**: 在 Web 前端中按 Ctrl+T
- **语法高亮**: 自动识别 SQL 关键字、字符串、数字、注释

### 4.2 快捷键

| 快捷键 | 功能 |
|--------|------|
| Ctrl+N | 新建连接 |
| Ctrl+O | 打开 SQL 文件 |
| Ctrl+S | 保存 SQL 文件 |
| F5 | 执行 SQL |
| Esc | 停止查询 |
| Ctrl+Shift+C | 提交事务 |
| Ctrl+Shift+R | 回滚事务 |
| Ctrl+Z | 撤销 |
| Ctrl+Y | 重做 |
| Ctrl+X | 剪切 |
| Ctrl+C | 复制 |
| Ctrl+V | 粘贴 |
| Ctrl+T | 切换主题 |
| Ctrl+Q | 退出程序 |

### 4.3 查询历史

查询历史自动保存到 localStorage（Web 前端）或内存（桌面端），记录：
- 执行时间
- SQL 语句
- 连接信息
- 执行耗时
- 是否成功

---

## 5. 对象浏览器

位于左侧 Dock，以树形结构展示数据库对象：

```
📡 goods_db (Connected)
├── 🗄️ warehouse_db
│   ├── 📁 Tables
│   │   ├── 📊 warehouses
│   │   │   └── 📁 Columns
│   │   │       ├── 🔹 id (INT)
│   │   │       ├── 🔹 name (VARCHAR)
│   │   │       └── 🔹 location (VARCHAR)
│   │   ├── 📊 products
│   │   ├── 📊 inventory
│   │   └── 📊 orders
│   └── 📁 Indexes
```

### 5.1 操作

- **双击表名**: 自动生成 `SELECT * FROM db.table LIMIT 100;` 并执行
- **单击表名**: 右侧 Table Info 面板显示表结构
- **右键菜单**: 复制表名 / 全名 / 生成 CRUD 模板
- **刷新**: 工具栏 Refresh 按钮
- **折叠**: 工具栏 Collapse All 按钮

---

## 6. 结果浏览器

位于底部 Dock，以表格形式显示查询结果。

### 6.1 功能

- **分页浏览**: 默认每页 100 行，支持翻页
- **排序**: 点击列标题排序
- **导出**: File → Export Results 支持三种格式：
  - **CSV**: 逗号分隔值，可导入 Excel
  - **JSON**: JSON 数组格式，适合程序处理
  - **SQL INSERT**: 生成 INSERT 语句，可直接执行

### 6.2 导出步骤

1. 执行查询（确保有返回结果）
2. 菜单 **Tools → Export Results**
3. 选择导出格式（CSV / JSON / SQL INSERT）
4. 选择保存路径
5. 点击 Export

---

## 7. 管理面板 (AdminPanel)

管理面板提供服务器运行状态监控和运维操作。

### 7.1 状态仪表盘

显示 5 个核心指标（5 秒自动刷新）：

| 指标 | 说明 | 来源命令 |
|------|------|----------|
| **QPS** | 每秒查询数 | SHOW STATUS → queries / uptime |
| **Connections** | 当前连接数 / 最大连接数 | SHOW STATUS → threads_connected |
| **Buffer Pool Hit Rate** | 缓冲池命中率 | SHOW STATUS → buffer_pool 指标 |
| **Dirty Pages** | 脏页数量 | SHOW STATUS → buffer_pool_pages_dirty |
| **Uptime** | 服务器运行时间 | SHOW STATUS → uptime |

### 7.2 进程列表

显示所有活跃连接信息：ID / User / Host / Database / Command / Time / State / Info

- **Kill Selected Connection**: 选中一行后点击，终止该连接

### 7.3 操作按钮

| 按钮 | 图标 | 功能 | SQL 命令 |
|------|------|------|----------|
| **Flush Hosts** | 🔄 | 清空主机缓存 | `FLUSH HOSTS` |
| **Flush Logs** | 🔄 | 刷新日志缓冲区到磁盘 | `FLUSH LOGS` |
| **Flush Tables** | 🔄 | 关闭并重新打开所有表文件 | `FLUSH TABLES` |
| **Reload** | ⟳ | 重新加载授权表 | `RELOAD` |
| **Ping** | 📶 | 检测服务器是否存活 | 客户端 Ping |
| **Shutdown** | ⏻ | 优雅关闭服务器 | `SHUTDOWN` |

### 7.4 刷新控制

- **Auto-refresh**: 勾选后自动刷新（默认 5 秒）
- **刷新间隔**: 可设置 1–300 秒
- **Refresh Now**: 手动立即刷新

### 7.5 事件日志

底部日志区域记录所有操作和服务器响应，包括：
- 每次刷新的时间戳
- 操作执行结果（成功/失败）
- 错误信息详情

---

## 8. 备份恢复向导 (BackupWizard)

通过分步向导完成数据库备份和恢复操作。

### 8.1 启动

菜单 **Tools → Backup & Restore Wizard**

### 8.2 备份流程

1. **Step 1: 选择操作类型**
   - Full Backup（全量备份）
   - Incremental Backup（增量备份）
   - Restore（恢复）

2. **Step 2: 选择数据库/表**
   - 从数据库列表中选择要备份的数据库
   - 可选择具体表（默认全库）

3. **Step 3: 选择输出路径**
   - 通过 Browse 选择备份文件保存位置
   - 默认文件名：`backup_YYYYMMDD_HHMMSS.sql`

4. **Step 4: 执行备份**
   - 显示备份进度条
   - 生成 DDL (CREATE TABLE) + DATA (INSERT INTO) 语句
   - 日志区域显示详细执行信息

### 8.3 恢复流程

1. 选择 Restore 操作
2. 选择备份 SQL 文件
3. 选择目标数据库
4. 执行恢复（逐条执行 SQL 语句）

---

## 9. 日志查看器 (LogViewer)

查看服务器端三类日志。

### 9.1 三个标签页

| 标签 | 内容 | 数据源 | 列信息 |
|------|------|--------|--------|
| **Error Log** | 错误日志 | SHOW ERRORLOG | Time / Level / Code / Message / Source |
| **Query Log** | 查询日志 | SHOW QUERYLOG | Time / User / Host / DB / Duration / Rows / Query |
| **Binary Log** | 二进制日志 | SHOW BINARYLOG | 原始文本格式 |

### 9.2 过滤功能

- **Level**: 按级别过滤（ALL / INFO / WARNING / ERROR / FATAL）
- **时间范围**: From / To 时间选择器，支持日历弹出
- **关键词**: 在 Message 字段中搜索关键词
- **Apply** 按钮应用过滤，**Clear** 按钮清除所有过滤条件

### 9.3 搜索功能

底部搜索栏：
- 输入搜索文本 → 回车
- **▲ Prev** / **▼ Next** 按钮上下导航搜索结果
- 支持跨列的全局文本搜索

### 9.4 自动刷新

勾选 **Auto-refresh (30s)** 开启 30 秒自动刷新。

---

## 10. 用户管理 (UserManager)

管理数据库用户账号和权限。

### 10.1 用户列表

左侧面板显示所有用户：User / Host / Created

### 10.2 用户操作

| 按钮 | 功能 | SQL 命令 |
|------|------|----------|
| **New User** | 创建新用户（弹出对话框填写 Username/Password/Host）| `CREATE USER` |
| **Delete** | 删除选中用户（确认后执行）| `DROP USER` |
| **Change Password** | 修改密码（弹出对话框输入新密码）| `ALTER USER ... IDENTIFIED BY` |

### 10.3 权限编辑

1. 在左侧用户列表选中一个用户
2. 中间的对象树中展开数据库 → 选择数据库或具体表
3. 右侧选择要授予的权限（CHECKBOX）：
   - SELECT / INSERT / UPDATE / DELETE（数据操作）
   - CREATE / DROP / INDEX（结构操作）
   - GRANT（授权他人）
4. 点击 **Load Permissions** 查看当前权限
5. 点击 **Apply Permissions** 应用更改

权限操作使用 `GRANT` / `REVOKE` SQL 语法，支持 `ON db.*`、`ON *.*`、`ON db.table` 格式。

---

## 11. 主题切换

### 11.1 深色主题（默认）

适合长时间编程，减少眼睛疲劳。深蓝黑色调 + 蓝色强调色 (#0078d4)。

### 11.2 浅色主题

适合明亮环境。白色背景 + 灰色边框。

### 11.3 切换方式

- 菜单 **Settings → Toggle Theme** (Ctrl+T)
- 工具栏 Toggle Theme 按钮 (⚙)
- 设置自动保存，重启后保持

---

## 12. Web 前端

goods_db 提供单文件零依赖 SPA Web 前端（`index.html`），通过 cpp-httplib 内嵌 HTTP 服务器与数据库通信。

### 12.1 启动 Web 服务器

```bash
./goods_db_studio/build/goods_db_web
# 默认监听 0.0.0.0:8080
# 可选参数: --host 0.0.0.0 --port 8080
```

### 12.2 浏览器访问

打开 `http://localhost:8080`

### 12.3 Web 前端功能

| 功能 | 说明 |
|------|------|
| **连接管理** | 顶栏 Host/Port/User/Pass 字段，连接状态指示灯 |
| **对象树** | 左侧树形展示数据库/表，单击浏览数据，右键菜单（表结构/查询/复制表名/行数统计） |
| **数据浏览** | 表头 tooltip 显示列类型 + 长度，数值列右对齐，NULL 斜体显示 |
| **排序** | 点击列头客户端排序，升序/降序切换（▲/▼ 箭头） |
| **筛选** | 展开筛选面板，每列独立输入框。字符串列模糊匹配（LIKE），数值列精确匹配（=） |
| **增删改** | ＋ 新增（表单自动识别列类型 → INSERT）、✏️ 编辑（主键锁定 → UPDATE）、🗑 删除（批量 Checkbox → DELETE） |
| **多标签 SQL** | Ctrl+T 新建标签，Ctrl+Enter / F5 执行，支持多条语句 |
| **结果导出** | 支持 CSV / JSON / SQL INSERT 三种格式 |
| **查询历史** | localStorage 持久化（最近 50 条），刷新页面不丢失 |
| **表结构查看** | 弹出 Modal 显示列名/类型/长度 |
| **分页** | 每页 100 行，首页/上一页/跳转/下一页 |
| **右键菜单** | 表名右键：浏览数据/查看结构/生成 SELECT/复制表名/行数统计 |

### 12.4 架构说明

```
浏览器 ←→ goods_db_web (cpp-httplib :8080) ←→ goods_db_server (:3307)
              │                    │
              │ 静态文件服务        │ JSON REST API
              │ (index.html)       │ /api/connect
              │                    │ /api/execute
              │                    │ /api/databases
              │                    │ /api/tables
              │                    │ /api/columns
```

Web 前端为纯静态 HTML/CSS/JS，无外部依赖，所有样式内联，支持深色主题。

---

## 13. 常见问题

### Q: 连接失败，提示 "Connection refused"
- 确认 goods_db 服务器已启动
- 检查端口号是否正确（默认 3307）
- 检查防火墙设置

### Q: 执行 SQL 报错 "No active connection"
- 先通过 File → New Connection 连接到服务器
- 状态栏应显示绿色 "Connected"

### Q: 主题切换后显示异常
- 尝试重启客户端程序
- 检查 `resources/themes/` 下 QSS 文件是否完整

### Q: AdminPanel 仪表盘指标显示 "--"
- 确认服务器在线且已连接
- 点击 "Refresh Now" 手动刷新
- 检查服务器是否支持 SHOW STATUS 命令

### Q: 备份文件生成在哪里
- 备份向导会询问保存路径
- 默认生成 .sql 文件，包含 DDL + DATA

### Q: Web 前端页面打不开
- 确认 Web 服务器已启动（`./goods_db_web`）
- 检查终端输出确认端口号（默认 `http://localhost:8080`）
- 检查 `web_root` 是否正确找到 `index.html`（查看 Web 服务器启动日志）
- 如果 index.html 找不到，确认编译目录结构正确

### Q: Web 前端删除数据后还在 / 筛选没结果
- **v1.1.0 已修复**：旧版存在 JSON API 键名不匹配 bug（`"type"` vs `type_name`）
- 如果仍出现此问题，请重新编译 `goods_db_web`
- 确认执行 `cmake --build . -j$(nproc)` 后重启 Web 服务器

### Q: SQL 结果表格列对齐混乱
- **v1.1.0 已修复**：多列结果集自动启用 `table-layout: fixed`
- 鼠标悬停单元格可查看被截断的完整内容（tooltip）

### Q: 删除父表数据时子表关联数据也自动删除吗
- **v1.2.2 新增**：是的，系统支持级联删除（CASCADE DELETE）。
- 删除 `warehouses` 中的数据时，`inventory` 和 `shipments` 中关联行自动删除
- 删除确认对话框会展示级联影响范围（如"级联删除 inventory → order_items"）
- 级联关系由 `REGISTER_FK` 命令注册

### Q: 如何报告 Bug
- GitHub Issues: [项目仓库地址]
- 附上错误日志截图和操作步骤

---

## 附录A：菜单快捷键速查表

| 菜单 | 项目 | 快捷键 |
|------|------|--------|
| File | New Connection | Ctrl+N |
| File | Open SQL File | Ctrl+O |
| File | Save SQL File | Ctrl+S |
| File | Exit | Ctrl+Q |
| Edit | Undo | Ctrl+Z |
| Edit | Redo | Ctrl+Y |
| Edit | Cut | Ctrl+X |
| Edit | Copy | Ctrl+C |
| Edit | Paste | Ctrl+V |
| Query | Execute All | F5 |
| Query | Stop | Esc |
| Query | Commit | Ctrl+Shift+C |
| Query | Rollback | Ctrl+Shift+R |
| Settings | Toggle Theme | Ctrl+T |

## 附录B：支持的管理命令

| 命令 | 说明 |
|------|------|
| `SHOW STATUS` | 服务器状态指标 |
| `SHOW PROCESSLIST` | 活跃连接列表 |
| `SHOW USERS` | 用户列表 |
| `SHOW GRANTS FOR 'user'@'host'` | 用户权限 |
| `SHOW ERRORLOG` | 错误日志 |
| `SHOW QUERYLOG` | 查询日志 |
| `SHOW BINARYLOG` | 二进制日志 |
| `FLUSH HOSTS` | 清空主机缓存 |
| `FLUSH LOGS` | 刷新日志 |
| `FLUSH TABLES` | 刷新表 |
| `RELOAD` | 重载授权表 |
| `KILL <id>` | 终止连接 |
| `SHUTDOWN` | 关闭服务器 |
| `REGISTER_FK parent.col child.col [CASCADE]` | 注册外键级联关系 |

---

> 文档版本: v1.2.2 | 更新时间: 2026-07-16 | 适用于 goods_db v1.2.2
