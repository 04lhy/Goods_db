# goods_db Web 前端 — 电商数据库可视化演示指南

> 演示用：全程可视化点击操作，几乎不需要写 SQL。适合课堂展示、项目答辩。

---

## 架构速览

```
浏览器 (localhost:8080)  ←→  goods_db_web (cpp-httplib)  ←→  goods_db_server (port 3307)
      HTTP/WebSocket             8 MB JSON API              自定义 MySQL-like 协议
```

| 组件 | 端口 | 启动命令 |
|------|------|----------|
| 数据库服务器 | **3307** | `./goods_db_server --port 3307` |
| Web 前端 | **8080** | `./goods_db_web` |

---

## 目录

1. [快速启动（一次性）](#1-快速启动一次性)
2. [演示总览（12 分钟）](#2-演示总览12-分钟)
3. [阶段一：连接与浏览（3 分钟）](#3-阶段一连接与浏览3-分钟)
4. [阶段二：可视化数据浏览（2 分钟）](#4-阶段二可视化数据浏览2-分钟)
5. [阶段三：可视化增删改（3 分钟）](#5-阶段三可视化增删改3-分钟)
6. [阶段四：SQL 业务查询（3 分钟）](#6-阶段四sql-业务查询3-分钟)
7. [阶段五：数据导出（1 分钟）](#7-阶段五数据导出1-分钟)
8. [附录：电商数据模型 ER 图](#8-附录电商数据模型-er-图)
9. [故障排查](#9-故障排查)

---

## 1. 快速启动（一次性）

### 1.1 编译（已编译过可跳过）

```bash
cd goods_db/build
cmake .. -DCMAKE_BUILD_TYPE=Debug && cmake --build . -j$(nproc)

cd goods_db_studio/build
cmake .. -DCMAKE_BUILD_TYPE=Debug && cmake --build . -j$(nproc)
```

### 1.2 启动服务（需要 2 个终端）

> **注意**：必须先编译！如果修改过代码，执行：
> ```bash
> cd goods_db/build && cmake --build . -j$(nproc)
> cd ../../goods_db_studio/build && cmake --build . -j$(nproc)
> cd ../..
> ```

**终端 1** — 数据库服务器（先清旧数据避免残留损坏）：

```bash
cd goods_db/build
rm -rf ./data
./src/sql/goods_db_server --port 3307
# 看到 "Server listening on 0.0.0.0:3307" 即就绪
```

**终端 2** — Web 服务器：

```bash
cd goods_db_studio/build
./goods_db_web
# 看到 "goods_db_studio web server starting on http://0.0.0.0:8080" 即就绪
```

> **端口被占用**：
> ```bash
> pkill -9 -f goods_db_server; pkill -9 -f goods_db_web
> # 如果 pkill 无效，用 ss -tlnp | grep <端口号> 查找 PID 后手动 kill
> ```

### 1.3 加载演示数据（终端 3）

```bash
cd 项目根目录
chmod +x goods_db/scripts/setup_demo_data.sh
./goods_db/scripts/setup_demo_data.sh
```

依赖 `curl`（未安装则 `sudo apt install -y curl`）。

**预期输出（10 步全绿）：**

```
╔══════════════════════════════════════════════╗
║  ✅ 全部数据加载完成！                       ║
║  浏览器打开: http://localhost:8080           ║
╚══════════════════════════════════════════════╝
  数据清单:
    📦 5 个仓库    📱 15 种商品    📊 15 条库存
    👤 6 位客户    📋 12 条订单    📝 15 条订单明细
    🚚 5 条物流    🔍 6 个索引
```

### 1.4 打开浏览器

```
http://localhost:8080
```

---

## 2. 演示总览（12 分钟）

| 阶段 | 内容 | 操作方式 | 用时 |
|------|------|----------|------|
| 一 | 连接 + 对象树浏览 | 纯点击 | 3 min |
| 二 | 数据浏览：分页/排序/筛选/表结构 | 纯点击 | 2 min |
| 三 | 增删改：新增→编辑→删除一条记录 | 表单操作 | 3 min |
| 四 | 业务查询：JOIN/聚合/物流追踪 | SQL（可提前备好） | 3 min |
| 五 | 数据导出 CSV/JSON/SQL INSERT | 纯点击 | 1 min |

---

## 3. 阶段一：连接与浏览（3 分钟）

> **全程鼠标操作，不需要键盘**

### 3.1 连接数据库

1. 连接栏中填写：
   - Host：`localhost`
   - Port：`3307`
   - User：`root`
   - Pass：留空

2. 点击 **连接** → 状态栏变绿：`● 已连接: localhost:3307`

3. 左侧 **数据库** 面板自动加载，展开 `retail` 数据库 → 看到 7 张表和 6 个索引

```
 数据库
 ├─  goods_db        ← 系统默认库
 └─  retail           ← 电商数据库
     ├─  customers     (6 行)
     ├─  inventory     (15 行)
     ├─  order_items   (15 行)
     ├─  orders        (12 行)
     ├─  products      (15 行)
     ├─  shipments     (5 行)
     ├─  warehouses    (5 行)
     └─  idx_*         (6 个索引)
```

### 3.2 演示要点

- 数据库/表层次一目了然，和 MySQL Workbench 左侧面板一样
- 折叠/展开树节点演示异步加载
- 点击刷新按钮重新加载列表

---

## 4. 阶段二：可视化数据浏览（2 分钟）

> **点击表名 → 数据自动加载，全程不需要输入任何 SQL**

### 4.1 浏览表数据（最核心的可视化操作）

1. 点击左侧 `warehouses` 表 → 右侧立即显示 5 条仓库数据
2. 再点击 `products` → 切换到 10 条商品数据
3. 再点击 `orders` → 8 条订单数据

**展示要点**：
- 表头自动显示列名和表名
- 每行有 Checkbox 可用于批量选择
- 每行右侧有编辑和删除按钮

### 4.2 排序

点击任意列头 → 当前页数据按该列排序，再次点击切换升序/降序（▲/▼ 箭头指示）

> 注意：排序为**客户端排序**（对当前已加载数据排序），可配合筛选使用。数值列按数值比较，字符串列按字典序比较。

> 演示：点击 `customers` 表 → 点击 `membership_level` 列头 → 按会员等级排序（diamond → gold → silver → normal）

### 4.3 筛选

1. 点击 **筛选** 按钮 → 展开筛选面板（每个列对应一个输入框）
2. 在目标列的输入框中输入筛选值 → 点击 **应用筛选**（或按 Enter）
3. 表格即时过滤出匹配的行

> **筛选逻辑**：字符串列使用模糊匹配（`LIKE '%keyword%'`），数值列使用精确匹配（`= value`）。点击 **清除** 重置所有筛选条件。

> 演示：在 `inventory` 表的 `quantity` 列输入 `500` → 只显示库存恰好为 500 的记录。在 `products` 表的 `name` 列输入 `iPhone` → 模糊匹配出包含 iPhone 的商品。

### 4.4 查看表结构

点击 **表结构** 按钮 → 弹出列名、类型、长度的模态窗口

> 演示：查看 `orders` 表结构 → 清晰的列信息展示

### 4.5 分页浏览

数据行数超过 100 行时（大数据量场景），底部分页控件自动出现：
- 首页 / 上一页 / 下一页 / 跳转

---

## 5. 阶段三：可视化增删改（3 分钟）

> **纯表单操作，不需要写 INSERT / UPDATE / DELETE 语句**

### 5.1 新增一条记录 ⭐ 重点演示

1. 点击左侧 `customers` 表
2. 点击 **＋ 新增** 按钮 → 弹出表单模态窗口
3. 填写：

   ```
   id:               1007
   name:             周明远
   address:          长沙市岳麓区麓山南路932号
   phone:            13873101007
   membership_level: gold
   ```

4. 点击 **保存** → 表格自动刷新，新记录出现在列表中

**展示要点**：
- 表单自动识别列类型（数值列显示数字输入框，字符串列显示文本输入框）
- 类型提示（VARCHAR(100)、INT 等）在每个输入框上方
- 保存后自动刷新，无需手动刷新页面

### 5.2 编辑一条记录

1. 在 `customers` 表中找到"周明远"
2. 点击行右侧 **编辑** 按钮 → 弹出编辑表单
3. 把 `membership_level` 从 `gold` 改为 `platinum`
4. 点击 **保存** → 表格自动刷新

**展示要点**：
- 编辑表单自动回填当前值
- 主键（id）字段灰色不可修改
- 修改后自动刷新，效果立即可见

### 5.3 删除一条记录

1. 在 `customers` 表中勾选"周明远"所在行（最左侧 Checkbox）
2. 点击 **删除选中**（按钮上显示已选数量）→ 弹出确认窗口
3. 点击 **确认删除** → 后端执行 DELETE → 表格自动刷新，记录消失

**展示要点**：
- 自动识别主键类型（INT/VARCHAR），生成正确的 DELETE 语句
- 支持批量勾选删除（多行 Checkbox + 全选 Checkbox）
- 确认弹窗显示主键值，防止误操作
- 删除按钮在未选中时为禁用态（灰色不可点击）
- 删除后自动刷新表格，状态栏显示受影响的记录数

### 5.4 级联删除演示 ⭐ v1.2.2 新增

> **演示外键级联：删父表自动删子表，确认对话框展示级联影响范围**

1. 点击左侧 `warehouses` 表
2. 找到第一行 → 点击 🗑 删除按钮
3. 确认对话框展示：
   ```
   确定要删除记录 id=1 吗？

   ⚠️ 此操作将级联删除以下表中的关联数据：
     • inventory
     • shipments
   ```
4. 点击确认 → `warehouses` 少 1 行，切换到 `inventory` 和 `shipments` 验证关联数据也已删除

**延伸演示**：删除 `customers` 表中的客户 → 级联删除 `orders` → 递归级联删除 `order_items` 和 `shipments`（三级链式级联）

### 5.5 完整 CRUD 闭环效果

```
新增(表单) → 浏览(表格) → 编辑(表单) → 浏览(验证) → 删除(确认) → 浏览(验证)
```

> **这 3 分钟是演示的核心亮点 —— 不需要写一行 SQL 完成增删改**

---

## 6. 阶段四：SQL 业务查询（3 分钟）

> **切换到 SQL 标签，执行预准备好的查询。可提前粘贴到不同标签中**

> **技巧**：演示前在 3 个 SQL 标签中提前粘贴好查询 SQL → 演示时只点执行

### 6.1 多标签准备

点击 **＋** 新建 2 个 SQL 标签，分别粘贴不同查询。演示时只需切换标签 → 点执行。

### 6.2 查询 1：库存实时查询

> 场景：查 iPhone 15 Pro Max 在华东物流中心的库存

```sql
SELECT p.name, i.quantity, i.shelf_location, w.name AS warehouse
FROM retail.inventory i
JOIN retail.warehouses w ON i.warehouse_id = w.id
JOIN retail.products p ON i.product_id = p.id
WHERE i.warehouse_id = 1 AND i.product_id = 101;
```

### 6.3 查询 2：客户消费排行

> 场景：各客户累计消费金额排名

```sql
SELECT c.name, c.membership_level,
       SUM(oi.quantity * oi.unit_price) AS total_spent
FROM retail.customers c
JOIN retail.orders o ON c.id = o.customer_id
JOIN retail.order_items oi ON o.id = oi.order_id
GROUP BY c.id, c.name, c.membership_level
ORDER BY total_spent DESC;
```

### 6.4 查询 3：订单物流全链路追踪 ⭐ 亮点

> 场景：订单→客户→商品→物流→仓库 五表 JOIN

```sql
SELECT o.id AS order_id, c.name AS customer,
       p.name AS product, oi.quantity,
       s.tracking_no, s.status AS ship_status,
       w.name AS warehouse
FROM retail.orders o
JOIN retail.customers c ON o.customer_id = c.id
JOIN retail.order_items oi ON o.id = oi.order_id
JOIN retail.products p ON oi.product_id = p.id
LEFT JOIN retail.shipments s ON o.id = s.order_id
LEFT JOIN retail.warehouses w ON s.warehouse_id = w.id
ORDER BY o.order_date DESC;
```

---

## 7. 阶段五：数据导出（1 分钟）

> **演示数据可导出为 CSV 用 Excel 打开，体现"真实可用"**

1. 点击 `orders` 表，数据已加载
2. 点击 **导出** → 下拉菜单出现：
   - 导出 CSV
   - 导出 JSON
   - 导出 SQL INSERT
3. 选择 **导出 CSV** → 浏览器下载 `orders.csv` → 用 Excel / WPS 打开展示

**展示要点**：导出的 CSV 可以直接给数据分析师用，体现系统的实用性。

---

## 8. 附录

### 8.1 电商数据模型 ER 图

```
  ┌──────────┐     ┌──────────┐     ┌──────────────┐
  │ customers │────→│  orders  │────→│ order_items  │←────│ products  │
  └──────────┘     └──────────┘     └──────────────┘     └───────────┘
                         │                                      ↑
                         │  ┌────────────┐                      │
                         └─→│ shipments  │←──│ warehouses │     │
                            └────────────┘   └───────────┘     │
                                                               │
                            ┌────────────┐                     │
                            │ inventory  │←──│ warehouses │    │
                            └────────────┘   │ products  │────┘
                                             └───────────┘
```

### 8.2 手动执行 SQL（脚本失败时的备选方案）

<details>
<summary>点击展开完整 SQL（建表 + 数据，逐条复制执行）</summary>

```sql
-- 1. 创建数据库
CREATE DATABASE retail;

-- 2. 建表（7 张）
CREATE TABLE warehouses (id INT NOT NULL, name VARCHAR(100) NOT NULL, location VARCHAR(200) NOT NULL, capacity INT NOT NULL, status VARCHAR(20) DEFAULT 'active', PRIMARY KEY (id));
CREATE TABLE products (id INT NOT NULL, name VARCHAR(200) NOT NULL, sku VARCHAR(50) NOT NULL, category VARCHAR(50) NOT NULL, unit_price DECIMAL(10,2) NOT NULL, PRIMARY KEY (id));
CREATE TABLE inventory (warehouse_id INT NOT NULL, product_id INT NOT NULL, quantity INT NOT NULL DEFAULT 0, shelf_location VARCHAR(50), last_updated TIMESTAMP, PRIMARY KEY (warehouse_id, product_id));
CREATE TABLE customers (id INT NOT NULL, name VARCHAR(100) NOT NULL, address VARCHAR(300), phone VARCHAR(20), membership_level VARCHAR(20) DEFAULT 'normal', PRIMARY KEY (id));
CREATE TABLE orders (id INT NOT NULL, customer_id INT NOT NULL, order_date TIMESTAMP, status VARCHAR(20) DEFAULT 'pending', total_amount DECIMAL(12,2) NOT NULL, PRIMARY KEY (id));
CREATE TABLE order_items (order_id INT NOT NULL, product_id INT NOT NULL, quantity INT NOT NULL, unit_price DECIMAL(10,2) NOT NULL, PRIMARY KEY (order_id, product_id));
CREATE TABLE shipments (id INT NOT NULL, order_id INT NOT NULL, warehouse_id INT NOT NULL, status VARCHAR(20) DEFAULT 'processing', tracking_no VARCHAR(50), ship_date TIMESTAMP, arrive_date TIMESTAMP, PRIMARY KEY (id));

-- 3. 建索引（6 个）
CREATE INDEX idx_inv_product ON inventory (product_id);
CREATE INDEX idx_inv_warehouse ON inventory (warehouse_id);
CREATE INDEX idx_orders_customer ON orders (customer_id);
CREATE INDEX idx_orders_date ON orders (order_date);
CREATE INDEX idx_ship_order ON shipments (order_id);
CREATE INDEX idx_ship_tracking ON shipments (tracking_no);

-- 4. 插入数据
INSERT INTO warehouses (id, name, location, capacity) VALUES
(1,'华东物流中心','上海市浦东新区外高桥保税区富特路288号',80000),
(2,'华南配送中心','广州市白云区太和镇民营科技园科兴路8号',65000),
(3,'华北仓储基地','北京市大兴区亦庄经济开发区荣华南路11号',70000),
(4,'西南分拨中心','成都市双流区航空港物流大道688号',50000),
(5,'华中中转仓','武汉市东西湖区走马岭物流园A区',55000);

INSERT INTO products (id, name, sku, category, unit_price) VALUES
(101,'iPhone 15 Pro Max 256GB 原色钛金属','ELEC-001','手机数码',9999.00),
(102,'华为 Mate 60 Pro 512GB 雅丹黑','ELEC-002','手机数码',6999.00),
(103,'索尼 WH-1000XM5 无线降噪耳机','ELEC-003','手机数码',2499.00),
(104,'Apple MacBook Air M3 15寸 16G+512G','COMP-001','电脑办公',10499.00),
(105,'联想 ThinkPad X1 Carbon Gen11','COMP-002','电脑办公',8999.00),
(106,'认养一头牛 纯牛奶 250ml×24盒','FOOD-001','食品饮料',69.90),
(107,'三只松鼠 坚果大礼包 1.5kg','FOOD-002','食品饮料',128.00),
(108,'农夫山泉 矿泉水 550ml×24瓶','FOOD-003','食品饮料',39.90),
(109,'蓝月亮 洗衣液 3kg×2瓶装','HOME-001','家居厨具',69.90),
(110,'维达 超韧抽纸 3层120抽×24包','HOME-002','家居厨具',59.90),
(111,'苏泊尔 不粘炒锅 32cm','HOME-003','家居厨具',199.00),
(112,'兰蔻 小黑瓶精华肌底液 30ml','BEAU-001','个护美妆',760.00),
(113,'Nike Air Force 1 经典白 男款','FASH-001','服饰鞋包',799.00),
(114,'Keep 瑜伽垫 加厚防滑 185×80cm','SPOR-001','运动户外',129.00),
(115,'Nintendo Switch OLED 游戏机','BOOK-001','图书文娱',1999.00);

INSERT INTO inventory (warehouse_id, product_id, quantity, shelf_location) VALUES
(1,101,350,'A-01-01'),(1,102,200,'A-01-02'),(1,104,120,'A-02-01'),
(1,106,3000,'B-01-01'),(1,108,8000,'B-01-02'),(2,103,180,'A-01-01'),
(2,105,90,'A-02-01'),(2,107,1500,'B-01-01'),(2,109,2000,'C-01-01'),
(3,110,3500,'C-01-01'),(3,111,800,'C-01-02'),(3,112,400,'B-01-01'),
(4,113,500,'B-02-01'),(4,114,600,'D-01-01'),(5,115,300,'D-01-02');

INSERT INTO customers (id, name, address, phone, membership_level) VALUES
(1001,'张伟明','上海市徐汇区衡山路12号301室','13816551001','gold'),
(1002,'李雪芳','广州市天河区体育西路8号天汇大厦B座2205','13902231002','silver'),
(1003,'王建国','北京市朝阳区国贸大厦A座1506','13501101003','gold'),
(1004,'赵丽娜','成都市锦江区春熙路88号王府井百货C座901','18628111004','silver'),
(1005,'孙志强','武汉市洪山区光谷大道106号光谷国际广场D栋1203','15927011005','gold'),
(1006,'陈美玲','杭州市西湖区文三路478号华星时代广场A座701','13757111006','diamond');

INSERT INTO orders (id, customer_id, order_date, status, total_amount) VALUES
(2001,1001,'2026-07-01 10:30:00','delivered',12498.00),
(2002,1002,'2026-07-03 14:20:00','shipped',69.90),
(2003,1003,'2026-07-05 09:15:00','processing',8999.00),
(2004,1001,'2026-07-08 16:45:00','delivered',258.80),
(2005,1004,'2026-07-10 11:00:00','shipped',928.00),
(2006,1005,'2026-07-11 08:30:00','pending',199.00),
(2007,1002,'2026-07-12 13:00:00','processing',129.00),
(2008,1003,'2026-07-13 20:15:00','delivered',398.00),
(2009,1006,'2026-07-14 09:00:00','pending',2499.00),
(2010,1001,'2026-07-15 11:30:00','pending',128.00),
(2011,1004,'2026-06-28 15:00:00','delivered',69.90),
(2012,1005,'2026-07-02 10:00:00','cancelled',8999.00);

INSERT INTO order_items (order_id, product_id, quantity, unit_price) VALUES
(2001,101,1,9999.00),(2001,103,1,2499.00),
(2002,109,1,69.90),(2003,105,1,8999.00),
(2004,110,1,59.90),(2004,106,1,69.90),(2004,114,1,129.90),
(2005,113,1,799.00),(2005,114,1,129.00),
(2006,111,1,199.00),(2007,114,1,129.00),
(2008,107,2,128.00),(2008,106,1,69.90),
(2009,103,1,2499.00),(2010,107,1,128.00);

INSERT INTO shipments (id, order_id, warehouse_id, status, tracking_no, ship_date, arrive_date) VALUES
(3001,2001,1,'delivered','SF119876543210','2026-07-01 15:00:00','2026-07-02 18:30:00'),
(3002,2002,2,'in_transit','YT119876543211','2026-07-04 09:00:00',NULL),
(3003,2004,1,'delivered','SF119876543212','2026-07-09 10:00:00','2026-07-10 14:00:00'),
(3004,2005,4,'in_transit','DB119876543213','2026-07-11 08:00:00',NULL),
(3005,2011,1,'delivered','SF119876543214','2026-06-29 09:00:00','2026-06-30 11:00:00');
```

</details>

---

## 9. 故障排查

| 问题 | 可能原因 | 解决方法 |
|------|----------|----------|
| 网页打不开 `localhost:8080` | Web 服务未启动或端口被占用 | `ss -tlnp \| grep 8080` 检查，有旧进程则 `kill <PID>` 后重启 |
| 连接报错 | 数据库服务未启动 | `cd goods_db/build && rm -rf ./data && ./src/sql/goods_db_server --port 3307` |
| 脚本报 "Not connected to server" | Web 服务器是旧二进制（未重编译） | `kill` 旧 Web 进程 → 重编译 → 重启 |
| 所有 INT 值显示为 0 | 数据库引擎类型转换 bug（v1.2.0 已修复） | 重新编译 `goods_db/build` + 重启数据库 |
| 中文数据乱码 | Python `json.dumps` 转义 + C++ 未解码（v1.2.0 已修复） | 重新编译 `goods_db_studio/build` + 重启 Web |
| 筛选不生效 | 所有列用 LIKE 模糊匹配（v1.2.0 已修复） | 重新编译 Web 前端 |
| 新增记录 id=0 | INSERT 缺主键 + DB 默认填 0（v1.2.0 已修复） | 重新编译 DB 引擎 |
| 脚本报 "curl: command not found" | 未安装 curl | `sudo apt install -y curl` |
| 对象树不显示 `retail` 库 | 未执行数据加载脚本 | 运行 `./goods_db/scripts/setup_demo_data.sh` |
| 表格中文显示参差不齐 | 表字体无中文字符（v1.2.0 已修复） | 已改为含中文的 `var(--font)` |

> **终极方案**：如果以上都无效，按这个顺序重来：
> ```bash
> cd /home/lhy/桌面/Goods_db
> pkill -9 -f goods_db_server; pkill -9 -f goods_db_web; sleep 1
> cd goods_db/build && cmake --build . -j$(nproc) && cd ../..
> cd goods_db_studio/build && cmake --build . -j$(nproc) && cd ../..
> cd goods_db/build && rm -rf ./data && ./src/sql/goods_db_server --port 3307 &
> sleep 3
> cd ../../goods_db_studio/build && ./goods_db_web &
> sleep 2
> cd ../.. && ./goods_db/scripts/setup_demo_data.sh
> ```
