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
- 严格门禁要求原型的 **13 个交互状态**都出现在实际运行时日志中；对冻结基线中的 7 个关键状态逐元素比较 `[x,y,w,h]`，误差超过 2px、缺失页面/元素即失败。
- 日志每次采样输出 `begin=1` 边界，解析器丢弃上一轮同名状态坐标；不带边界的旧日志禁止通过，避免虚假验收。
- 当前运行时记录的元素**还不是全量**（包括部分 CSS 逻辑分组），必须继续补齐可观测控件映射；严格门禁尚未通过。字体、光效、部分 SVG 轮廓及真实动效还需要独立审核。书本图标已根据批准 SVG 转为 LVGL 线条，仍有细微描边差异。
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

## 真机重叠反馈及修复记录 · 2026-11-12

用户提供四张 LCDWiki ES3C28P 真机照片，确认上一版横屏固件可显示，但**字体和滑杆实际渲染并不符合 HTML 的盒模型**。具体处理：

- 首页：农历行与“你好，我是天天”相撞。两行之间增加实际高度间隙。
- 播放器：未连接服务时标题和状态文字挤在一起；缩短断线文案，保留真实连接失败反馈；时间标记和状态行按字库实际高度留空间。
- 万年历：星期/日期、底部日期/农历文字间隙不足；重新计算文字盒，确保行高不小于 16px。
- 家长设置：第四行主副标题、底部提示碰撞；调整各行 Y 坐标和文字盒。
- 亮度、音量和播放进度滑杆：原先把滑杆本身设成 22px 高，再额外加 5px knob padding，在 LVGL 9.5 中生成约 32px 高的白色块。现统一使用标准 LVGL slider，**22px 触摸容器 + 7px 轨道 + 17px knob**，显式重置主题样式与颜色。
- 字体：官方 `font_noto_sans_basic_14_1` **实际行高为 16px**；只为 ES3C28P 横屏启用组件中现成的 14px 中文字库，保留竖屏原有 16px 版本。
- 机器人：保留一份 LVGL 脸部对象，使用同一眨眼定时器；新增 neutral/happy/thinking/sad/speaking 表情，包含眼睛、瞳孔、脸颊与嘴形变化。

新增回归防护：`test_es3c28p_ui_collisions.py` 固化上述四组标签重叠；诊断固件如果发现文本盒低于当前字体实际行高，会记录 `ES3C28P_UI: label clipped`，严格门禁会失败。暂时不能用该静态结果代替**重新编译、烧录与真机拍照确认**。

## UI Parity Matrix · 2026-11-12

| 页面 | Layout / Token | Interaction / Data | 运行时 / 实机 |
| --- | --- | --- | --- |
| 首页 | Partial：字体行高与提示语间距已修，机器人浮动与表情；未复测 | Partial：真实时间/天气/农历及入口 | pending reflash |
| 语音 | Partial：机器人眨眼和五种表情，呼吸环实现；思考态旋转仍未还原 | Partial：实际状态映射 | pending reflash |
| 播放器 | Partial：缩短断线文字、7px 轨道+触摸容器；未复测 | Partial：真实媒体能力及列表/Seek/播放控制 | pending reflash |
| 万年历 | Partial：星期、日期、农历按 16px 实际行高隔开；未复测 | Partial：实际黄历服务 | pending reflash |
| 家长设置 | Partial：第四行文字分层、亮度音量小轨道；未复测 | Partial：真实设备亮度与音量 | pending reflash |

**当前未达到“1:1 通过”或“固件功能验收通过”。** 不得因浏览器基线通过而擅自更改本状态。
