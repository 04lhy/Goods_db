-- =============================================================================
-- goods_db 新零售物流电商测试数据集 DDL
-- 7 张表，目标数据量约 95 万行
-- =============================================================================

-- 仓库表（50 行）
CREATE TABLE warehouses (
    id INT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    location VARCHAR(200) NOT NULL,
    capacity INT NOT NULL,
    status VARCHAR(20) DEFAULT 'active'
);

-- 商品表（1000 行）
CREATE TABLE products (
    id INT PRIMARY KEY,
    name VARCHAR(200) NOT NULL,
    sku VARCHAR(50) UNIQUE NOT NULL,
    category VARCHAR(50) NOT NULL,
    unit_price DECIMAL(10,2) NOT NULL
);

-- 库存表（200K 行）
CREATE TABLE inventory (
    warehouse_id INT NOT NULL,
    product_id INT NOT NULL,
    quantity INT NOT NULL DEFAULT 0,
    shelf_location VARCHAR(50),
    last_updated TIMESTAMP DEFAULT 0,
    PRIMARY KEY (warehouse_id, product_id)
);

-- 客户表（50K 行）
CREATE TABLE customers (
    id INT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    address VARCHAR(300),
    phone VARCHAR(20),
    membership_level VARCHAR(20) DEFAULT 'normal'
);

-- 订单表（100K 行）
CREATE TABLE orders (
    id INT PRIMARY KEY,
    customer_id INT NOT NULL,
    order_date TIMESTAMP DEFAULT 0,
    status VARCHAR(20) DEFAULT 'pending',
    total_amount DECIMAL(12,2) NOT NULL
);

-- 订单明细表（500K 行）
CREATE TABLE order_items (
    order_id INT NOT NULL,
    product_id INT NOT NULL,
    quantity INT NOT NULL,
    unit_price DECIMAL(10,2) NOT NULL,
    PRIMARY KEY (order_id, product_id)
);

-- 物流表（100K 行）
CREATE TABLE shipments (
    id INT PRIMARY KEY,
    order_id INT NOT NULL,
    warehouse_id INT NOT NULL,
    status VARCHAR(20) DEFAULT 'processing',
    tracking_no VARCHAR(50) UNIQUE,
    ship_date TIMESTAMP DEFAULT 0,
    arrive_date TIMESTAMP DEFAULT 0
);

-- =============================================================================
-- 索引
-- =============================================================================

CREATE INDEX idx_inventory_product ON inventory(product_id);
CREATE INDEX idx_inventory_warehouse ON inventory(warehouse_id);
CREATE INDEX idx_orders_customer ON orders(customer_id);
CREATE INDEX idx_orders_date ON orders(order_date);
CREATE INDEX idx_order_items_product ON order_items(product_id);
CREATE INDEX idx_shipments_order ON shipments(order_id);
CREATE INDEX idx_shipments_tracking ON shipments(tracking_no);
