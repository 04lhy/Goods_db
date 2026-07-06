# goods_db — 新零售场景数据库系统

> 数据库系统设计课程项目 · 湖南大学

## 项目简介

goods_db 是一个面向新零售场景的数据库存储引擎，参考 MySQL 插件式存储引擎架构设计。支持完整的 CRUD 操作、B+Tree 和 Extendible Hash 双索引、LRU-K/Clock 缓存替换策略，以及 slotted page 磁盘管理。

## 团队成员

| 成员 | 分工 |
|------|------|
| **刘晗阳**（组长） | 索引层（B+Tree / ExtendibleHashIndex） |
| 肖清峰 | 引擎接口层 / 集成联调（handlerton, handler, goods_handler） |
| 李锦华 | 磁盘与缓冲层（DiskManager, BufferPoolManager） |
| 杨玉山 | 表与元组层（TablePage, Tuple, TableHeap, TableIterator） |
| 唐瑞涛 | 项目骨架 / 工具链 / 通用组件（CMake, Catalog, RWLatch, Config, Logger） |
| 王宇航 | 缓冲池替换策略 + 页保护（LRU-K, ClockReplacer, PageGuard） |

## 项目结构

```
Goods_db/
├── goods_db/                    # 主项目（存储引擎）
│   ├── src/
│   │   ├── include/             # 头文件
│   │   │   ├── common/          # RID, RWLatch, Config, Logger
│   │   │   ├── type/            # Value, Schema
│   │   │   ├── storage/         # DiskManager, Page, TablePage, Tuple, Index
│   │   │   ├── buffer/          # BufferPoolManager, LRU-K/ClockReplacer
│   │   │   ├── catalog/         # Catalog
│   │   │   └── sql/             # handler, handlerton, goods_handler
│   │   └── ...                  # 各模块实现文件
│   ├── test/                    # 单元测试 & 集成测试
│   ├── third_party/             # 第三方库（符号链接 → bustub/third_party）
│   └── CMakeLists.txt
├── bustub/                      # CMU BusTub 参考实现（MIT License）
├── 第一周/                       # 团队和个人周计划
├── 模板/                         # 报告/计划模板
├── Dockerfile                   # 开发环境镜像
└── docker-compose.yml           # 容器编排
```

## 已实现模块

| 模块 | 测试状态 |
|------|----------|
| 类型系统 (Value / Schema / Tuple) | ✅ 7/7 pass |
| Page / TablePage | ✅ 7/7 pass |
| BufferPoolManager + LRU-K / Clock | ✅ 6/6 pass |
| TableHeap + TableIterator | ✅ 2/2 pass |
| B+Tree Index | ✅ 6/6 pass |
| Extendible Hash Index | ✅ 6/6 pass |
| Catalog + handlerton + handler | ✅ |
| 集成测试 (FullEndToEnd) | ✅ |

## 环境要求

- **编译器**: clang-15+
- **CMake**: 3.22+
- **操作系统**: Linux (Ubuntu 24.04 推荐)

### Docker 环境（推荐）

```bash
docker compose build
docker compose run --rm bustub
```

### 本地编译

```bash
cd goods_db/build
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_C_COMPILER=clang-15 \
         -DCMAKE_CXX_COMPILER=clang++-15
make -j$(nproc)
```

### 运行测试

```bash
cd goods_db/build
ctest --output-on-failure
```

## 参考资料

- [CMU 15-445/645 Database Systems](https://15445.courses.cs.cmu.edu/)
- [BusTub](https://github.com/cmu-db/bustub) (MIT License)

## License

本项目 goods_db 为课程项目，bustub 部分遵循 MIT License（Copyright (c) 2019 CMU Database Group）。
