# goods_db 服务器部署指南

> **版本**: goods_db v1.2.2  
> **适用环境**: Ubuntu 22.04+ / Docker  
> **最后更新**: 2026-07-16

---

## 目录

1. [快速开始](#快速开始)
2. [配置文件说明](#配置文件说明)
3. [Docker 部署](#docker-部署)
4. [物理机部署 (systemd)](#物理机部署-systemd)
5. [命令行参数](#命令行参数)
6. [性能调优](#性能调优)
7. [安全加固](#安全加固)
8. [运维管理](#运维管理)
9. [故障排查](#故障排查)

---

## 快速开始

### 1. 编译

```bash
# 服务端
mkdir -p goods_db/build && cd goods_db/build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# 桌面客户端 + Web 前端
mkdir -p goods_db_studio/build && cd goods_db_studio/build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

编译产物：
- `goods_db/build/src/sql/goods_db_server` — 数据库服务器
- `goods_db/build/tools/goods_db_admin` — 命令行管理工具
- `goods_db/build/tools/goods_db_dump` — 数据导出工具
- `goods_db_studio/build/goods_db_web` — Web 前端服务器
- `goods_db_studio/build/goods_db_studio` — 桌面客户端

### 2. 启动服务器

```bash
cd goods_db/build
rm -rf ./data
./src/sql/goods_db_server --port 3307
```

### 3. 连接测试

```bash
# 使用 telnet 测试连接
telnet 127.0.0.1 3307
```

### 4. 使用桌面客户端

编译并启动 `goods_db_studio`（Qt 6 桌面客户端），在连接对话框中填写：
- 主机: `127.0.0.1`
- 端口: `3307`
- 用户名: `root`
- 密码: （默认无密码）

---

## 配置文件说明

配置文件 `goods_db.conf` 使用 INI 格式（参见 [../goods_db.conf](../goods_db.conf)）。

### 配置项完整清单

| 配置项 | 默认值 | 取值范围 | 说明 | 生效时机 |
|--------|--------|---------|------|---------|
| **基本配置** | | | | |
| `bind_address` | 0.0.0.0 | IP 地址 | 监听地址 | 启动时 |
| `port` | 3307 | 1–65535 | 监听端口 | 启动时 |
| `datadir` | ./data | 目录路径 | 数据存储目录 | 启动时 |
| `pid_file` | goods_db.pid | 文件路径 | PID 文件路径 | 启动时 |
| **线程池** | | | | |
| `thread_pool_size` | 0 (auto) | 1–1024 | 工作线程数，0 = CPU 核心数 | 启动时 |
| `max_connections` | 200 | 1–10000 | 最大并发连接数 | 启动时 |
| `connection_timeout` | 28800 | 1–86400 | 连接空闲超时（秒） | 启动时 |
| **缓冲池** | | | | |
| `buffer_pool_size` | 100 | 10–100000 | Buffer Pool 页数 (每页 16KB) | 启动时 |
| **日志** | | | | |
| `log_error` | goods_db_error.log | 文件路径 | 错误日志路径 | 启动时 |
| `log_query` | goods_db_query.log | 文件路径 | 查询日志路径 | 启动时 |
| `log_binlog_prefix` | goods_db_binlog | 文件前缀 | binlog 文件前缀 | 启动时 |
| `log_error_level` | INFO | DEBUG/INFO/WARN/ERROR/FATAL | 错误日志级别 | 启动时 |
| `max_binlog_size` | 1073741824 | 4096–104857600 | binlog 文件最大字节数 (默认 1GB) | 启动时 |
| `max_querylog_size` | 104857600 | 4096–104857600 | 查询日志最大字节数 (默认 100MB) | 启动时 |
| `binlog_expire_days` | 7 | 1–365 | binlog 自动清理天数 | 运行时 |
| **安全** | | | | |
| `max_auth_failures` | 10 | 1–1000 | IP 封锁前最大失败次数 | 启动时 |
| `auth_block_duration` | 300 | 1–86400 | IP 封锁时间（秒） | 启动时 |
| `require_secure_transport` | false | true/false | 是否要求安全传输 | 启动时 |
| **事务/并发** | | | | |
| `lock_wait_timeout` | 30000 | 100–86400000 | 锁等待超时（毫秒） | 启动时 |
| `max_txn_duration` | 0 | 0–86400 | 事务最大持续时间（秒），0 = 不限制 | 运行时 |
| `default_isolation_level` | READ COMMITTED | READ UNCOMMITTED/READ COMMITTED/REPEATABLE READ/SERIALIZABLE | 默认隔离级别 | 启动时 |
| `xid_persist_file` | goods_db_xid.dat | 文件路径 | XID 持久化文件 | 启动时 |
| `xid_persist_interval` | 1000 | 1–100000 | XID 持久化间隔（每 N 次分配） | 运行时 |
| **网络** | | | | |
| `tcp_backlog` | 128 | 1–4096 | TCP 连接队列长度 | 启动时 |
| `net_read_timeout` | 30 | 1–3600 | 网络读取超时（秒） | 运行时 |
| `net_write_timeout` | 60 | 1–3600 | 网络写入超时（秒） | 运行时 |
| `tcp_nodelay` | true | true/false | 是否禁用 Nagle 算法 | 启动时 |

---

## Docker 部署

### 构建镜像

```bash
cd /path/to/Goods_db
docker compose build
```

### 启动服务

```bash
# 开发模式（前台运行）
docker compose run --rm --service-ports bustub bash

# 生产模式（后台运行）
docker compose up -d
```

### Docker Compose 配置 (`docker-compose.yml`)

```yaml
version: '3.8'
services:
  goods_db:
    build: .
    ports:
      - "3307:3307"
    volumes:
      - ./data:/app/data
      - ./goods_db.conf:/app/goods_db.conf
    command: >
      /app/goods_db/build/src/sql/goods_db_server
      --host 0.0.0.0
      --port 3307
      --datadir /app/data
      --threads 4
    restart: unless-stopped
```

---

## 物理机部署 (systemd)

### 1. 创建系统用户

```bash
sudo useradd -r -s /bin/false goods_db
sudo mkdir -p /var/lib/goods_db/data
sudo mkdir -p /var/log/goods_db
sudo chown -R goods_db:goods_db /var/lib/goods_db /var/log/goods_db
```

### 2. 安装二进制文件

```bash
sudo cp goods_db_server /usr/local/bin/
sudo chmod 755 /usr/local/bin/goods_db_server
```

### 3. 创建配置文件

```bash
sudo cp goods_db.conf /etc/goods_db/
sudo chown goods_db:goods_db /etc/goods_db/goods_db.conf
sudo chmod 640 /etc/goods_db/goods_db.conf
```

### 4. 安装 systemd 服务

```bash
sudo cp goods_db.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable goods_db
sudo systemctl start goods_db
```

### 5. 管理服务

```bash
# 查看状态
sudo systemctl status goods_db

# 查看日志
sudo journalctl -u goods_db -f

# 停止 / 重启
sudo systemctl stop goods_db
sudo systemctl restart goods_db
```

---

## 命令行参数

```
goods_db_server [options]

选项:
  --host HOST        绑定地址（默认: 0.0.0.0）
  --port PORT        监听端口（默认: 3307）
  --threads N        线程池大小（默认: CPU 核心数）
  --datadir DIR      数据目录（默认: ./data）
  --config FILE      配置文件路径
  --help             显示帮助信息
```

---

## 性能调优

### Buffer Pool 配置

```ini
# Buffer Pool 大小 = 页数 × 16KB
# 建议设置为物理内存的 50%–70%
buffer_pool_size = 64000  # 约 1GB
```

### 线程池配置

```ini
# 工作线程数 = CPU 核心数 × (1.5 到 2) 
# 对于 I/O 密集型负载可以使用更大值
thread_pool_size = 8
```

### 日志调优

```ini
# 高性能场景：减少日志级别
log_error_level = WARN
# 关闭查询日志（设置 max_querylog_size = 0 或在代码中禁用）
```

### 网络调优

```bash
# 系统级优化（/etc/sysctl.conf）
net.core.somaxconn = 4096
net.ipv4.tcp_max_syn_backlog = 4096
net.ipv4.tcp_tw_reuse = 1
```

---

## 安全加固

### 首次登录后设置 root 密码

```sql
ALTER USER 'root'@'localhost' IDENTIFIED BY 'strong_password_here';
FLUSH PRIVILEGES;
```

### 创建应用专用用户

```sql
CREATE USER 'app_user'@'%' IDENTIFIED BY 'app_password';
GRANT SELECT, INSERT, UPDATE, DELETE ON goods_db.* TO 'app_user'@'%';
FLUSH PRIVILEGES;
```

### 最小权限原则

```sql
-- 只读用户
CREATE USER 'readonly'@'%' IDENTIFIED BY 'readonly_pass';
GRANT SELECT ON goods_db.* TO 'readonly'@'%';

-- 备份用户（需要 SELECT + LOCK TABLES 等效权限）
CREATE USER 'backup'@'localhost' IDENTIFIED BY 'backup_pass';
GRANT SELECT ON *.* TO 'backup'@'localhost';
```

---

## 运维管理

### 日志管理

```sql
-- 查看当前 binlog 位置
SHOW MASTER STATUS;

-- 查看 binlog 事件
SHOW BINLOG EVENTS;
SHOW BINLOG EVENTS IN 'goods_db_binlog.000001' FROM 0 LIMIT 10;

-- 清理旧 binlog（保留最近 7 天）
PURGE BINARY LOGS TO 'goods_db_binlog.000003';

-- 强制日志轮转
FLUSH LOGS;

-- 重置 binlog（谨慎使用）
RESET MASTER;
```

### 用户管理

```sql
SHOW LOGS;  -- 列出日志文件

CREATE USER 'newuser'@'%' IDENTIFIED BY 'password';
DROP USER 'olduser'@'%';
ALTER USER 'user'@'localhost' IDENTIFIED BY 'new_password';
SET PASSWORD FOR 'user'@'%' = 'new_password';
FLUSH PRIVILEGES;
```

### binlog 工具

```bash
# 查看 binlog 内容
goods_db_binlog goods_db_binlog.000001

# 从指定位置开始查看
goods_db_binlog --start-position=4096 goods_db_binlog.000001

# 详细模式
goods_db_binlog --verbose goods_db_binlog.000001
```

### 备份

```bash
# 数据目录备份
tar -czf goods_db_backup_$(date +%Y%m%d).tar.gz ./data/

# 导出 binlog 用于增量恢复
cp goods_db_binlog.* /backup/location/
```

---

## 故障排查

### 服务器无法启动

1. 检查端口是否被占用: `ss -tlnp | grep 3307`
2. 检查数据目录权限: `ls -la ./data/`
3. 查看错误日志: `tail -100 goods_db_error.log`

### 连接被拒绝

1. 确认服务器正在监听: `ss -tlnp | grep goods_db`
2. 检查防火墙规则: `sudo ufw status`
3. 检查 IP 是否被封锁（密码连续错误）

### 查询性能慢

1. 检查 Buffer Pool 命中率（需实现统计接口）
2. 确认表上是否有合适的索引
3. 使用 `SHOW BINLOG EVENTS` 检查写入负载

### 连接数过多

```sql
-- 查看当前连接数（通过管理端口或统计接口）
-- 增加最大连接数配置
max_connections = 500
```

### 事务阻塞

- 检查是否有长时间未提交的事务
- 设置 `max_txn_duration` 限制事务最大时间
- 检查死锁日志（`goods_db_error.log`）

---

## 参考

- [整体架构设计](../模板/实验整体计划.md)
- [操作手册](operations_guide.md)
- [网络协议规范](design/network_protocol.md)
- [安全管理设计](design/security_design.md)
- [GitHub 仓库](https://github.com/your-org/goods_db)
