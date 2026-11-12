# LCDWiki ES3C28P · V1.1 UI 基线和验收

唯一设计依据：`prototypes/esp32-ui/index.html`（用户批准的 320×240 横屏交互原型 V1.1，不得为通过测试自行修改）。

- `ui-baseline.json`：Chromium 计算后的 7 个关键状态锚点与 SHA256、12 个 SVG ID、19 个交互动作。
- `scripts/extract_es3c28p_ui.py --full`：按需导出 13 个状态的完整可见 DOM 节点、计算样式、SVG 和动画；以 HTML 为单一事实来源。
- 固件：`pixel_touch_ui.cc` / `pixel_robot_display.cc`。所有演示数据均由真实 Application、Board、WeatherService、AlmanacService 与服务端媒体状态替换。

## 本地门禁（无需 CI / 截图）

```bash
python -m pip install playwright
python -m playwright install chromium
python scripts/check_es3c28p_ui.py
python scripts/extract_es3c28p_ui.py --full --output /tmp/es3c28p-ui-contract-full.json
python -m unittest discover -s scripts/tests -p 'test_es3c28p_ui_*.py' -v
```

本机已有 Chromium 可使用 `CHROMIUM=/usr/bin/chromium`。

这只能证明 **HTML 基线未漂移**，不能证明 LVGL 像素 1:1。完成固件诊断构建并用真实硬件采集串口后：

```bash
python3 scripts/build.py lcdwiki-es3c28p --name lcdwiki-es3c28p-landscape-diag
# 烧录并采集真实串口日志，例如 idf.py monitor | tee /tmp/es3c28p.log
python scripts/parse_es3c28p_ui_log.py /tmp/es3c28p.log --output /tmp/lvgl-ui.json
python scripts/check_es3c28p_ui.py --strict --runtime-tree /tmp/lvgl-ui.json
```

- 诊断版在实际创建页面和打开详情/音量弹层时记录真实 LVGL 坐标，不额外创建后台任务。
- 严格门禁逐状态、逐元素对比 `[x,y,w,h]`，误差超过 2px、缺少页面/元素或没有硬件证据即失败，不会编造通过结果。
- 当前记录的元素**不是全量**（如不可见容器和少数图标），仍要继续补齐映射；严格门禁尚未通过。字体/图标形状、SVG 细节、动效及不同渲染引擎的像素差异还需要独立审核。
- 未获得实际屏幕图片时视觉比对记录为 `not-executed`，不以几何通过冒充最终视觉验收。

## V1.1 与旧固件能力

| 原型示例 | 固件真实行为 |
| --- | --- |
| 固定 2026-11-12、08:08 | 实际 RTC/SNTP 时间 |
| 15°、晴、西安 | WeatherService 状态，未配置就如实提示 |
| 模拟农历和黄历宜忌 | AlmanacService 实际缓存和按日期查询 |
| 固定故事/儿歌、模拟进度和时长 | Go 服务端播放状态，旧版未协商时禁用控制 |
| 模拟语音提问与固定回答 | 设备 Listening、真实 ASR 文本触发 Thinking、Speaking |
| 演示电量 / Wi-Fi | Board 实际电池等级及网络状态图标，不支持则隐藏 |
| HTML CSS 亮度 65–110% | 硬件 PWM 10–100%，硬件不支持超过 100% |

V1.1 视觉没有“停止”与“播放队列”按钮。为防止已有能力退化：**长按封面打开实际队列，关闭儿童模式后长按播放键停止**；Seek 仅在服务端给出可用时长时启用。此为目前的受控交互差异，仍须真机确认长按手势不误触。

## UI Parity Matrix · 2026-11-12

| 页面 | Layout / Token | Interaction / Data | 运行时 / 实机 |
| --- | --- | --- | --- |
| 首页 | Partial：主要坐标、圆形机器人、网络电量；字体与背景仍有差异 | Partial：真实时间/天气/农历及入口 | not-executed |
| 语音 | Partial：圆形机器人共用；呼吸/思考动效缺失 | Partial：真实 ASR、Listening/Thinking/Speaking | not-executed |
| 播放器 | Partial：封面、时间条、圆形按钮、音量浮层 | Partial：真实媒体能力及列表/Seek/播放控制 | not-executed |
| 万年历 | Partial：7 列网格/详情弹层按原型重排 | Partial：翻月、选日、按需查询 | not-executed |
| 家长设置 | Partial：四行设置与滑杆/开关 | Partial：持久化儿童模式、设备亮度/音量 | not-executed |

**当前未达到“1:1 通过”或“固件功能验收通过”。** 不得因浏览器基线通过而擅自更改本状态。
