# MedieaPlayer 推流服务下拉菜单 — 设计文档

**日期:** 2026-05-02
**状态:** 已批准

## 目标

在播放器控制栏增加"推流服务"下拉菜单按钮，对接 VM (192.168.136.137) 上的 nginx-rtmp 服务。

## 改动范围

仅修改 3 个文件，新增约 40 行代码：

| 文件 | 改动 |
|------|------|
| `playerdialog.ui` | 在 `pb_stop` 右侧增加 `pb_stream_service` 按钮 |
| `playerdialog.h` | 声明 `on_pb_stream_service_clicked()` 槽函数 |
| `playerdialog.cpp` | 实现菜单构建 + 输入弹窗 + URL 拼接 + 播放逻辑 |

## 菜单结构

```
[推流服务 ▼]
  ├── RTMP 直播推流  → QInputDialog(输入推流码)  → rtmp://192.168.136.137:1935/videotest/{key}
  ├── RTMP 点播      → QInputDialog(输入文件名)   → rtmp://192.168.136.137:1935/vod/{name}
  └── HLS 点播       → QInputDialog(输入HLS地址)  → 直接使用输入（不拼接）
```

## 交互流程

1. 点击"推流服务"按钮 → 弹出 QMenu（3 个 action）
2. 选择菜单项 → 弹出 QInputDialog::getText，预填默认值
3. 用户确认 → 停止当前播放 → setFileName(url) → start()
4. HLS 选项不拼接前缀，直接使用用户输入的完整 URL

## 不做什么

- 不改动 `on_pb_start_clicked()` 的硬编码 `_DEF_PATH`（保持现有行为）
- 不增加 URL 历史记录
- 不增加 `pb_url` 的交互逻辑
- 不在 VM 侧增加任何代码
