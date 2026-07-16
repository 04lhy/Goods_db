/**
 * generate_test_data — 新零售物流电商测试数据生成器
 *
 * 输出两个 SQL 文件：
 *   - new_retail_schema.sql (DDL)
 *   - new_retail_data.sql   (INSERT)
 *
 * 用法：
 *   generate_test_data [options]
 *
 * 选项：
 *   --output-dir PATH     输出目录 (默认: .)
 *   --warehouses N        仓库数量 (默认: 50)
 *   --products N          商品数量 (默认: 1000)
 *   --customers N         客户数量 (默认: 50000)
 *   --orders N            订单数量 (默认: 100000)
 *   --seed N              随机种子 (默认: 42)
 *   --help                显示帮助
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// Simple pseudo-random generator with fixed seed for reproducibility
class XorShift64 {
 public:
  explicit XorShift64(uint64_t seed = 42) : state_(seed) {}
  uint64_t Next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 7;
    state_ ^= state_ << 17;
    return state_;
  }
  uint64_t NextInRange(uint64_t min, uint64_t max) {
    return min + (Next() % (max - min + 1));
  }
  double NextDouble() {
    return static_cast<double>(Next()) / static_cast<double>(UINT64_MAX);
  }

 private:
  uint64_t state_;
};

// ---- Helpers ---------------------------------------------------------------

static std::string Quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "''";
    else out += c;
  }
  out += "'";
  return out;
}

// Category list
static const char* kCategories[] = {
    "食品饮料", "日用百货", "数码家电", "服装鞋帽", "美妆个护",
    "母婴用品", "运动户外", "图书文娱", "医药健康", "家居家装"
};
static const int kNumCategories = 10;

// Membership levels
static const char* kMemberships[] = {"normal", "silver", "gold", "platinum"};
static const int kNumMemberships = 4;

// Order statuses
static const char* kOrderStatus[] = {"pending", "confirmed", "shipped", "delivered", "cancelled"};
static const int kNumOrderStatus = 5;

// Shipment statuses
static const char* kShipStatus[] = {"processing", "in_transit", "delivered", "returned"};
static const int kNumShipStatus = 4;

// Warehouse names
static const char* kCities[] = {
    "北京", "上海", "广州", "深圳", "杭州", "成都", "武汉", "南京",
    "重庆", "西安", "郑州", "长沙", "青岛", "天津", "苏州", "厦门",
    "合肥", "福州", "昆明", "南宁", "贵阳", "兰州", "太原", "沈阳",
    "长春", "哈尔滨", "乌鲁木齐", "拉萨", "银川", "西宁", "海口",
    "大连", "无锡", "宁波", "温州", "东莞", "佛山", "珠海", "泉州",
    "烟台", "济南", "南昌", "石家庄", "呼和浩特", "桂林", "三亚",
    "秦皇岛", "威海", "徐州", "扬州"
};

// ---- Data generation -------------------------------------------------------

struct Generator {
  XorShift64 rng;
  std::ofstream out;
  int batch_size = 100;

  explicit Generator(uint64_t seed) : rng(seed) {}

  std::string Timestamp(int days_ago) {
    time_t now = time(nullptr);
    time_t ts = now - days_ago * 86400 + rng.NextInRange(0, 86400);
    return std::to_string(ts);
  }

  void WriteBatchStart(const std::string& table, const std::string& cols) {
    out << "INSERT INTO " << table << " " << cols << " VALUES\n";
  }

  void WriteBatchEnd(bool last) {
    out << ";\n\n";
  }
};

void GenerateData(int num_warehouses, int num_products, int num_customers,
                  int num_orders, uint64_t seed, const std::string& output_dir) {
  Generator g(seed);

  // Open output file
  std::string data_path = output_dir + "/new_retail_data.sql";
  g.out.open(data_path);
  if (!g.out.is_open()) {
    std::cerr << "ERROR: Cannot open output file: " << data_path << std::endl;
    exit(1);
  }

  std::cout << "Generating test data..." << std::endl;
  std::cout << "  Warehouses: " << num_warehouses << std::endl;
  std::cout << "  Products:   " << num_products << std::endl;
  std::cout << "  Customers:  " << num_customers << std::endl;
  std::cout << "  Orders:     " << num_orders << std::endl;

  // Pre-compute: num_inventory = num_warehouses * num_products (~200K for 50*1000)
  int num_inventory = num_warehouses * num_products;
  // num_order_items: ~5 per order on average
  int num_order_items = num_orders * 5;
  // num_shipments: 1 per order
  int num_shipments = num_orders;

  // =========================================================================
  // 1. Warehouses
  // =========================================================================
  std::cout << "  [1/7] Generating warehouses..." << std::endl;
  int count = 0;
  for (int i = 1; i <= num_warehouses; i++) {
    if (count % g.batch_size == 0) {
      if (count > 0) g.out << ";\n\n";
      g.out << "INSERT INTO warehouses (id, name, location, capacity, status) VALUES\n";
    }
    if (count % g.batch_size > 0) g.out << ",\n";

    int city_idx = g.rng.NextInRange(0, 49);
    std::string name = std::string(kCities[city_idx]) + "仓库" +
                       std::to_string(g.rng.NextInRange(1, 9)) + "号";
    std::string location = std::string(kCities[city_idx]) + "市" +
                           std::string(g.rng.NextInRange(0, 1) ? "东" : "西") + "区物流园";
    int capacity = static_cast<int>(g.rng.NextInRange(5000, 50000));

    g.out << "  (" << i << ", " << Quote(name) << ", " << Quote(location)
          << ", " << capacity << ", 'active')";
    count++;
  }
  if (count % g.batch_size != 0) g.out << ";\n\n";

  // =========================================================================
  // 2. Products
  // =========================================================================
  std::cout << "  [2/7] Generating products..." << std::endl;
  count = 0;
  for (int i = 1; i <= num_products; i++) {
    if (count % g.batch_size == 0) {
      if (count > 0) g.out << ";\n\n";
      g.out << "INSERT INTO products (id, name, sku, category, unit_price) VALUES\n";
    }
    if (count % g.batch_size > 0) g.out << ",\n";

    int cat_idx = g.rng.NextInRange(0, kNumCategories - 1);
    std::string cat = kCategories[cat_idx];
    std::string sku = "SKU-" + std::string(8, '0') + std::to_string(i);
    sku = "SKU-" + sku.substr(sku.size() - 8);
    std::string name = cat + std::to_string(g.rng.NextInRange(100, 9999));
    double price = std::round(g.rng.NextInRange(10, 99990) / 100.0 * 100) / 100;

    g.out << "  (" << i << ", " << Quote(name) << ", " << Quote(sku)
          << ", " << Quote(cat) << ", " << std::fixed << std::setprecision(2)
          << price << ")";
    count++;
  }
  if (count % g.batch_size != 0) g.out << ";\n\n";

  // =========================================================================
  // 3. Inventory
  // =========================================================================
  std::cout << "  [3/7] Generating inventory..." << std::endl;
  count = 0;
  int total_printed = 0;
  for (int w = 1; w <= num_warehouses && total_printed < num_inventory; w++) {
    for (int p = 1; p <= num_products && total_printed < num_inventory; p++) {
      if (count % g.batch_size == 0) {
        if (count > 0) g.out << ";\n\n";
        g.out << "INSERT INTO inventory (warehouse_id, product_id, quantity, "
                 "shelf_location, last_updated) VALUES\n";
      }
      if (count % g.batch_size > 0) g.out << ",\n";

      int qty = static_cast<int>(g.rng.NextInRange(0, 9999));
      std::string shelf = std::string(1, 'A' + g.rng.NextInRange(0, 25)) +
                          "-" + std::to_string(g.rng.NextInRange(1, 99));
      std::string ts = g.Timestamp(g.rng.NextInRange(0, 90));

      g.out << "  (" << w << ", " << p << ", " << qty << ", "
            << Quote(shelf) << ", " << ts << ")";
      count++;
      total_printed++;
    }
  }
  if (count % g.batch_size != 0) g.out << ";\n\n";

  // =========================================================================
  // 4. Customers
  // =========================================================================
  std::cout << "  [4/7] Generating customers..." << std::endl;
  count = 0;
  for (int i = 1; i <= num_customers; i++) {
    if (count % g.batch_size == 0) {
      if (count > 0) g.out << ";\n\n";
      g.out << "INSERT INTO customers (id, name, address, phone, membership_level) "
               "VALUES\n";
    }
    if (count % g.batch_size > 0) g.out << ",\n";

    int city_idx = g.rng.NextInRange(0, 49);
    std::string name = "客户" + std::to_string(i);
    std::string addr = std::string(kCities[city_idx]) + "市" +
                       std::string(1, 'A' + g.rng.NextInRange(0, 25)) + "区" +
                       std::to_string(g.rng.NextInRange(1, 999)) + "号";
    std::string phone = "1" + std::to_string(g.rng.NextInRange(30, 99)) +
                        std::to_string(g.rng.NextInRange(10000000, 99999999));
    int mem_idx = g.rng.NextInRange(0, 100);
    const char* member;
    if (mem_idx < 70) member = "normal";
    else if (mem_idx < 90) member = "silver";
    else if (mem_idx < 97) member = "gold";
    else member = "platinum";

    g.out << "  (" << i << ", " << Quote(name) << ", " << Quote(addr)
          << ", " << Quote(phone) << ", '" << member << "')";
    count++;
  }
  if (count % g.batch_size != 0) g.out << ";\n\n";

  // =========================================================================
  // 5. Orders
  // =========================================================================
  std::cout << "  [5/7] Generating orders..." << std::endl;
  count = 0;
  std::vector<double> order_amounts;
  for (int i = 1; i <= num_orders; i++) {
    if (count % g.batch_size == 0) {
      if (count > 0) g.out << ";\n\n";
      g.out << "INSERT INTO orders (id, customer_id, order_date, status, "
               "total_amount) VALUES\n";
    }
    if (count % g.batch_size > 0) g.out << ",\n";

    int cust_id = static_cast<int>(g.rng.NextInRange(1, num_customers));
    std::string ts = g.Timestamp(g.rng.NextInRange(0, 365));
    int st_idx = g.rng.NextInRange(0, kNumOrderStatus - 1);
    double amount = 0;  // Will be computed from order_items

    g.out << "  (" << i << ", " << cust_id << ", " << ts << ", '"
          << kOrderStatus[st_idx] << "', " << amount << ")";
    order_amounts.push_back(amount);
    count++;
  }
  if (count % g.batch_size != 0) g.out << ";\n\n";

  // =========================================================================
  // 6. Order Items (~5 per order)
  // =========================================================================
  std::cout << "  [6/7] Generating order items..." << std::endl;
  count = 0;
  int oi_id = 0;
  for (int o = 0; o < num_orders; o++) {
    int items_per_order = static_cast<int>(g.rng.NextInRange(1, 8));
    double order_total = 0.0;

    for (int j = 0; j < items_per_order && oi_id < num_order_items; j++) {
      if (count % g.batch_size == 0) {
        if (count > 0) g.out << ";\n\n";
        g.out << "INSERT INTO order_items (order_id, product_id, quantity, "
                 "unit_price) VALUES\n";
      }
      if (count % g.batch_size > 0) g.out << ",\n";

      oi_id++;
      int order_id = o + 1;
      int prod_id = static_cast<int>(g.rng.NextInRange(1, num_products));
      int qty = static_cast<int>(g.rng.NextInRange(1, 10));
      double price = std::round(g.rng.NextInRange(10, 99990) / 100.0 * 100) / 100;

      g.out << "  (" << order_id << ", " << prod_id << ", " << qty
            << ", " << std::fixed << std::setprecision(2) << price << ")";
      order_total += price * qty;
      count++;
    }
    if (o < static_cast<int>(order_amounts.size())) {
      order_amounts[o] = order_total;
    }
  }
  if (count % g.batch_size != 0) g.out << ";\n\n";

  // =========================================================================
  // 7. Shipments
  // =========================================================================
  std::cout << "  [7/7] Generating shipments..." << std::endl;
  count = 0;
  for (int i = 1; i <= num_shipments; i++) {
    if (count % g.batch_size == 0) {
      if (count > 0) g.out << ";\n\n";
      g.out << "INSERT INTO shipments (id, order_id, warehouse_id, status, "
               "tracking_no, ship_date, arrive_date) VALUES\n";
    }
    if (count % g.batch_size > 0) g.out << ",\n";

    int order_id = i;
    int wh_id = static_cast<int>(g.rng.NextInRange(1, num_warehouses));
    int st_idx = g.rng.NextInRange(0, kNumShipStatus - 1);
    std::string tracking = "SF" + std::to_string(g.rng.NextInRange(1000000000ull, 9999999999ull));
    int ship_days = static_cast<int>(g.rng.NextInRange(5, 60));
    std::string ship_date = g.Timestamp(ship_days);
    std::string arrive_date = g.Timestamp(std::max(0, ship_days - 3));

    g.out << "  (" << i << ", " << order_id << ", " << wh_id << ", '"
          << kShipStatus[st_idx] << "', " << Quote(tracking) << ", "
          << ship_date << ", " << arrive_date << ")";
    count++;
  }
  if (count % g.batch_size != 0) g.out << ";\n\n";

  // Write UPDATE statements to fix order total_amount values
  std::cout << "  Writing order amount updates..." << std::endl;
  for (int o = 0; o < num_orders; o++) {
    if (order_amounts[o] > 0) {
      g.out << "UPDATE orders SET total_amount = "
            << std::fixed << std::setprecision(2) << order_amounts[o]
            << " WHERE id = " << (o + 1) << ";\n";
    }
    if ((o + 1) % 10000 == 0) {
      g.out << "\n";  // Add line breaks every 10000 updates
    }
  }
  g.out << std::endl;

  g.out.close();
  std::cout << "Data generation complete: " << data_path << std::endl;
}

void PrintUsage(const char* prog) {
  std::cout << "goods_db test data generator v1.0.0\n\n"
            << "Usage: " << prog << " [options]\n\n"
            << "Options:\n"
            << "  --output-dir PATH   Output directory (default: .)\n"
            << "  --warehouses N      Number of warehouses (default: 50)\n"
            << "  --products N        Number of products (default: 1000)\n"
            << "  --customers N       Number of customers (default: 50000)\n"
            << "  --orders N          Number of orders (default: 100000)\n"
            << "  --seed N            Random seed (default: 42)\n"
            << "  --help              Show this help\n";
}

int main(int argc, char* argv[]) {
  std::string output_dir = ".";
  int num_warehouses = 50;
  int num_products = 1000;
  int num_customers = 50000;
  int num_orders = 100000;
  uint64_t seed = 42;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--output-dir" && i + 1 < argc) {
      output_dir = argv[++i];
    } else if (arg.rfind("--output-dir=", 0) == 0) {
      output_dir = arg.substr(13);
    } else if (arg == "--warehouses" && i + 1 < argc) {
      num_warehouses = std::stoi(argv[++i]);
    } else if (arg == "--products" && i + 1 < argc) {
      num_products = std::stoi(argv[++i]);
    } else if (arg == "--customers" && i + 1 < argc) {
      num_customers = std::stoi(argv[++i]);
    } else if (arg == "--orders" && i + 1 < argc) {
      num_orders = std::stoi(argv[++i]);
    } else if (arg == "--seed" && i + 1 < argc) {
      seed = std::stoull(argv[++i]);
    } else if (arg == "--help") {
      PrintUsage(argv[0]);
      return 0;
    }
  }

  std::cout << "goods_db test data generator" << std::endl;
  std::cout << "  Output dir: " << output_dir << std::endl;
  std::cout << "  Seed: " << seed << std::endl;

  GenerateData(num_warehouses, num_products, num_customers, num_orders,
               seed, output_dir);
  return 0;
}
