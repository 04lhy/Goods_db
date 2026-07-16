# goods_db 第三阶段 — 操作手册

> 涵盖设计五（日志）、设计六（网络协议）、设计七（线程/内存管理）、设计九（安全管理）、设计十（桌面客户端）

---

## 一、系统架构概览

```
┌──────────────────────────────────────────────────────────────┐
│                goods_db_studio (Qt 6 桌面客户端)              │
│   SQL 编辑器 · 结果浏览器 · 对象树 · 连接管理 · 导出          │
├──────────────────────────────────────────────────────────────┤
│                 TCP/IP 网络协议 (Protocol_text)               │
│              Protocol 基类 → Protocol_text / Protocol_binary │
├──────────────────────────────────────────────────────────────┤
│          ConnectionHandler → 连接认证 → SQL 执行 → 结果返回   │
├──────────────────┬──────────────────┬────────────────────────┤
│  安全管理         │  日志子系统       │  线程池                 │
│  AuthManager     │  Error/Query/     │  ThreadPool            │
│  SHA-256 + ACL   │  BinaryLog        │  连接并发处理            │
├──────────────────┴──────────────────┴────────────────────────┤
│              查询引擎 + 存储引擎（第一/二阶段）                 │
└──────────────────────────────────────────────────────────────┘
```

---

## 二、编译与启动

### 2.1 编译服务端

```bash
cd goods_db/build
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_C_COMPILER=clang-15 \
         -DCMAKE_CXX_COMPILER=clang++-15
make -j$(nproc)
```

产出文件：
- `goods_db/build/src/sql/goods_db_server` — 数据库服务器
- `goods_db/build/src/sql/goods_db_binlog` — Binlog 查看工具

### 2.2 编译桌面客户端

```bash
cd goods_db_studio/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

产出文件：
- `goods_db_studio/build/goods_db_studio` — 桌面客户端

### 2.3 启动服务器

```bash
# 默认配置：监听 0.0.0.0:3307
./goods_db/build/src/sql/goods_db_server

# 自定义配置
./goods_db/build/src/sql/goods_db_server \
    --host 127.0.0.1 \
    --port 3307 \
    --threads 8 \
    --datadir ./mydata

# 查看帮助
./goods_db/build/src/sql/goods_db_server --help
```

### 2.4 启动桌面客户端

```bash
./goods_db_studio/build/goods_db_studio
```

---

## 三、网络协议层（设计六）

### 3.1 类层次结构

```
Protocol (抽象基类)
├── Protocol_text   — 文本协议，适合 telnet/nc 调试
└── Protocol_binary — 二进制协议，适合 Prepared Statement 高性能场景

Connection          — 封装 TCP socket + 协议实例
SocketServer        — TCP 监听 + accept 连接
```

### 3.2 协议包格式

**包结构（4 字节头）：**
```
Byte 0-2: payload_length (小端序, 最大 16MB)
Byte 3:   sequence_id (递增，用于请求-响应匹配)
后跟 payload
```

**命令包（客户端→服务端）：**
```
AUTH\0user\0password\0db\0   — 认证
QUERY\0sql_text               — SQL 查询
PING                          — 心跳检测
QUIT                          — 断开连接
```

**响应包（服务端→客户端）：**
```
OK affected_rows last_insert_id        — 成功响应
ERR error_code sql_state message       — 错误响应
Columns: N\n  + N×ColDef + Row 数据   — 结果集
EOF                                     — 结果集结束
```

### 3.3 Text 协议示例

```
# 客户端发送
AUTH\0root\0\0warehouse_db\0

# 服务端响应
OK 0 0 Authentication successful

# 客户端发送
QUERY\0SELECT * FROM products LIMIT 5;

# 服务端响应
Columns: 4
ColDef: id INT 0
ColDef: name VARCHAR 200
ColDef: sku VARCHAR 50
ColDef: unit_price DECIMAL 0
Row: 1\t商品A\tSKU001\t99.00
Row: 2\t商品B\tSKU002\t199.00
EOF
```

### 3.4 使用 telnet 测试

```bash
telnet localhost 3307
# 输入: AUTH\0root\0\0\0     （Ctrl+@ 输入 \0）
# 输入: QUERY\0SELECT 1;
```

---

## 四、线程与服务器管理（设计七）

### 4.1 ThreadPool

- 固定大小工作线程池（默认 = CPU 核心数）
- 线程安全任务队列（std::mutex + std::condition_variable）
- `Submit(task)` → `std::future<Result>`
- `Shutdown()`：优雅关闭，等待所有任务完成
- `ShutdownNow()`：强制关闭，丢弃未执行任务

### 4.2 ConnectionHandler

每个客户端连接的生命周期管理：

```
INIT → AUTH → READY → QUERYING → SENDING → READY → ... → CLOSING
```

1. 发送握手问候（`goods_db 0.1.0`）
2. 等待认证包 → 调用 `AuthManager::CheckConnection()`
3. 主循环：读命令 → 分发（HandleQuery/HandlePing/HandleQuit）
4. 执行 SQL → 通过 Protocol 序列化结果 → 发送
5. 连接断开时清理资源

### 4.3 服务器启动流程

```
main():
  1. 解析命令行参数
  2. 初始化 Logger
  3. 创建 DiskManager / BufferPoolManager / Catalog
  4. 注册存储引擎 (register_engine)
  5. 初始化 AuthManager / LogManager
  6. 创建 goods_handler 引擎实例
  7. 启动 ThreadPool
  8. 创建 SocketServer → Listen(host, port)
  9. accept 循环 → 每个连接 Submit 到 ThreadPool
  10. SIGTERM/SIGINT → 优雅关闭
```

---

## 五、日志子系统（设计五）

### 5.1 三层日志架构

```
LogManager
├── ErrorLog  — 服务器错误/警告/信息
├── QueryLog  — 所有 SQL 查询记录
└── BinaryLog — WAL binlog（用于复制和增量备份）
```

### 5.2 ErrorLog

**格式：** `[时间戳] [级别] [线程ID] [文件:行号] 消息`

**日志级别：** DEBUG < INFO < WARN < ERROR < FATAL

**输出目标：** 文件 + stderr（可配置）

**配置项：**
- `min_level`：最低记录级别（默认 INFO）
- `also_stderr`：是否同时输出到 stderr

### 5.3 QueryLog

**格式：** `[时间戳] [user@host] [database] [exec_time_ms] [rows] sql_text`

**功能：**
- 自动文件轮转（默认 100MB 切割）
- 按用户/数据库/最小执行时间过滤（配置项）
- 隐私脱敏：密码相关 SQL 自动替换明文为 `***`

### 5.4 BinaryLog

**事件类型：**
| 类型 | 编码 | 说明 |
|------|------|------|
| QUERY | 0x01 | DDL/DML SQL 语句 |
| ROW_INSERT | 0x10 | 行插入 |
| ROW_UPDATE | 0x11 | 行更新 |
| ROW_DELETE | 0x12 | 行删除 |
| XID | 0x20 | 事务提交 |

**事件格式：**
```
[timestamp:8B][server_id:4B][event_type:1B][txn_id:8B][payload_len:4B][payload:N][CRC32:4B]
```

**文件管理：**
- 文件命名：`goods_db_binlog.000001`, `goods_db_binlog.000002`, ...
- 索引文件：`goods_db_binlog.index` 记录所有 binlog 文件列表
- 轮转：文件达到 `max_binlog_size`（默认 1GB）自动切换
- 写入：事务提交前 `Flush()` + `fsync()` 确保持久化

### 5.5 goods_db_binlog 工具

```bash
# 查看 binlog 内容
goods_db_binlog --verbose goods_db_binlog.000001

# 从指定位点开始
goods_db_binlog --start-position=1000 goods_db_binlog.000001

# 指定位点范围
goods_db_binlog --start-position=100 --stop-position=5000 goods_db_binlog.000001
```

**输出示例：**
```
# 2026-07-10 10:30:45 server_id=1 event=Query txn_id=42 len=28 crc=a3f2b1c0
INSERT INTO products VALUES (1, '商品A', 'SKU001', 99.00)
# 2026-07-10 10:30:45 server_id=1 event=Xid txn_id=42 len=0 crc=0
### COMMIT (XID)
```

### 5.6 日志管理 SQL 命令

```sql
SHOW LOGS;                              -- 列出所有日志文件及大小
SHOW BINLOG EVENTS IN 'binlog.000001';  -- 查看 binlog 事件列表
SHOW MASTER STATUS;                     -- 当前 binlog 文件名和位点
FLUSH LOGS;                             -- 强制轮转所有日志文件
PURGE BINARY LOGS TO 'binlog.000005';   -- 删除指定文件之前的 binlog
RESET MASTER;                           -- 删除所有 binlog 并重置
```

---

## 六、安全管理子系统（设计九）

### 6.1 认证流程

```
客户端连接 → 服务端发送握手 → 客户端发送 user/password
    → AuthManager::CheckConnection(host, user, password)
    → SHA-256(password + random_salt) == stored_hash ?
    → YES: 发送 OK, 加载权限缓存
    → NO:  发送 ERR, 记录失败, 封锁规则检查
```

### 6.2 ACL 权限模型

**三张系统表：**

| 表 | 粒度 | 权限字段 |
|----|------|---------|
| `goods_db.user` | 全局 | 用户认证信息（host, user, password_hash, salt） |
| `goods_db.db` | 数据库级 | select/insert/update/delete/create/drop/index/grant |
| `goods_db.tables_priv` | 表级 | 同上 + column_priv（SET 类型） |

**权限匹配优先级：** 精确 host+user+db+table > 通配符 host(%) > 通配符 db/table

### 6.3 权限位掩码

```cpp
enum class Privilege : uint32_t {
  NONE   = 0,
  SELECT = 1 << 0,   INSERT = 1 << 1,   UPDATE = 1 << 2,
  DELETE = 1 << 3,   CREATE = 1 << 4,   DROP   = 1 << 5,
  INDEX  = 1 << 6,   ALTER  = 1 << 7,   GRANT  = 1 << 8,
  ALL    = 0xFFFFFFFF,
};
```

### 6.4 安全 SQL 命令

```sql
-- 用户管理
CREATE USER 'app_user'@'%' IDENTIFIED BY 'secure_password';
DROP USER 'app_user'@'%';
ALTER USER 'app_user'@'%' IDENTIFIED BY 'new_password';
SET PASSWORD = 'new_password';  -- 修改当前用户密码

-- 权限管理
GRANT SELECT, INSERT ON warehouse_db.* TO 'app_user'@'%';
GRANT ALL ON warehouse_db.products TO 'app_user'@'%';
REVOKE INSERT ON warehouse_db.* FROM 'app_user'@'%';
FLUSH PRIVILEGES;  -- 重新加载权限缓存
```

### 6.5 密码安全

- 哈希算法：SHA-256(password + 16字节随机salt)
- 存储格式：password_hash = hex(SHA-256), salt = hex(random)
- 主机封锁：同一 IP 连续失败 10 次 → 封锁 5 分钟

---

## 七、桌面客户端（设计十）

### 7.1 界面布局

```
┌─────────────────────────────────────────────────────────────┐
│ 菜单栏: File | Edit | Query | Tools | Settings | Help        │
│ 工具栏: [新建连接] [打开] [执行F5] [停止] [提交] [回滚]       │
├───────────┬──────────────────────────┬──────────────────────┤
│ 对象浏览器  │   SQL 编辑器（多标签）     │  表详情面板            │
│ (左Dock)  │   - 语法高亮              │  (右Dock)            │
│           │   - 行号                  │  列名 | 类型          │
│  Server   │   - 代码补全              │                      │
│  ├─ DB1   ├──────────────────────────┤                      │
│  │  ├─表1 │   结果浏览器               │                      │
│  │  ├─表2 │   (底部Dock)              │                      │
│  │  └─索引 │   - 表格渲染              │                      │
│  └─ DB2   │   - 分页浏览              │                      │
│           │   - 导出 CSV/JSON/SQL     │                      │
├───────────┴──────────────────────────┴──────────────────────┤
│ 状态栏: ● Connected: localhost:3307 | DB: warehouse_db | Auto-commit ON │
└─────────────────────────────────────────────────────────────┘
```

### 7.2 连接管理

1. **新建连接**：Ctrl+N → 填写主机/端口/用户/密码/数据库 → 测试连接 → 确定
2. **连接状态**：状态栏绿色圆点表示已连接，红色表示断开
3. **多连接管理**：Settings → Manage Connections，支持多服务器同时连接
4. **持久化**：连接信息加密存储到 QSettings（密码加密）

### 7.3 SQL 编辑器

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+T` | 新建标签页 |
| `Ctrl+W` | 关闭标签页 |
| `Ctrl+O` | 打开 SQL 文件 |
| `Ctrl+S` | 保存 SQL 文件 |
| `F5` | 执行全部 SQL |
| `Ctrl+Return` | 执行当前语句 |
| `Esc` | 停止执行 |

**语法高亮规则：**
| 类别 | 颜色 | 示例 |
|------|------|------|
| SQL 关键字 | 蓝色加粗 | SELECT, FROM, WHERE, JOIN... |
| 数据类型 | 青色 | INT, VARCHAR, DECIMAL... |
| 字符串 | 橙红色 | 'hello' |
| 数字 | 浅绿色 | 123, 3.14 |
| 注释 | 灰色斜体 | -- comment, /* block */ |
| 函数 | 紫色 | COUNT(), SUM(), AVG()... |

### 7.4 结果浏览器

- **NULL 值**：灰色斜体 `(NULL)`
- **数值**：右对齐
- **字符串**：左对齐
- **长文本**：悬停 tooltip 显示完整值
- **大结果集**：>1000 行自动分页
- **导出格式**：CSV / JSON / SQL INSERT

### 7.5 对象浏览器

- **树形结构**：服务器 → 数据库 → Tables → 表 → Columns → 列名(类型)
- **双击表名**：自动生成 `SELECT * FROM db.table LIMIT 100;`
- **右键菜单**：打开表、查看结构、复制表名、生成 CRUD 模板

### 7.6 主题

- **深色主题**（默认）：VSCode Dark+ 风格配色
- **浅色主题**：标准 IDE 亮色风格
- **切换**：Settings → Toggle Theme 或 `Ctrl+T`

---

## 八、文件清单

### 服务端新增文件（35 个）

```
goods_db/src/include/sql/
├── protocol/
│   ├── protocol.h            # Protocol 抽象基类
│   ├── protocol_text.h       # 文本协议
│   └── protocol_binary.h     # 二进制协议
├── network/
│   ├── connection.h          # TCP 连接封装
│   └── net_serv.h            # 网络监听服务
├── server/
│   ├── thread_pool.h         # 线程池
│   └── connection_handler.h  # 连接处理器
├── log/
│   ├── error_log.h           # 错误日志
│   ├── query_log.h           # 查询日志
│   ├── binary_log.h          # 二进制日志
│   └── log_manager.h         # 日志管理器
└── security/
    └── auth_manager.h        # 认证与授权

goods_db/src/sql/
├── protocol/{protocol,protocol_text,protocol_binary}.cpp
├── network/{connection,net_serv}.cpp
├── server/{thread_pool,connection_handler,goods_db_main}.cpp
├── log/{error_log,query_log,binary_log,log_manager}.cpp
├── security/auth_manager.cpp
└── binlog/goods_db_binlog.cpp
```

### 桌面客户端文件（17 个 .h + 17 个 .cpp + CMakeLists.txt + 3 资源文件）

```
goods_db_studio/
├── CMakeLists.txt
├── resources/
│   ├── resources.qrc
│   └── themes/{dark,light}.qss
└── src/
    ├── main.cpp
    ├── main_window.{h,cpp}
    ├── network/
    │   ├── goods_db_client.{h,cpp}
    │   └── protocol_text_adapter.{h,cpp}
    ├── connection/
    │   ├── connection_dialog.{h,cpp}
    │   └── connection_pool.{h,cpp}
    ├── sql_editor/
    │   ├── sql_highlighter.{h,cpp}
    │   ├── sql_editor_widget.{h,cpp}
    │   ├── query_executor.{h,cpp}
    │   └── query_history.{h,cpp}
    ├── result_view/
    │   ├── result_table_model.{h,cpp}
    │   ├── result_table_view.{h,cpp}
    │   ├── result_pagination.{h,cpp}
    │   └── export_dialog.{h,cpp}
    └── object_tree/
        ├── object_tree_model.{h,cpp}
        ├── object_tree_widget.{h,cpp}
        └── table_info_panel.{h,cpp}
```

---

## 九、默认用户

| 用户 | 主机 | 密码 | 权限 |
|------|------|------|------|
| root | localhost | （空密码） | ALL |
| root | % | （空密码） | ALL |

> ⚠️ **安全提示：** 生产环境请立即修改 root 密码：
> ```sql
> ALTER USER 'root'@'localhost' IDENTIFIED BY 'new_strong_password';
> ```

---

## 十、第四阶段新增功能（v1.2.2）

### 10.1 级联删除 (CASCADE DELETE)

删除父表行时自动级联删除所有子表关联行，支持多层递归。

**注册外键关系**：
```sql
REGISTER_FK parent_table.parent_column child_table.child_column [CASCADE|RESTRICT|SET_NULL]
```

**示例**：
```sql
REGISTER_FK warehouses.id inventory.warehouse_id CASCADE;
REGISTER_FK customers.id orders.customer_id CASCADE;
REGISTER_FK orders.id order_items.order_id CASCADE;
```

**级联效果**：删除 `warehouses.id=1` 时，自动删除 `inventory` 和 `shipments` 中 `warehouse_id=1` 的所有行。

**Web 前端**：删除确认对话框中展示级联删除警告，列出所有受影响的子表。

### 10.2 IS NULL 修复（v1.2.1）

`WHERE column IS NULL` 和 `IS NOT NULL` 在 v1.2.0 中始终返回 FALSE。修复后正确匹配 NULL 列。

### 10.3 权限隔离

系统默认三个用户：
| 用户 | 密码 | 权限 |
|------|------|------|
| root | （空） | 全部权限 |
| admin | 12345 | 全部权限 |
| guest | （空） | 仅 SELECT |

以 `guest` 身份登录可验证权限隔离效果。

---

## 十一、配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host` | 0.0.0.0 | 绑定地址 |
| `--port` | 3307 | 监听端口 |
| `--threads` | CPU核心数 | 线程池大小 |
| `--datadir` | ./data | 数据目录 |
| `max_binlog_size` | 1 GB | Binlog 轮转大小 |
| `lock_wait_timeout` | 30 秒 | 锁等待超时 |
| `max_auth_failures` | 10 次 | 认证失败封锁阈值 |
| `block_duration` | 300 秒 | IP 封锁时长 |
