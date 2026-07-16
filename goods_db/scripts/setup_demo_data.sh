#!/bin/bash
# =============================================================================
# goods_db 电商演示数据一键加载脚本
#
# 用法:
#   1. 先启动数据库服务器:  ./goods_db_server --port 3307
#   2. 再启动 Web 服务器:    ./goods_db_web
#   3. 运行本脚本:           ./scripts/setup_demo_data.sh
#
#   然后浏览器打开 http://localhost:8080 即可开始可视化演示
# =============================================================================

set -e

HOST="${GOODS_DB_HOST:-localhost}"
PORT="${GOODS_DB_WEB_PORT:-8080}"
API="http://${HOST}:${PORT}/api"

# 颜色输出
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RESET='\033[0m'

log_step()  { echo -e "${BLUE}[步骤 $1]${RESET} $2"; }
log_ok()    { echo -e "  ${GREEN}✅ $1${RESET}"; }
log_info()  { echo -e "  ${YELLOW}⏳ $1${RESET}"; }

# ------------------------------------------------------------------
# 发送 SQL 到 Web 服务器执行
# ------------------------------------------------------------------
exec_sql() {
    local sql="$1"
    local desc="$2"
    local result
    # 用 python3 构建 JSON 避免 shell 转义问题
    local body
    body=$(python3 -c "import json,sys; print(json.dumps({'sql': sys.argv[1]}, ensure_ascii=False))" "${sql}")
    result=$(curl -s -X POST "${API}/execute" \
        -H 'Content-Type: application/json' \
        -d "${body}" 2>&1)
    if echo "$result" | grep -q '"success":false'; then
        local err=$(echo "$result" | grep -o '"error":"[^"]*"' | head -1)
        echo -e "  ❌ ${desc}: ${err}"
        return 1
    fi
    if [ -n "$desc" ]; then
        log_ok "$desc"
    fi
}

# ------------------------------------------------------------------
# 等待 Web 服务器就绪
# ------------------------------------------------------------------
wait_for_server() {
    log_info "等待 Web 服务器就绪..."
    for i in $(seq 1 30); do
        if curl -s "${API}/databases" >/dev/null 2>&1; then
            log_ok "Web 服务器就绪 (${HOST}:${PORT})"
            return 0
        fi
        sleep 1
    done
    echo -e "  ❌ Web 服务器未在 ${HOST}:${PORT} 启动，请先运行 ./goods_db_web"
    exit 1
}

# ------------------------------------------------------------------
# 连接数据库
# ------------------------------------------------------------------
connect() {
    log_info "连接数据库服务器..."
    local result
    result=$(curl -s -X POST "${API}/connect" \
        -H 'Content-Type: application/json' \
        -d '{"host":"localhost","port":3307,"user":"root","password":"","db":""}')
    if echo "$result" | grep -q '"success":true'; then
        log_ok "已连接到 goods_db (localhost:3307)"
    else
        echo -e "  ❌ 连接失败，请先启动 ./goods_db_server --port 3307"
        exit 1
    fi
}

# =============================================================================
# 主流程
# =============================================================================

echo ""
echo "  ╔══════════════════════════════════════════════════╗"
echo "  ║  goods_db — 电商演示数据一键加载                 ║"
echo "  ║  7 张表 · 60+ 条数据 · 电商全链路闭环            ║"
echo "  ╚══════════════════════════════════════════════════╝"
echo ""

wait_for_server
connect

# ===== Step 1: 创建数据库 =====
log_step "1" "创建数据库 retail"
exec_sql "CREATE DATABASE retail" "数据库 retail 已创建"

# ===== Step 2: 创建 7 张表 =====
log_step "2" "创建 7 张业务表"

exec_sql \
"CREATE TABLE warehouses (
    id INT NOT NULL,
    name VARCHAR(100) NOT NULL,
    location VARCHAR(200) NOT NULL,
    capacity INT NOT NULL,
    status VARCHAR(20) DEFAULT 'active',
    PRIMARY KEY (id)
)" "warehouses 表已创建"

exec_sql \
"CREATE TABLE products (
    id INT NOT NULL,
    name VARCHAR(200) NOT NULL,
    sku VARCHAR(50) NOT NULL,
    category VARCHAR(50) NOT NULL,
    unit_price DECIMAL(10,2) NOT NULL,
    PRIMARY KEY (id)
)" "products 表已创建"

exec_sql \
"CREATE TABLE inventory (
    warehouse_id INT NOT NULL,
    product_id INT NOT NULL,
    quantity INT NOT NULL DEFAULT 0,
    shelf_location VARCHAR(50),
    last_updated TIMESTAMP,
    PRIMARY KEY (warehouse_id, product_id)
)" "inventory 表已创建"

exec_sql \
"CREATE TABLE customers (
    id INT NOT NULL,
    name VARCHAR(100) NOT NULL,
    address VARCHAR(300),
    phone VARCHAR(20),
    membership_level VARCHAR(20) DEFAULT 'normal',
    PRIMARY KEY (id)
)" "customers 表已创建"

exec_sql \
"CREATE TABLE orders (
    id INT NOT NULL,
    customer_id INT NOT NULL,
    order_date TIMESTAMP,
    status VARCHAR(20) DEFAULT 'pending',
    total_amount DECIMAL(12,2) NOT NULL,
    PRIMARY KEY (id)
)" "orders 表已创建"

exec_sql \
"CREATE TABLE order_items (
    order_id INT NOT NULL,
    product_id INT NOT NULL,
    quantity INT NOT NULL,
    unit_price DECIMAL(10,2) NOT NULL,
    PRIMARY KEY (order_id, product_id)
)" "order_items 表已创建"

exec_sql \
"CREATE TABLE shipments (
    id INT NOT NULL,
    order_id INT NOT NULL,
    warehouse_id INT NOT NULL,
    status VARCHAR(20) DEFAULT 'processing',
    tracking_no VARCHAR(50),
    ship_date TIMESTAMP,
    arrive_date TIMESTAMP,
    PRIMARY KEY (id)
)" "shipments 表已创建"

# ===== Step 3: 创建索引 =====
log_step "3" "创建索引（6 个）"
exec_sql "CREATE INDEX idx_inv_product ON inventory (product_id)" "idx_inv_product"
exec_sql "CREATE INDEX idx_inv_warehouse ON inventory (warehouse_id)" "idx_inv_warehouse"
exec_sql "CREATE INDEX idx_orders_customer ON orders (customer_id)" "idx_orders_customer"
exec_sql "CREATE INDEX idx_orders_date ON orders (order_date)" "idx_orders_date"
exec_sql "CREATE INDEX idx_ship_order ON shipments (order_id)" "idx_ship_order"
exec_sql "CREATE INDEX idx_ship_tracking ON shipments (tracking_no)" "idx_ship_tracking"

# ===== Step 4: 插入仓库（5 个） =====
log_step "4" "插入仓库数据（5 个）"
exec_sql \
"INSERT INTO warehouses (id, name, location, capacity) VALUES
(1, '华东物流中心', '上海市浦东新区外高桥保税区富特路288号', 80000),
(2, '华南配送中心', '广州市白云区太和镇民营科技园科兴路8号', 65000),
(3, '华北仓储基地', '北京市大兴区亦庄经济开发区荣华南路11号', 70000),
(4, '西南分拨中心', '成都市双流区航空港物流大道688号', 50000),
(5, '华中中转仓', '武汉市东西湖区走马岭物流园A区', 55000)" \
"5 个仓库已插入"

# ===== Step 5: 插入商品（15 个，覆盖 6 个品类） =====
log_step "5" "插入商品数据（15 个）"
exec_sql \
"INSERT INTO products (id, name, sku, category, unit_price) VALUES
(101, 'iPhone 15 Pro Max 256GB 原色钛金属', 'ELEC-001', '手机数码', 9999.00),
(102, '华为 Mate 60 Pro 512GB 雅丹黑', 'ELEC-002', '手机数码', 6999.00),
(103, '索尼 WH-1000XM5 无线降噪耳机', 'ELEC-003', '手机数码', 2499.00),
(104, 'Apple MacBook Air M3 15寸 16G+512G', 'COMP-001', '电脑办公', 10499.00),
(105, '联想 ThinkPad X1 Carbon Gen11', 'COMP-002', '电脑办公', 8999.00),
(106, '认养一头牛 纯牛奶 250ml×24盒', 'FOOD-001', '食品饮料', 69.90),
(107, '三只松鼠 坚果大礼包 1.5kg', 'FOOD-002', '食品饮料', 128.00),
(108, '农夫山泉 矿泉水 550ml×24瓶', 'FOOD-003', '食品饮料', 39.90),
(109, '蓝月亮 洗衣液 3kg×2瓶装', 'HOME-001', '家居厨具', 69.90),
(110, '维达 超韧抽纸 3层120抽×24包', 'HOME-002', '家居厨具', 59.90),
(111, '苏泊尔 不粘炒锅 32cm', 'HOME-003', '家居厨具', 199.00),
(112, '兰蔻 小黑瓶精华肌底液 30ml', 'BEAU-001', '个护美妆', 760.00),
(113, 'Nike Air Force 1 经典白 男款', 'FASH-001', '服饰鞋包', 799.00),
(114, 'Keep 瑜伽垫 加厚防滑 185×80cm', 'SPOR-001', '运动户外', 129.00),
(115, 'Nintendo Switch OLED 游戏机', 'BOOK-001', '图书文娱', 1999.00)" \
"15 种商品已插入"

# ===== Step 6: 插入库存（15 条） =====
log_step "6" "插入库存数据（15 条）"
exec_sql \
"INSERT INTO inventory (warehouse_id, product_id, quantity, shelf_location) VALUES
(1, 101, 350, 'A-01-01'),
(1, 102, 200, 'A-01-02'),
(1, 104, 120, 'A-02-01'),
(1, 106, 3000, 'B-01-01'),
(1, 108, 8000, 'B-01-02'),
(2, 103, 180, 'A-01-01'),
(2, 105, 90, 'A-02-01'),
(2, 107, 1500, 'B-01-01'),
(2, 109, 2000, 'C-01-01'),
(3, 110, 3500, 'C-01-01'),
(3, 111, 800, 'C-01-02'),
(3, 112, 400, 'B-01-01'),
(4, 113, 500, 'B-02-01'),
(4, 114, 600, 'D-01-01'),
(5, 115, 300, 'D-01-02')" \
"15 条库存已插入"

# ===== Step 7: 插入客户（6 位） =====
log_step "7" "插入客户数据（6 位）"
exec_sql \
"INSERT INTO customers (id, name, address, phone, membership_level) VALUES
(1001, '张伟明', '上海市徐汇区衡山路12号301室', '13816551001', 'gold'),
(1002, '李雪芳', '广州市天河区体育西路8号天汇大厦B座2205', '13902231002', 'silver'),
(1003, '王建国', '北京市朝阳区国贸大厦A座1506', '13501101003', 'gold'),
(1004, '赵丽娜', '成都市锦江区春熙路88号王府井百货C座901', '18628111004', 'silver'),
(1005, '孙志强', '武汉市洪山区光谷大道106号光谷国际广场D栋1203', '15927011005', 'gold'),
(1006, '陈美玲', '杭州市西湖区文三路478号华星时代广场A座701', '13757111006', 'diamond')" \
"6 位客户已插入"

# ===== Step 8: 插入订单（12 条） =====
log_step "8" "插入订单数据（12 条）"
exec_sql \
"INSERT INTO orders (id, customer_id, order_date, status, total_amount) VALUES
(2001, 1001, '2026-07-01 10:30:00', 'delivered', 12498.00),
(2002, 1002, '2026-07-03 14:20:00', 'shipped', 69.90),
(2003, 1003, '2026-07-05 09:15:00', 'processing', 8999.00),
(2004, 1001, '2026-07-08 16:45:00', 'delivered', 258.80),
(2005, 1004, '2026-07-10 11:00:00', 'shipped', 928.00),
(2006, 1005, '2026-07-11 08:30:00', 'pending', 199.00),
(2007, 1002, '2026-07-12 13:00:00', 'processing', 129.00),
(2008, 1003, '2026-07-13 20:15:00', 'delivered', 398.00),
(2009, 1006, '2026-07-14 09:00:00', 'pending', 2499.00),
(2010, 1001, '2026-07-15 11:30:00', 'pending', 128.00),
(2011, 1004, '2026-06-28 15:00:00', 'delivered', 69.90),
(2012, 1005, '2026-07-02 10:00:00', 'cancelled', 8999.00)" \
"12 条订单已插入"

# ===== Step 9: 插入订单明细（16 条） =====
log_step "9" "插入订单明细数据（15 条）"
exec_sql \
"INSERT INTO order_items (order_id, product_id, quantity, unit_price) VALUES
(2001, 101, 1, 9999.00),
(2001, 103, 1, 2499.00),
(2002, 109, 1, 69.90),
(2003, 105, 1, 8999.00),
(2004, 110, 1, 59.90),
(2004, 106, 1, 69.90),
(2004, 114, 1, 129.00),
(2005, 113, 1, 799.00),
(2005, 114, 1, 129.00),
(2006, 111, 1, 199.00),
(2007, 114, 1, 129.00),
(2008, 107, 2, 128.00),
(2008, 106, 1, 69.90),
(2009, 103, 1, 2499.00),
(2010, 107, 1, 128.00)" \
"15 条订单明细已插入"

# ===== Step 10: 插入物流（5 条） =====
log_step "10" "插入物流数据（5 条）"
exec_sql \
"INSERT INTO shipments (id, order_id, warehouse_id, status, tracking_no, ship_date, arrive_date) VALUES
(3001, 2001, 1, 'delivered', 'SF119876543210', '2026-07-01 15:00:00', '2026-07-02 18:30:00'),
(3002, 2002, 2, 'in_transit', 'YT119876543211', '2026-07-04 09:00:00', NULL),
(3003, 2004, 1, 'delivered', 'SF119876543212', '2026-07-09 10:00:00', '2026-07-10 14:00:00'),
(3004, 2005, 4, 'in_transit', 'DB119876543213', '2026-07-11 08:00:00', NULL),
(3005, 2011, 1, 'delivered', 'SF119876543214', '2026-06-29 09:00:00', '2026-06-30 11:00:00')" \
"5 条物流已插入"

# =============================================================================
# Step 11: 注册外键级联关系
# =============================================================================
log_step "11" "注册外键级联关系（7 对）"
exec_sql "REGISTER_FK warehouses.id inventory.warehouse_id CASCADE" "warehouses.id → inventory.warehouse_id"
exec_sql "REGISTER_FK warehouses.id shipments.warehouse_id CASCADE" "warehouses.id → shipments.warehouse_id"
exec_sql "REGISTER_FK products.id inventory.product_id CASCADE" "products.id → inventory.product_id"
exec_sql "REGISTER_FK products.id order_items.product_id CASCADE" "products.id → order_items.product_id"
exec_sql "REGISTER_FK customers.id orders.customer_id CASCADE" "customers.id → orders.customer_id"
exec_sql "REGISTER_FK orders.id order_items.order_id CASCADE" "orders.id → order_items.order_id"
exec_sql "REGISTER_FK orders.id shipments.order_id CASCADE" "orders.id → shipments.order_id"

# =============================================================================
# 完成
# =============================================================================
echo ""
echo -e "  ╔══════════════════════════════════════════════╗"
echo -e "  ║  ${GREEN}✅ 全部数据加载完成！${RESET}                       ║"
echo -e "  ║                                              ║"
echo -e "  ║  ${YELLOW}浏览器打开: http://localhost:8080${RESET}         ║"
echo -e "  ║  ${YELLOW}即可开始可视化演示${RESET}                         ║"
echo -e "  ╚══════════════════════════════════════════════╝"
echo ""
echo "  数据清单:"
echo "    📦 5 个仓库    📱 15 种商品    📊 15 条库存"
echo "    👤 6 位客户    📋 12 条订单    📝 15 条订单明细"
echo "    🚚 5 条物流    🔍 6 个索引    🔗 7 对外键"
echo ""
