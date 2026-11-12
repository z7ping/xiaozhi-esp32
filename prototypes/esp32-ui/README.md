# ES3C28P V1.1 UI 还原基线

- 唯一原型：`index.html`（LCDWiki 320×240 横屏交互原型 V1.1，未经授权禁止修改）。
- 稳定几何基线：`ui-baseline.json`；由 Chromium 在 320×240 逻辑坐标系中计算 DOM 布局得到，保留 7 类关键状态的锚点、字体、圆角、SVG ID 与操作。
- 全量导出：`python scripts/extract_es3c28p_ui.py --full --output /tmp/es3c28p-ui-full.json`，可查看 13 个状态的所有可见节点、计算样式和 SVG 定义；不保存额外的第二套设计事实源。
- 固件：`main/boards/lcdwiki-es3c28p/pixel_touch_ui.cc` + `pixel_robot_display.cc`；沿用原语音、天气、万年历、Jellyfin 和 Go 媒体控制链路，不能复制 HTML 模拟数据。

## 门禁

```bash
python -m pip install playwright
python -m playwright install chromium
python scripts/check_es3c28p_ui.py
python scripts/check_es3c28p_ui.py --strict --runtime-tree /path/to/lvgl-runtime-tree.json
```

支持本机已安装 Chromium：设置 `CHROMIUM=/usr/bin/chromium`。

- 默认门禁：重新执行浏览器计算，比较冻结的页面锚点及原始 HTML SHA256；**通过只证明原型基线未漂移**。
- 严格门禁：同时要求 LVGL 正式页面和运行时控件树，按同名 anchor 与 `[x,y,w,h]` 比较，每项容差 2px；没有运行时证据必须失败。正式实现不能通过修改或重新生成 HTML 基线让结果变绿。
- 完整验收还需图标、动画、字体的实际渲染和触摸/音频/内存真机回归。不同渲染引擎，不能仅靠控件坐标宣称像素完全一致。
- 未提供可靠截图比对时明确记录 `not-executed`；无需用户逐页挑错，差异由开发者自行消化。

## 原型假数据与正式行为边界

| 原型 | 固件实际来源 |
| --- | --- |
| 2026-11-12、08:08 | SNTP/RTC 本机时间 |
| 15°C 晴、西安 | 已有 WeatherService 快照 |
| 农历和黄历演示 | 已有 AlmanacService，缺失时展示错误 |
| 小兔子乖乖 / 小星星、模拟进度 | Go 服务端的实际 Jellyfin 播放状态与媒体能力协商 |
| 语音「模拟提问」和固定回答 | Application 正式 listening/connecting/speaking 状态与服务器真实音频 |
| 设置示例值 | 板载 Codec、Backlight 与 NVS |

交互基线：主页触摸机器人进入语音；故事、儿歌入口进入播放器；万年历翻月/选日/详情；音量和播放控制；首页左滑进日历、日历右滑返回。**原型未包含播放队列入口和服务端降级提示**：既有固件功能不能静默删除，必须作为明确受控例外处理。

## UI Parity Matrix（2026-11-12）

| 页面 | Layout/Token | Interaction | Real data | LVGL runtime | Physical device |
| --- | --- | --- | --- | --- | --- |
| 首页 | Missing | Partial | Aligned（旧实现） | not-executed | Pending |
| 语音 | Missing | Missing（独立页面） | Aligned（原链路） | not-executed | Pending |
| 播放器 | Missing | Partial（旧实现） | Aligned（Go 分支支持时） | not-executed | Pending |
| 万年历 | Missing | Partial | Aligned（旧服务） | not-executed | Pending |
| 设置 | Missing | Partial | Aligned（旧设备接口） | not-executed | Pending |

此矩阵是当前对齐状态，而非完成度百分比；验收前必须更新真实证据。
