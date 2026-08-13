# Gesture Rack — 低延迟与 Rack UI 重构实施计划

**状态：待实施**  
**适用基线：当前 `main`**  
**目标：优先解决外接摄像头 5–10 秒控制延迟，并把现有工程化 UI 重构成可演奏、可拖拽、可视化的 Modular Rack。**

---

# 1. 本次改动目标

这次不是单纯“调几个延迟参数”和“换个皮肤”。需要同时解决两个产品级问题：

1. **视觉控制必须具备实时演奏能力**
   - 当前外接摄像头存在约 5–10 秒延迟，这已经不是 `hold_ms` 级别的问题，而是摄像头采集/缓冲链路存在旧帧堆积。
   - 新架构必须保证：识别永远消费“最新一帧”，处理跟不上时丢帧，而不是排队。

2. **主 UI 必须从“开发面板”变成真正的插件 Rack**
   - 当前大量下拉框、按钮、测试控件、数值和 Mapping 列表不适合演奏场景。
   - 参考 Snap Heap / Multipass 的交互思想：插件是模块、手势是调制源、参数是调制目标、连接关系直接可见。
   - 核心交互尽量使用拖拽，不依赖层层菜单和“先选来源 → 再点 Map”式操作。

本次不改变已经确定的产品逻辑：

- Rack 固定 9 个插件 Slot。
- 音频链固定串联 `1 → 2 → ... → 9`。
- 左手负责选择 Slot。
- 右手负责控制当前 Slot。
- 每个 Slot 保存独立 Gesture Mapping。
- 右手手势仍为：
  - Open Palm
  - Closed Fist
  - Victory
  - Thumb Up
  - Thumb Down
  - Point Right
  - Point Left
- 左手 Slot 6–9 手形识别仍不在本次范围内。

---

# 2. 验收标准

## 2.1 延迟验收

不能再用“感觉快了”作为验收。

需要在 Rack 内直接看到实时指标，并满足：

- 不允许出现持续增长的摄像头 backlog。
- Vision Engine 忙时允许丢帧，但最新画面必须及时进入识别。
- 外接 USB 摄像头正常情况下：
  - Camera Capture FPS 可见。
  - Vision Result FPS 可见。
  - Capture → Result latency 可见。
  - Packet age 可见。
- 正常设备目标体感：控制响应进入几十到约一百毫秒级，而不是秒级。
- 当摄像头/驱动无法达到目标时，UI 必须明确显示瓶颈，而不是隐藏问题。

## 2.2 UI 验收

主界面打开后，用户不用阅读说明书也应该能理解：

1. 哪里放插件。
2. 当前选中了哪个 Slot。
3. 哪个手势当前被识别。
4. 哪个手势控制了哪个目标。
5. 手上下移动时，具体哪个参数正在变化。
6. 摄像头是否在线、是否有异常延迟。

主界面不再默认暴露：

- TEST HEIGHT
- TEST ENTER
- MAP ACTIVE
- MAP BYPASS
- MAP PARAM
- LEARN PARAM 按钮堆
- raw/stable/debug 数字堆
- 永久显示的 MIN/MAX/SMOOTHING/DEADBAND 编辑区

这些功能保留，但移动到自然交互或 Debug/Detail 区域。

---

# 3. 延迟问题判断

当前代码链路大致是：

```text
USB Camera
  ↓
OpenCV VideoCapture
  ↓
capture.read()
  ↓
MediaPipe recognize_async()
  ↓
Gesture stabilizer
  ↓
UDP multicast
  ↓
VisionReceiver
  ↓
PluginProcessor 50 Hz control timer
  ↓
GestureMappingEngine
```

当前 `hold_ms=120`、`slot_hold_ms=150`、50 Hz C++ control timer，不可能单独制造 5–10 秒延迟。

因此 5–10 秒延迟优先按“采集旧帧/摄像头后台缓冲”处理。

核心原则：

> Vision Engine 永远只处理摄像头的最新帧。任何来不及处理的历史帧直接丢弃。

---

# 4. VisionEngine 低延迟采集重构

## 4.1 新增 `LatestFrameCapture`

不要继续让识别主循环直接同步调用 `capture.read()`。

增加独立采集对象：

```text
Camera Thread
    ↓
不断 read()
    ↓
覆盖 latestFrame

Vision Thread / Main Loop
    ↓
只 copy 当前 latestFrame
    ↓
recognize_async()
```

建议新文件：

```text
VisionEngine/latest_frame_capture.py
```

职责：

- 打开摄像头。
- 维护独立 capture thread。
- 永远只保留一份最新 frame。
- 保存 frame sequence。
- 保存 capture timestamp。
- 提供线程安全的 `get_latest_frame()`。
- 提供 capture FPS。
- 提供实际 backend / resolution / FPS / FOURCC 信息。
- 停止时正确 join thread 并释放摄像头。

明确禁止：

- `queue.Queue` 存多帧。
- deque 维护历史帧。
- 识别慢时继续积压摄像头帧。

## 4.2 Windows Backend 选择

Windows 外接摄像头优先尝试：

```text
1. CAP_DSHOW
2. CAP_MSMF
3. CAP_ANY
```

封装成统一的 camera open 流程，不能把 backend 写死到主循环。

建议支持参数：

```text
--backend auto
--backend dshow
--backend msmf
--backend any
```

默认 `auto`。

## 4.3 摄像头格式协商

打开后尝试请求：

```text
640 × 480
30 FPS
MJPG
CAP_PROP_BUFFERSIZE = 1
```

但不能假设 `set()` 一定生效。

必须读取实际值，并记录：

```text
requested_width
requested_height
requested_fps
actual_width
actual_height
actual_fps
backend_name
fourcc
```

以后允许扩展 Camera Settings，但本次主界面不放复杂设置菜单。

## 4.4 最新帧时间戳

摄像头线程在拿到 frame 时记录：

```text
capture_monotonic_ms
capture_sequence
```

送给 MediaPipe 后继续记录：

```text
submit_monotonic_ms
result_monotonic_ms
```

因此可以得到：

```text
capture_to_submit_ms
submit_to_result_ms
capture_to_result_ms
```

这是判断延迟来源的基础。

## 4.5 MediaPipe 调用策略

继续使用：

```text
LIVE_STREAM
recognize_async()
```

但提交逻辑要增加：

- 不重复提交同一 `capture_sequence`。
- 没新帧就不提交。
- MediaPipe 忙时允许自然掉帧。
- 绝不为了“每一帧都识别”而建立排队系统。

---

# 5. Gesture 稳定器参数调整

修复摄像头 backlog 后，再降低人为等待。

建议默认：

```text
Right gesture hold:      50 ms
Right gesture release:   50 ms
Left slot hold:           80 ms
```

这些值以后继续作为 CLI/config 参数保留，不写死为唯一行为。

不要把稳定性全部依赖大 hold time。

稳定来源应组合：

```text
classifier confidence
+ candidate hold
+ release hysteresis
+ RightGestureRuntime re-arm
```

---

# 6. C++ 控制更新频率

当前 `PluginProcessor` 使用约 50 Hz timer 处理视觉控制。

改为：

```text
100 Hz control update
```

即约 10 ms 粒度。

注意：

- 不把视觉/JSON/Mapping 逻辑移动到 audio thread。
- Audio thread 继续只做 DSP。
- 连续参数调制仍由 message/control side 更新 child parameter。

Parameter Mapping 默认 smoothing 从：

```text
80 ms
```

调整为：

```text
20 ms
```

用户仍可以对单条 Mapping 修改 smoothing。

---

# 7. Vision Protocol Telemetry

现有 protocol v2 增加 telemetry 字段。

保持向后兼容；未知字段由旧 Receiver 忽略。

建议 packet：

```json
{
  "protocol": 2,
  "seq": 123,
  "timestamp_ms": 123456,
  "telemetry": {
    "capture_seq": 9123,
    "capture_fps": 29.8,
    "vision_fps": 27.4,
    "capture_to_result_ms": 34.2,
    "inference_ms": 25.1,
    "backend": "DSHOW",
    "width": 640,
    "height": 480,
    "fps": 30.0,
    "fourcc": "MJPG"
  },
  "left": {},
  "right": {}
}
```

不要把 telemetry 放进 hand packet；它属于整个 Vision Engine。

---

# 8. VisionReceiver freshness 保护

当前 Receiver 收到合法 JSON 后直接更新 snapshot。

增加：

- sequence regression 检测。
- duplicate packet 检测。
- packet received age。
- telemetry parse。
- stale vision 状态。

注意：Python `timestamp_ms` 使用 monotonic clock，而 C++ `currentTimeMillis()` 是 wall clock，不能直接相减得到跨进程绝对 packet latency。

因此：

- 端到端 capture/result latency 在 Python 内计算后直接发送。
- C++ 只计算 `receivedAtMs` 到当前时间的 local packet age。

UI 最终显示：

```text
VISION LATENCY = Python capture_to_result_ms
PACKET AGE     = C++ now - receivedAtMs
```

---

# 9. 新主 UI 信息架构

整体参考 modular multi-effect / modulation rack 的交互思想，但保持 Gesture Rack 自己的视觉语言。

主界面只保留四个核心区域：

```text
┌─────────────────────────────────────────────────────────────┐
│ TOP STATUS                                                  │
├─────────────────────────────────────────────────────────────┤
│ 9-SLOT SIGNAL CHAIN                                         │
├─────────────────────────────────────────────────────────────┤
│ SELECTED PLUGIN / PARAMETERS                                │
├─────────────────────────────────────────────────────────────┤
│ GESTURE MODULATORS                                          │
└─────────────────────────────────────────────────────────────┘
```

不再把左右手骨骼预览永久占据主界面 1/3 空间。

---

# 10. Top Status

顶部只显示玩家真正需要的状态：

```text
GESTURE RACK

● CAMERA   29 FPS   38 ms
LEFT  3
RIGHT ✌ VICTORY
GESTURES ● ON
DEBUG
```

颜色规则：

```text
camera offline        red
latency < 60 ms       normal/good
60–120 ms             warning
> 120 ms              high latency
> 300 ms              stale / critical
```

颜色只是辅助，必须同时显示文本/数字，不能只靠颜色表达。

---

# 11. 9-Slot Signal Chain

9 个 Slot 从顶部 Tab 改成真正的 Rack Module。

## 11.1 Empty Slot

视觉：

```text
┌──────────────┐
│ 4            │
│              │
│      +       │
│   DROP VST3  │
│              │
└──────────────┘
```

支持：

- 点击 `+` 使用文件选择器。
- 从系统拖 `.vst3` 到 Slot。
- 拖新的 `.vst3` 到已加载 Slot = Replace。

文件选择器作为 fallback 保留，但不作为主要交互。

## 11.2 Loaded Slot

视觉：

```text
┌──────────────┐
│ 3        ↗ × │
│              │
│ VINTAGE VERB │
│              │
│ ● ACTIVE   B │
│ 4 MODS       │
└──────────────┘
```

交互：

- 单击主体：选择 Slot。
- 双击主体 / `↗`：打开 Child Plugin GUI。
- Active/BYPASS 直接点击。
- `×`：Remove。
- 拖 VST3 到模块：Replace。

不再需要主界面底部：

```text
LOAD / REPLACE
OPEN PLUGIN
REMOVE
ACTIVE
```

这些独立大按钮。

## 11.3 左手 Slot 选择反馈

左手稳定识别 `3` 时：

- Slot 3 模块直接高亮。
- Slot 顶部显示短暂 `LEFT HAND` source indicator。
- 不需要用户去另一个面板阅读 `stableSlot=3`。

---

# 12. Selected Plugin / Parameter Area

选择一个有插件的 Slot 后，显示该插件的 Host-visible parameters。

不要继续使用纯文本 ListBox 作为唯一主要表现。

首选：Parameter Target Grid。

每个 Parameter Target 至少显示：

```text
MIX
42%
[ parameter control / visual meter ]
[ mapped gesture badges ]
```

参数很多时允许滚动/搜索，但默认 UI 优先展示：

- 当前有 Mapping 的参数。
- 最近操作的参数。
- 其余 Host-visible 参数。

底层仍使用现有 `ParameterDescriptor` 和 stable parameter identity，不改变 mapping 数据模型。

---

# 13. Gesture Modulators

删除 Gesture 下拉选择框。

底部固定显示 7 个 Gesture Source：

```text
[ ✋ PALM ]
[ ✊ FIST ]
[ ✌ VICTORY ]
[ 👍 UP ]
[ 👎 DOWN ]
[ 👉 RIGHT ]
[ 👈 LEFT ]
```

每个都是独立的可拖拽 `GestureSourceComponent`。

当前识别到某个右手 Gesture 时：

- 对应 Gesture Source 高亮。
- 如果它有 active mappings，对应目标同步显示 modulation feedback。

手势不再被 UI 当成“下拉选项”，而是统一当作：

```text
MODULATION SOURCE
```

---

# 14. Drag Mapping UX

## 14.1 Gesture → Active

```text
✋ PALM
   drag
    ↓
ACTIVE
```

松手后直接建立：

```text
Open Palm → Set Active
```

## 14.2 Gesture → Bypass

```text
✊ FIST
   drag
    ↓
BYPASS
```

松手后建立：

```text
Closed Fist → Set Bypassed
```

## 14.3 Gesture → Parameter

```text
✌ VICTORY
   drag
    ↓
MIX
```

建立：

```text
Victory + right hand height → Mix
```

连续参数 mapping 创建后默认：

```text
min = 0.0
max = 1.0
smoothing = 20 ms
deadband = 0.008
invert = false
enabled = true
```

这些仍由 `GestureBinding` 保存。

---

# 15. Mapping 可视化

建立连接后，不能只在右侧文字列表里显示。

目标本身必须显示来源。

例如：

```text
MIX     42%
✌
```

或：

```text
ACTIVE
✋
```

拖拽过程中允许显示临时连接线：

```text
✌ ─────────→ MIX
```

Mapping 完成后不需要永久画满屏幕连接线，避免 10+ mapping 时 UI 变乱。

默认使用目标旁的 Gesture Badge 表示连接。

鼠标 hover / 点击 badge 时再突出显示 source-target connection。

---

# 16. 连续调制可视反馈

这是新 UI 的重点。

当：

```text
Victory → Mix
```

且用户正在做 Victory 时，Parameter Target 必须实时显示：

- 当前参数 normalized value。
- Gesture mapping 当前输出。
- 调制正在 live 的状态。

不要只显示一个静态数值。

视觉可以参考 modulation ring / overlay，但必须保证：

- 原参数值可辨识。
- gesture modulation range 可辨识。
- 当前 gesture output 可辨识。

以后增加 X/Y/Z/velocity 等 modulation source 时，这套可视化无需重写。

---

# 17. Mapping Detail Editor

MIN / MAX / SMOOTHING / DEADBAND / INVERT 不再永久占据 UI。

点击目标旁 Gesture Badge，例如 `✌`，才打开轻量 Detail Editor：

```text
VICTORY → MIX

RANGE       10% ━━━━━━━━━ 80%
SMOOTH      ━━━●━━━━━━━━ 20 ms
DEADBAND    ━●━━━━━━━━━━ 0.008
INVERT      ○
ENABLED     ●

REMOVE MAPPING
```

所有修改实时生效。

删除：

```text
APPLY
```

按钮。

除非某个 Child Plugin 明确要求 transaction 式更新，否则 GUI 参数编辑不需要 Apply。

---

# 18. Parameter Learn 重构

第三方原生 Plugin GUI 无法通用识别“哪个屏幕坐标对应哪个 parameter”，因此保留现有 Parameter Listener Learn 架构。

但交互改为拖拽：

```text
✌ VICTORY
   drag
    ↓
LEARN TARGET
```

进入：

```text
LEARNING: VICTORY
Move a parameter in the plugin...
```

用户在 Child Plugin GUI 中转动一次旋钮后自动完成 Mapping。

`LEARN PARAM` 不再是主界面常驻按钮。

Selected Plugin 区提供一个小型 drop target：

```text
DROP GESTURE HERE TO LEARN NATIVE CONTROL
```

---

# 19. Debug Panel

现有左右手骨骼、raw/stable 状态、confidence 等开发信息不删除。

全部移入折叠 Debug Panel。

Debug Panel 包含：

```text
Camera backend
Camera resolution
Camera requested/actual FPS
Capture FPS
Vision FPS
Capture → Result ms
Inference ms
Packet age
Protocol
Sequence
Left raw slot
Left stable slot
Right raw gesture
Right stable gesture
Confidence
Right hand height
21 landmarks preview
```

主界面默认关闭 Debug。

这样开发能力保留，但用户不再被工程信息淹没。

---

# 20. JUCE UI 组件拆分

不要继续把所有 UI 逻辑堆进 `PluginEditor.cpp` 和 `ParameterInspector.cpp`。

建议新增：

```text
Source/UI/
  RackTopBar.h/.cpp
  RackChainComponent.h/.cpp
  RackSlotComponent.h/.cpp
  SelectedPluginComponent.h/.cpp
  ParameterGridComponent.h/.cpp
  ParameterTargetComponent.h/.cpp
  GestureModLane.h/.cpp
  GestureSourceComponent.h/.cpp
  MappingBadgeComponent.h/.cpp
  MappingDetailComponent.h/.cpp
  ParameterLearnDropTarget.h/.cpp
  VisionStatusComponent.h/.cpp
  VisionDebugPanel.h/.cpp
```

如果实现过程中发现部分组件过小，可合理合并，但职责边界保持不变。

---

# 21. Drag & Drop 架构

Editor 层提供统一 Drag container。

不要用散落 magic string 判断 drag 类型。

定义通用 payload 数据：

```cpp
struct ModulationDragPayload
{
    ModulationSourceType sourceType;
    ControlGesture gesture;
};
```

当前只有：

```text
sourceType = gesture
```

未来可以自然扩展：

```text
handHeight
handX
handY
handZ
velocity
rotation
twoHandDistance
```

而不改变目标组件接口。

目标统一分成：

```text
Slot Action Target
Child Parameter Target
Learn Target
```

Drag target 最终调用现有 `GestureMappingEngine`，UI 不直接修改 `PluginSlot::mappings`。

---

# 22. VST3 文件拖拽

`RackSlotComponent` 支持 JUCE `FileDragAndDropTarget`。

规则：

- Empty Slot + `.vst3` → Load。
- Loaded Slot + `.vst3` → Replace。
- 非 VST3 → reject，给清晰反馈。
- Gesture Rack 自身 → reject。
- Instrument → 仍沿用当前“不支持 instrument”规则。

文件选择器保留为点击 `+` / Replace fallback。

---

# 23. 底层现有系统尽量复用

以下系统原则上不推翻：

```text
PluginSlot
RackGraphManager
GestureBinding
GestureMappingEngine
ParameterLearnManager
RightGestureRuntime
VisionReceiver
9-slot state persistence
stable parameter identity
child plugin async loading
```

本次主要改变：

```text
Vision capture architecture
Vision telemetry
control timing defaults
UI presentation
UI mapping interaction
```

避免为了 UI 重构重新发明底层 Mapping 系统。

---

# 24. State Compatibility

UI 重构不能破坏已有 session。

要求：

- 现有 state version 4 项目仍可恢复。
- 当前每个 Slot 的插件、插件 state、bypass、mapping 仍可恢复。
- UI 不改变 mapping 的业务含义。
- 如果 telemetry 不存在，UI 显示 `N/A`，不阻止旧 Vision packet 工作。

只有在新增需要持久化的新用户设置时才升级 state version。

纯 UI layout / telemetry 不应无意义增加 state version。

---

# 25. 实施顺序

严格按以下顺序开发，避免 UI 与性能问题互相干扰。

## Step 1 — 建立可测量的延迟数据

- Vision telemetry 数据结构。
- capture/result timestamp。
- Capture FPS / Vision FPS。
- Debug log。

先能量化问题，再修。

## Step 2 — `LatestFrameCapture`

- 独立 camera thread。
- 单帧 latest buffer。
- 不允许 backlog。
- clean shutdown。

完成后先验证外接摄像头 5–10 秒延迟是否消失。

## Step 3 — Windows camera backend / format

- DSHOW / MSMF / ANY fallback。
- MJPG 请求。
- FPS/resolution negotiation。
- actual camera info。

## Step 4 — 稳定器与 control latency

- hold/release 默认调整。
- C++ 100 Hz control update。
- parameter smoothing 默认 20 ms。

## Step 5 — VisionReceiver telemetry / freshness

- telemetry parse。
- duplicate/regression protection。
- packet age。
- stale status。

## Step 6 — 新 Rack UI 骨架

先建立：

```text
TopBar
RackChain
SelectedPlugin
GestureModLane
DebugPanel
```

此时不急着做所有 drag mapping。

## Step 7 — RackSlot 模块化

- 9 个真正 Slot module。
- load/open/remove/active/bypass 内嵌。
- VST3 file drag/drop。
- 左手选择直接高亮。

## Step 8 — Gesture Source drag system

- 7 个 GestureSourceComponent。
- 当前 live gesture 高亮。
- drag payload。

## Step 9 — Slot Action drag targets

- Gesture → Active。
- Gesture → Bypass。
- mapping badge。
- remove/edit mapping。

## Step 10 — Parameter drag mapping

- ParameterTargetComponent。
- Gesture → Parameter。
- live modulation feedback。
- range visualization。

## Step 11 — Mapping detail editor

- min/max。
- smoothing。
- deadband。
- invert。
- enabled。
- realtime update，无 Apply。

## Step 12 — Parameter Learn drag target

- Gesture → Learn Target。
- native child GUI parameter listener。
- successful capture feedback。

## Step 13 — Debug UI 收口

- 手部 landmark preview 移入 Debug。
- camera diagnostics 完整显示。
- 主页面删除旧工程化控件。

## Step 14 — 完整回归测试

- 9 Slot audio chain。
- state save/restore。
- plugin replace/remove。
- mapping persistence。
- left hand selection。
- right hand re-arm。
- continuous modulation。
- duplicate instances。
- Vision Engine restart。
- camera unplug/replug failure behavior。

---

# 26. 必测摄像头场景

至少覆盖：

```text
Laptop built-in camera
USB webcam
USB capture device（如果可用）
30 FPS camera
60 FPS camera（如果可用）
Camera backend DSHOW
Camera backend MSMF
Camera temporary disconnect
Vision Engine restart
DAW already open then Vision start
Vision already running then DAW load Rack
```

每个场景记录：

```text
capture FPS
vision FPS
capture_to_result_ms
packet age
visible control response
```

---

# 27. UI 必测工作流

## Workflow A — 第一次使用

```text
打开 Rack
→ 看到 Empty Slot
→ 拖 VST3 进去
→ 插件加载
→ Open Palm / Fist 默认控制 Active/Bypass
```

不阅读文档也应能完成。

## Workflow B — 新参数 Mapping

```text
选 Slot
→ 从 Gesture Lane 把 Victory 拖到 Mix
→ 做 Victory
→ 上下移动手
→ Mix 实时变化
```

不经过下拉菜单。

## Workflow C — 编辑 Mapping

```text
点击 Mix 旁 Victory badge
→ 修改 Range / Smooth / Invert
→ 立即生效
```

无 Apply。

## Workflow D — Native Learn

```text
把 Thumb Up 拖到 Learn Target
→ 打开 Child Plugin GUI
→ 转一个参数
→ 自动创建 Mapping
```

## Workflow E — 左手切 Slot

```text
左手数字 1–5
→ 对应 Slot 直接高亮
→ 右手只控制当前 Slot mappings
```

---

# 28. 不在本次范围

明确不顺手增加：

- 左手 Slot 6–9 新手形识别。
- Parallel routing。
- Send/Return routing。
- Rack Slot reorder。
- Instrument hosting。
- 第三方原生 GUI 坐标级拖拽识别。
- 全机 VST 扫描器。
- Cloud/network vision。
- 新 gesture classifier 模型训练。

这些以后可以扩展，但不能干扰本次“低延迟 + 可理解 UI”的目标。

---

# 29. 完成定义

本次工作只有同时满足以下条件才算完成：

- 外接摄像头不再出现 5–10 秒累计旧帧延迟。
- Vision Engine 永远基于 latest-frame 架构。
- UI 能显示真实 camera/vision latency diagnostics。
- 主界面不再依赖 Gesture 下拉框 + 多个 MAP 按钮建立映射。
- VST3 可以直接拖到 Slot。
- Gesture 可以直接拖到 Active / Bypass / Child Parameter / Learn Target。
- 当前 Slot、当前 Gesture、当前 Mapping、当前连续调制均有直接可视反馈。
- Debug 信息仍保留，但不占主工作区。
- 现有 9 Slot、Mapping、Plugin State 的保存恢复继续工作。
- 音频线程不引入 camera / JSON / UI / file IO 工作。

最终产品体验应该从：

```text
“一个需要理解内部实现才能操作的工程工具”
```

变成：

```text
“把插件拖进 Rack，把手势拖到目标，然后直接用手演奏”
```
