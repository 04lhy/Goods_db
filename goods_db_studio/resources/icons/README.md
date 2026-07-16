# SVG 图标需求规格

> 交付人：李锦华  |  对接人：刘晗阳  |  目标：32+ SVG 图标用于 goods_db_studio 桌面客户端

## 交付物

所有 SVG 文件放到这个目录（`resources/icons/`）下。文件名使用英文小写 + 下划线，例如 `new_connection.svg`。

## 图标规格

| 属性 | 值 |
|------|-----|
| 格式 | SVG 1.1 |
| 画布尺寸 | 24×24 px |
| 颜色 | 使用 `currentColor`（跟随 QSS 主题色） |
| 描边宽度 | 2px |
| 风格 | Material Design / Feather Icons 风格 — 简洁、线条感、无填充背景 |

## 图标清单（共 35 个）

### A. 工具栏图标（6 个，最高优先级）
| 文件名 | 用途 | 描述 |
|--------|------|------|
| `new_connection.svg` | New Connection | 插头 + "+" 号，或数据库节点图标 |
| `open_file.svg` | Open SQL File | 打开的文件夹 |
| `execute.svg` | Execute (F5) | 播放三角形 ▶ |
| `stop.svg` | Stop | 停止方块 ■ |
| `commit.svg` | Commit | 勾选 ✓ 或向上的箭头 |
| `rollback.svg` | Rollback | 撤销 ↩ 或向下的箭头 |

### B. AdminPanel 操作按钮（8 个）
| 文件名 | 用途 | 描述 |
|--------|------|------|
| `flush.svg` | Flush Hosts / Flush Logs / Flush Tables（通用） | 刷新/旋转箭头 |
| `reload.svg` | Reload | 循环箭头 ⟳ |
| `ping.svg` | Ping | 信号波纹或雷达图标 |
| `shutdown.svg` | Shutdown | 电源按钮 ⏻ |
| `kill_connection.svg` | Kill Connection | X 或骷髅头 |
| `refresh.svg` | Refresh Now | 刷新箭头 |
| `dashboard.svg` | Status Dashboard 标题 | 仪表盘/速度表 |
| `process_list.svg` | Process List 标题 | 列表/表格图标 |

### C. LogViewer（4 个）
| 文件名 | 用途 | 描述 |
|--------|------|------|
| `filter.svg` | Apply Filter | 漏斗 |
| `clear_filter.svg` | Clear Filter | 漏斗 + X |
| `search.svg` | 搜索 | 放大镜 🔍 |
| `log.svg` | 日志标签 | 文档/日志图标 |

### D. UserManager（4 个）
| 文件名 | 用途 | 描述 |
|--------|------|------|
| `user_add.svg` | New User | 人 + "+" |
| `user_delete.svg` | Delete User | 人 + "-" 或 X |
| `password.svg` | Change Password | 钥匙 🔑 |
| `permissions.svg` | Apply Permissions / Grants | 盾牌/锁 |

### E. BackupWizard（3 个）
| 文件名 | 用途 | 描述 |
|--------|------|------|
| `backup.svg` | Backup & Restore Wizard | 硬盘 + 箭头（保存） |
| `restore.svg` | Restore | 硬盘 + 反向箭头（恢复） |
| `browse.svg` | Browse... | 文件夹 |

### F. 对象树图标（4 个）
| 文件名 | 用途 | 描述 |
|--------|------|------|
| `database.svg` | 数据库节点 | 圆柱体（数据库经典图标） |
| `table.svg` | 表节点 | 网格/表格 |
| `column.svg` | 列节点 | 单列/竖条 |
| `folder.svg` | 文件夹节点 | 文件夹 |

### G. 通用 UI 图标（6 个）
| 文件名 | 用途 | 描述 |
|--------|------|------|
| `connect.svg` | Test Connection / Connect | 链接/连接图标 |
| `disconnect.svg` | Disconnect | 断开的链接 |
| `export.svg` | Export Results | 导出箭头 ↑ |
| `settings.svg` | Settings / Toggle Theme | 齿轮 ⚙ |
| `about.svg` | About | 信息 i |
| `save.svg` | Save SQL File | 软盘 💾 |

## 颜色说明

所有图标使用 `currentColor` 作为颜色值。例如：

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24"
     viewBox="0 0 24 24" fill="none" stroke="currentColor"
     stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <!-- 路径... -->
</svg>
```

这样深色主题下是白色，浅色主题下是深灰色，自动适配。

## 优先级

1. **P0（必须）**：工具栏 6 个 + 对象树 4 个 = 10 个
2. **P1（重要）**：AdminPanel 8 个 + UserManager 4 个 = 12 个
3. **P2（补充）**：LogViewer 4 个 + BackupWizard 3 个 + 通用 6 个 = 13 个

如果时间不够，优先完成 P0 和 P1 共 22 个。

## 参考网站

- https://feathericons.com/ — 开源 SVG 图标库，可直接下载使用
- https://fonts.google.com/icons — Material Icons，可选择 SVG 下载
- https://lucide.dev/ — Feather 的 fork，更多图标
