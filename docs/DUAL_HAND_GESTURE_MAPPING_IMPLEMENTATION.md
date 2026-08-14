# Gesture Rack — 双手选择 / 手势映射 / 参数调制实现文档

**状态：设计文档，尚未开始代码实现**  
**适用基线：当前 `main` 上的 GestureRack**  
**目标平台：Windows VST3 优先，架构继续保持可扩展到 macOS VST3/AU**

---

## 1. 本次功能目标

把当前只支持“一个 Child Plugin + Open Palm / Closed Fist 控制 Bypass”的 Gesture Rack，扩展成一个真正可演奏、可编程的 **9 插件手势控制 Rack**。

核心交互：

- Rack 内最多加载 **9 个第三方效果插件**，固定编号 `1 ~ 9`。
- **左手只负责选择插件槽位**。
- **右手只负责控制当前选中的插件**。
- 左手识别数字 `1 ~ 9`，分别选择 Rack 的 Slot 1 ~ Slot 9。
- 右手提供 7 个可编程手势源：
  - `Open Palm` ✋
  - `Closed Fist` ✊
  - `Victory` ✌
  - `Thumb Up` 👍
  - `Thumb Down` 👎
  - `Point Right` 👉
  - `Point Left` 👈
- 每一个右手手势都不是写死功能，而是一个 **可拖拽的 Control Source / Modulator**。
- 用户可以像 Synth 里把 LFO 拖到旋钮上一样，把一个手势拖到：
  - Slot 的 `ACTIVE`
  - Slot 的 `BYPASS`
  - Child Plugin 暴露给 Host 的任意参数
- 初始示例：
  - ✋ `Open Palm` → `ACTIVE`
  - ✊ `Closed Fist` → `BYPASS`
- 当某个手势已经映射到连续参数时：
  - 手抬高 → 参数升高
  - 手降低 → 参数降低
  - 手势消失 / 改变 → 默认保持最后值
- 其余右手手势当前可以先不预设功能，但完整保留映射能力。

这个功能必须建立在通用 Mapping 架构上，不能继续在 `PluginProcessor::timerCallback()` 里增加大量 `if (gesture == ...)`。

---

# 2. 用户层面的最终工作流

## 2.1 Rack 加载插件

Rack 顶部显示固定 9 个 Slot：

```text
[1 Compressor] [2 Delay] [3 Reverb] [4 Empty] ... [9 Empty]
```

每个 Slot 至少支持：

- Load / Replace VST3
- Open Plugin UI
- Remove Plugin
- Active / Bypass 状态
- 显示插件名
- 显示是否为当前选中 Slot
- 显示当前该 Slot 有多少 Gesture Mapping

Signal Chain 初始定义为固定串联：

```text
DAW Input
   ↓
Slot 1
   ↓
Slot 2
   ↓
Slot 3
   ↓
...
   ↓
Slot 9
   ↓
DAW Output
```

空 Slot 直接透传。

以后可以再加 Parallel / Send / Reorder，但本次不要把系统复杂化，先固定 `1 → 9` 串联。

---

## 2.2 左手选择 Slot

左手只负责选择，不控制插件参数。

```text
Left Hand Digit 1 → Select Slot 1
Left Hand Digit 2 → Select Slot 2
...
Left Hand Digit 9 → Select Slot 9
```

识别稳定后：

```text
selectedSlot = digit - 1
```

UI 立即高亮对应 Slot。

左手离开镜头后：

```text
保持当前 selectedSlot
```

不能因为左手消失自动退回 Slot 1。

---

## 2.3 右手控制当前 Slot

右手只影响当前 `selectedSlot`。

例如：

```text
左手 = 3
→ Slot 3 Selected

右手 = ✊
→ 执行 Slot 3 上绑定给 Closed Fist 的 Mapping
```

左手切到 6 后：

```text
Slot 6 Selected
```

之后右手手势只执行 Slot 6 自己的 Mapping。

**每个 Slot 保存自己独立的一套 Gesture Mapping。**

因此：

```text
Slot 1:
✋ → Active
✊ → Bypass
✌ + Y → Reverb Mix

Slot 2:
✋ + Y → Delay Feedback
👍 → Active
👎 → Bypass
```

完全可以不同。

---

# 3. 一个重要的实现限制：不能可靠地直接 Drag 到第三方原生 GUI 的某个 Knob

用户体验希望类似：

```text
LFO icon
  ↓ drag
Synth Knob
```

这个交互本身可以实现。

但是第三方 VST3 的原生 Plugin Editor 是厂商自己的 UI。Host 通常只能知道：

```text
Parameter ID / Index
Parameter Name
Current Value
Normalized Value
```

Host 并不知道：

```text
这个屏幕坐标 (x, y) 对应哪个 VST Parameter
```

因此不能做一个通用系统，直接判断“用户把 ✋ 拖到了 FabFilter GUI 上的哪个 Knob”。

如果强行做屏幕坐标 / OCR / 图像分析，会极度不稳定，并且每个插件都不同，不接受这条路线。

## 正确实现：同时提供两套映射 UX

### A. Rack Parameter Inspector — 精确 Drag Mapping

Rack 为当前 Child Plugin 读取全部 Host-visible 参数：

```text
PARAMETERS

Mix                 42%      [ ✋ ]
Decay               65%
Pre Delay            8 ms    [ ✌ ]
Low Cut            220 Hz
High Cut           8.4 kHz
```

用户可以直接：

```text
✋ icon
  ↓ drag
Mix
```

创建精确 Mapping。

这是最稳定、跨插件统一的方式。

### B. Native Plugin UI — Parameter Learn

用户打开厂商原生 UI 后：

1. 从 Rack 拖一个 Gesture Token 到 `LEARN` 区域。
2. Rack 进入：

```text
LEARNING: ✋
Move a parameter on the plugin...
```

3. 用户在原生插件 GUI 上动一次目标旋钮。
4. Rack 通过 Child `AudioProcessorParameter::Listener` 检测到哪个 Parameter 改变。
5. 自动建立：

```text
✋ → detected parameter
```

这样不用知道第三方 GUI 的坐标，也能达到“选一个手势 → 指定一个插件 Knob”的体验。

---

# 4. 9 Plugin Slot 架构

当前代码只持有：

```cpp
juce::AudioProcessorGraph::Node::Ptr childNode;
std::optional<juce::PluginDescription> loadedDescription;
```

这个结构不能直接扩成 9 份散落变量。

不能写成：

```cpp
childNode1
childNode2
childNode3
...
childNode9
```

必须新增 Slot 抽象。

## 4.1 `PluginSlot`

建议新增：

```text
Source/PluginSlot.h
Source/PluginSlot.cpp
```

概念结构：

```cpp
class PluginSlot
{
public:
    int slotIndex = 0;

    std::optional<juce::PluginDescription> description;
    juce::AudioProcessorGraph::Node::Ptr graphNode;

    bool hasPlugin() const;
    juce::AudioPluginInstance* getChild();
    GestureBypassWrapper* getWrapper();

    std::vector<GestureBinding> mappings;
};
```

每个 Slot 自己拥有：

- Child Plugin description
- Child state
- Graph node
- GestureBypassWrapper
- Slot mappings
- Plugin editor window reference / editor state
- Load error

Processor 只持有：

```cpp
std::array<std::unique_ptr<PluginSlot>, 9> slots;
```

---

## 4.2 `RackGraphManager`

建议从 `PluginProcessor` 拆出 Audio Graph 管理：

```text
Source/RackGraphManager.h
Source/RackGraphManager.cpp
```

职责：

- Audio input node
- Audio output node
- 9 Slot 的节点连接
- Slot 加载 / 卸载后的 graph rebuild
- latency 汇总
- bus layout

最终逻辑：

```text
Input
 ↓
loaded slot 1 or passthrough
 ↓
loaded slot 2 or passthrough
 ↓
...
 ↓
loaded slot 9 or passthrough
 ↓
Output
```

不要让 `PluginProcessor` 同时承担：

- Plugin scanning
- 9 Slot 生命周期
- Gesture mapping
- Vision parsing
- Parameter modulation
- Graph routing

否则后面很快失控。

---

# 5. Latency 规则

当前每个 `GestureBypassWrapper` 会保持 Child Plugin 一直 Processing，并通过 delay-compensated dry path 做 Bypass。

9 Slot 后继续保持这个原则。

如果：

```text
Slot 1 latency = 128 samples
Slot 2 latency = 0
Slot 3 latency = 512 samples
```

Rack 对 DAW 报告：

```text
Total latency = 640 samples
```

即使 Slot 3 当前 Gesture Bypass：

```text
总 latency 仍然保持 640
```

这样 Gesture Active / Bypass 不会导致轨道时间位置突然变化。

---

# 6. VisionEngine 从单手升级到双手

当前 `vision_engine.py`：

```python
num_hands=1
```

必须改成：

```python
num_hands=2
```

现在的单个：

```python
self.stabilizer
```

也必须拆成左右手独立状态。

不能让左右手共用 Candidate / Stable Gesture。

建议：

```python
self.left_selector_stabilizer
self.right_gesture_stabilizer
```

---

# 7. Hand Role：左手和右手必须先分离，再分类

Vision pipeline：

```text
Camera Frame
   ↓
MediaPipe 2-hand landmarks + handedness
   ↓
Hand Role Resolver
   ├─ Physical Left Hand
   └─ Physical Right Hand
        ↓
Left Selector Classifier
Right Control Gesture Classifier
```

**不能先把所有手都扔进同一个 Gesture Classifier，再猜谁是谁。**

因为左右手功能完全不同。

## 7.1 镜像问题

当前摄像头代码：

```python
frame = cv2.flip(frame, 1)
```

因此 Handedness 需要实际测试，不能凭字符串假设。

Vision Engine 最终发给 Rack 的字段必须已经归一化成：

```text
physical_left
physical_right
```

Rack 不关心摄像头是否 mirror。

## 7.2 防止左右手瞬间交换

双手交叉时检测系统可能短暂产生 handedness 抖动。

建议 `HandRoleResolver` 使用：

- MediaPipe handedness label
- handedness confidence
- wrist landmark trajectory
- previous-frame nearest-hand matching
- 短 TTL 保持

如果一帧无法可靠判断：

```text
不要立即交换 Left / Right Role
```

宁可短暂 Hold Previous Role。

否则用户双手交叉一下，右手拳头可能突然被当成左手数字选择器。

---

# 8. 左手数字 1 ~ 9 识别

这里不能直接依赖当前 MediaPipe canned gestures。

当前 canned model 本身不是“1 到 9 数字分类器”。

因此新增独立：

```text
LeftSlotGestureClassifier
```

输出：

```cpp
enum class SlotSelectGesture
{
    none,
    slot1,
    slot2,
    slot3,
    slot4,
    slot5,
    slot6,
    slot7,
    slot8,
    slot9
};
```

## 8.1 1 ~ 5

可以通过：

- 21 landmarks
- finger extension state
- finger angle
- fingertip / MCP relative position

做 landmark classifier。

## 8.2 6 ~ 9 是必须明确的设计点

单手“6 / 7 / 8 / 9”并不存在全球唯一手势。

所以 **不要在 C++ Rack Core 里硬编码文化相关姿势**。

设计要求：

```text
Slot Gesture 1~9 是逻辑 ID
具体手型由 VisionEngine classifier / model 定义
```

正式实现 6 ~ 9 前，需要确定最终使用的具体 4 个手型。

建议把数字分类器独立成：

```text
VisionEngine/slot_selector.py
```

未来如果 landmark rule 不够稳定，可以换成训练后的 custom gesture model，而不用改 Rack / Mapping Engine。

## 8.3 Selection Stabilizer

左手选择不能一帧就切 Slot。

建议默认：

```text
confidence >= 0.80
hold >= 150 ms
```

稳定后才：

```text
selectedSlot = N
```

左手消失：

```text
selectedSlot 保持
```

---

# 9. 右手 Gesture Set

新增统一 enum：

```cpp
enum class ControlGesture
{
    unknown = 0,
    openPalm,
    closedFist,
    victory,
    thumbUp,
    thumbDown,
    pointRight,
    pointLeft
};
```

---

## 9.1 可以直接使用 canned classifier 的手势

当前目标：

```text
Open_Palm
Closed_Fist
Victory
Thumb_Up
Thumb_Down
```

Vision Engine 将它们映射到统一 `ControlGesture`。

---

## 9.2 Point Right / Point Left

不要把 👉 / 👈 当成两套完全独立 AI model。

先检测“Index pointing pose”，然后根据 index finger direction 分类方向。

使用例如：

```text
index MCP = landmark 5
index TIP = landmark 8
```

方向向量：

```text
D = tip - MCP
```

只在：

```text
abs(D.x) > abs(D.y) * horizontalThreshold
```

时认定为水平 pointing。

然后在 **用户看到的 mirrored view coordinate** 下：

```text
D.x > threshold → POINT RIGHT
D.x < -threshold → POINT LEFT
```

这样用户看到自己指向哪边，就按哪边触发，不需要脑内反转。

---

# 10. Vision Protocol v2

当前 Protocol v1 只有一只手。

本功能必须升级为 Protocol v2。

建议 packet：

```json
{
  "protocol": 2,
  "seq": 518,
  "timestamp_ms": 123456789,
  "left": {
    "present": true,
    "handedness_confidence": 0.98,
    "raw_slot": 3,
    "stable_slot": 3,
    "confidence": 0.95,
    "landmarks": [[0.1,0.2,0.0]]
  },
  "right": {
    "present": true,
    "handedness_confidence": 0.99,
    "raw_gesture": "Victory",
    "stable_gesture": "Victory",
    "confidence": 0.96,
    "palm_x": 0.56,
    "palm_y": 0.31,
    "palm_z": -0.08,
    "height": 0.78,
    "landmarks": [[0.1,0.2,0.0]]
  }
}
```

Rack 侧新增：

```cpp
struct HandSnapshot
struct DualHandVisionSnapshot
```

不要继续把：

```cpp
bool handPresent;
Gesture stableGesture;
landmarks[21];
```

硬塞进一个单手 struct。

---

# 11. 右手 Height Signal

每一个右手 Gesture 不只提供：

```text
active / inactive
```

还同时提供一个连续信号：

```text
height = 0.0 ~ 1.0
```

## 11.1 Palm Center

不要直接使用一个 fingertip 的 Y。

不同手势时手指形状变化太大。

建议 Palm Center 由：

```text
wrist 0
index MCP 5
middle MCP 9
ring MCP 13
pinky MCP 17
```

取平均或加权平均。

得到：

```text
palmY
```

## 11.2 Height Normalization

摄像头 image coordinate：

```text
顶部 y = 0
底部 y = 1
```

但用户需要：

```text
手越高 → value 越大
```

所以：

```text
height = 1 - normalizedPalmY
```

不要直接拿全画面 0~1 映射，否则用户手到了画面边缘才到参数最大值。

默认使用 active control window：

```text
topY    = 0.15
bottomY = 0.85
```

映射：

```text
palmY <= topY    → 1.0
palmY >= bottomY → 0.0
中间线性插值
```

未来可以让用户 Calibration。

---

# 12. Height 必须平滑

Camera / landmarks 本身会轻微抖动。

绝对不能每帧原样写到音频参数。

建议：

```text
Vision FPS            30 ~ 60
Control update        60 Hz 左右
Default smoothing     80 ms
Default deadband      0.005 ~ 0.01 normalized
```

Mapping Engine 中：

```text
rawHeight
   ↓
deadband
   ↓
one-pole smoothing
   ↓
normalized modulation value
```

这样手保持不动时旋钮不会自己抖。

---

# 13. Control Source 抽象

一个 Gesture Token 不是一个固定命令。

每个右手 Gesture 必须同时暴露两个逻辑 source：

```text
Gesture Gate
Gesture Height
```

例如：

```text
Open Palm:
  active = true/false
  height = 0..1 while active

Closed Fist:
  active = true/false
  height = 0..1 while active
```

这样一个 Gesture 可以同时承担：

```text
✋ entered → Plugin Active
✋ held + move Y → Mix amount
```

而不是以后为了 Y 控制重新写一套系统。

---

# 14. Mapping Data Model

建议新增：

```text
Source/GestureMapping.h
Source/GestureMapping.cpp
Source/GestureMappingEngine.h
Source/GestureMappingEngine.cpp
```

## 14.1 Target 类型

```cpp
enum class MappingTargetType
{
    slotAction,
    childParameter
};
```

## 14.2 初始支持的 Mapping Mode

```cpp
enum class MappingMode
{
    triggerSetActive,
    triggerSetBypassed,
    absoluteHeight
};
```

架构预留：

```text
toggle
setValue
relativeHeight
bipolarHeight
increment
subtract
momentaryGate
```

但本次先不要全部做。

## 14.3 Binding

概念：

```cpp
struct GestureBinding
{
    juce::Uuid id;

    int slotIndex = 0;
    ControlGesture sourceGesture = ControlGesture::unknown;

    MappingTargetType targetType;
    MappingMode mode;

    juce::String parameterStableId;
    int parameterIndexFallback = -1;
    juce::String parameterName;

    float minValue = 0.0f;
    float maxValue = 1.0f;
    float smoothingMs = 80.0f;
    float deadband = 0.008f;
    bool inverted = false;
    bool enabled = true;
};
```

---

# 15. 为什么必须保存 Parameter Stable ID + Fallback

不同 Child Plugin 的参数不能只保存 UI 名称。

例如：

```text
Mix
Mix
Mix
```

可能有重复。

Mapping 保存：

```text
plugin identity
parameter ID if available
parameter index fallback
parameter display name only for UI
```

恢复工程时：

1. 先通过 stable ID 查找。
2. 找不到再用 index fallback。
3. 再校验 parameter name。
4. 都不匹配 → 标为 Missing Mapping，不要偷偷映射到别的参数。

---

# 16. Mapping UX

## 16.1 Gesture Palette

右侧固定显示：

```text
GESTURE MODULATORS

[ ✋ PALM ]
[ ✊ FIST ]
[ ✌ VICTORY ]
[ 👍 THUMB UP ]
[ 👎 THUMB DOWN ]
[ 👉 POINT RIGHT ]
[ 👈 POINT LEFT ]
```

每个 Token：

- 可以 drag
- 当前识别时发光
- 显示 Mapping 数量
- 点击可以展开 Mapping List

---

## 16.2 Slot Action Target

当前选中的 Slot 顶部提供两个特殊目标：

```text
[ ACTIVE ]
[ BYPASS ]
```

初始示例：

```text
✋ drag → ACTIVE
✊ drag → BYPASS
```

生成：

```text
Open Palm entered
→ set selected slot bypass = false

Closed Fist entered
→ set selected slot bypass = true
```

这不是 Toggle。

这样状态永远确定，不会因为识别重复而反复切换。

---

## 16.3 Parameter Target

用户：

```text
✌ drag → Wet Mix
```

默认创建：

```text
mode = absoluteHeight
min  = current parameter min normalized 0
max  = 1
```

之后：

```text
✌ stable active
+ right hand height = 0.2
→ Wet Mix = mapped 0.2

手抬高 height = 0.8
→ Wet Mix = mapped 0.8
```

---

## 16.4 Mapping Amount / Range

创建连续 Mapping 后，点击 Gesture Badge 打开小面板：

```text
VICTORY → Wet Mix

MIN       20%
MAX       75%
INVERT    OFF
SMOOTH    80 ms
REMOVE
```

所以用户不需要让整段手运动覆盖参数完整 0~100%。

映射：

```text
output = min + height * (max - min)
```

如果 invert：

```text
output = max - height * (max - min)
```

---

# 17. 同一个 Gesture 可以映射多个参数

这是 Synth Modulator 应有的行为。

例如 Slot 4：

```text
✌ → Filter Cutoff
✌ → Reverb Mix
✌ → Delay Feedback
```

Victory active 时，右手 Y 同时推动三个参数。

每个 Mapping 有自己的：

```text
min
max
invert
smoothing
```

因此：

```text
hand goes up
Filter Cutoff ↑
Reverb Mix ↑
Delay Feedback ↓   // inverted
```

都可以成立。

---

# 18. Mapping 冲突规则

## 18.1 一个参数被多个不同 Gesture 映射

允许。

因为当前只有一只右手，同一时间只会有一个 stable control gesture。

例如：

```text
✋ → Mix
✌ → Mix
```

不会同时写入。

## 18.2 同一个 Gesture 同时映射 Active 和 Bypass

不允许。

如果用户在同一个 Slot 上：

```text
✋ → Active
✋ → Bypass
```

UI 应拒绝第二个冲突 Mapping，并提示：

```text
This gesture already owns the opposite slot-state action.
```

## 18.3 一个 Gesture 映射多个连续参数

允许。

---

# 19. Slot 切换时必须有 Right-Hand Re-arm

这是非常重要的防误操作规则。

假设：

```text
当前 Slot 2
右手一直握拳
Slot 2 已 Bypass
```

此时左手比 `5`。

如果系统直接让“持续中的拳头”立刻应用到 Slot 5：

```text
Slot 5 会在用户没有重新做手势的情况下突然 Bypass
```

这是危险交互。

因此：

```text
selectedSlot 改变
→ Right Gesture Controller = DISARMED
```

必须等待右手：

```text
离开当前 gesture / 变成 unknown / 换到别的 gesture
然后重新稳定进入一个 gesture
```

才允许控制新 Slot。

UI 可显示：

```text
RIGHT HAND: RELEASE TO ARM
```

这个规则同样适用于连续 parameter modulation。

---

# 20. Gesture Event 和 Continuous Control 必须分离

Mapping Engine 每帧不应该把所有东西都当 Event。

需要两类：

## 20.1 Enter Event

例如：

```text
unknown → Closed Fist
```

产生：

```text
GestureEntered(ClosedFist)
```

用来：

```text
Set Bypass
Set Active
未来 Toggle / Trigger
```

## 20.2 Continuous State

当：

```text
stableGesture == Victory
```

每个 control tick：

```text
Victory.height
```

更新所有 `absoluteHeight` mappings。

这样不会每 16ms 重复执行一次 Bypass Action。

---

# 21. 参数写入线程

绝对不能在 Audio Thread 做 Camera / Gesture / Mapping UI 逻辑。

也不建议 Vision UDP thread 直接调用第三方插件 Parameter。

建议：

```text
VisionReceiver Thread
     ↓ snapshot only
GestureControlEngine Timer / Control Thread (60 Hz)
     ↓ mapping evaluation
Child Parameter setValueNotifyingHost
     ↓
Child DSP sees new parameter
```

Audio Thread：

```text
只处理 audio graph
```

---

# 22. Parameter Learn 实现

每个 Child Plugin 的参数注册 listener。

用户点击 / drag 一个 Gesture Token 进入 Learn 后：

```text
learningGesture = Victory
learningSlot = selectedSlot
```

随后观察参数变化。

## 22.1 识别“用户真正动了哪个参数”

不要只检测任何 value change，因为：

- Plugin 自己可能有内部 modulation
- preset load 可能一次改变几十个参数
- Gesture Engine 自己也在写参数

建议 Learn 条件：

```text
1. Learn 已 armed
2. 参数变化超过 threshold
3. 不属于 Mapping Engine 的 internal write
4. 在短窗口内该参数成为最显著 / 最近的人为变化
```

检测成功后：

```text
Capture Parameter
Exit Learn
Create Binding
```

需要 internal update guard：

```text
isApplyingGestureMapping
```

避免 Gesture Engine 自己改变参数时被 Learn 误抓。

---

# 23. 参数值改变后如何让 Child Plugin GUI 跟着动

Mapping Engine 对 Child Parameter 使用 Host-visible parameter API 写值。

不能直接修改厂商 GUI 控件。

正确逻辑：

```text
Gesture Mapping
   ↓
AudioProcessorParameter normalized value
   ↓
Child Plugin 自己同步 GUI
```

如果某插件没有正确把 GUI 和公开参数绑定，那是该插件自身兼容性问题。

Rack 只能控制它公开给 Host 的参数。

---

# 24. Slot State Mapping

每个 Slot 继续使用 `GestureBypassWrapper`。

新增统一 API：

```cpp
slot.setBypassed(bool)
```

而不是 Mapping Engine 直接碰 wrapper 的内部 atomic。

例如：

```text
Open Palm → ACTIVE
```

执行：

```cpp
slot.setBypassed(false);
```

```text
Closed Fist → BYPASS
```

执行：

```cpp
slot.setBypassed(true);
```

---

# 25. UI 布局建议

插件主界面建议变成：

```text
┌─────────────────────────────────────────────────────────────┐
│ SLOT 1 │ SLOT 2 │ SLOT 3 │ SLOT 4 │ ... │ SLOT 9          │
│ Comp   │ Delay  │ Reverb │ Empty  │     │                 │
└─────────────────────────────────────────────────────────────┘

┌───────────────┐  ┌──────────────────────────────────────────┐
│ LEFT HAND     │  │ SELECTED SLOT 3 — REVERB                │
│               │  │                                          │
│ DIGIT: 3      │  │ [ ACTIVE ] [ BYPASS ] [ OPEN PLUGIN ]   │
│ SLOT: 3       │  │                                          │
│ skeleton      │  │ PARAMETERS                               │
│               │  │ Mix             42%      [✌]            │
│ RIGHT HAND    │  │ Decay           68%                      │
│ ✌ VICTORY     │  │ PreDelay        12 ms                    │
│ HEIGHT 0.72   │  │ ...                                      │
│ skeleton      │  │                                          │
└───────────────┘  └──────────────────────────────────────────┘

GESTURE MODULATORS
[ ✋ ] [ ✊ ] [ ✌ ] [ 👍 ] [ 👎 ] [ 👉 ] [ 👈 ]
```

视觉优先让用户随时知道：

```text
左手识别了什么
当前选中了哪个 Slot
右手识别了什么
当前 Right Hand 是否 Armed
当前高度值
当前 Gesture 正在控制哪些目标
```

否则现场使用时用户会不知道为什么效果没有变化。

---

# 26. Gesture Modulation Visual Feedback

当某个连续 Mapping 正在工作：

Parameter Inspector 上显示：

```text
Wet Mix     62%
            ↑
       ✌ MOD 0.74
```

建议：

- Gesture token active glow
- 目标参数出现 modulation ring / progress overlay
- 显示 raw parameter value
- 显示 current gesture height
- Mapping badge 使用对应 Gesture icon

视觉逻辑不要改第三方插件 GUI，本 Rack 自己呈现状态。

---

# 27. 保存 / 恢复 State

项目 State 现在只保存一个 Child。

升级后必须保存 9 个 Slot：

```text
Rack State
├─ selectedSlot
├─ gestureEnabled
├─ Slot 1
│   ├─ PluginDescription
│   ├─ ChildState
│   ├─ bypass state
│   └─ mappings[]
├─ Slot 2
│   └─ ...
...
└─ Slot 9
```

每个 Mapping 保存：

```text
sourceGesture
targetType
mappingMode
parameterStableId
parameterIndexFallback
parameterName
minValue
maxValue
invert
smoothingMs
deadband
enabled
```

Slot 选择状态也保存。

Vision 当前有没有连接不保存。

---

# 28. Plugin Replace 时 Mapping 怎么处理

如果用户把：

```text
Slot 3 = Valhalla VintageVerb
```

换成：

```text
Slot 3 = FabFilter Pro-Q
```

旧参数 Mapping 不能自动套到新插件。

规则：

```text
Plugin identity changed
→ parameter mappings marked incompatible / cleared after confirmation
```

Slot-level：

```text
✋ → ACTIVE
✊ → BYPASS
```

可以保留，因为它们不依赖 Child Parameter。

参数 Mapping 不保留。

---

# 29. Plugin Unload 时

Unload：

- 停止 parameter listeners
- 关闭 child editor
- 断开 graph node
- Slot 变 passthrough
- 清空 child parameter mappings
- 可以保留 slot action mappings
- rebuild graph
- update total latency

---

# 30. 多 Slot Child Editor

不建议同时允许 9 个厂商插件窗口全部长期保持打开。

第一阶段：

```text
每次只管理当前 Selected Slot 的 ChildEditorWindow
```

用户切 Slot：

- 不强制关闭旧窗口也可以
- 但 Rack 必须知道每个 window 对应 Slot

结构不要继续只有一个：

```cpp
std::unique_ptr<ChildEditorWindow> childEditorWindow;
```

建议 Slot 自己管理 editor 或 Rack 有：

```cpp
std::array<std::unique_ptr<ChildEditorWindow>, 9>
```

---

# 31. 新建议类结构

```text
Source/
├─ GestureTypes.h
├─ VisionReceiver.h/.cpp
├─ DualHandVisionTypes.h
├─ ControlGesture.h
├─ PluginSlot.h/.cpp
├─ RackGraphManager.h/.cpp
├─ GestureBinding.h/.cpp
├─ GestureMappingEngine.h/.cpp
├─ ParameterLearnManager.h/.cpp
├─ GestureBypassWrapper.h/.cpp
├─ PluginProcessor.h/.cpp
└─ PluginEditor.h/.cpp

VisionEngine/
├─ vision_engine.py
├─ slot_selector.py
├─ right_gesture_classifier.py
└─ requirements.txt
```

不是所有文件必须机械拆开，但职责必须按这个方向分层。

---

# 32. Processor 最终应该负责什么

`GestureRackAudioProcessor` 最终只做协调：

```text
Audio lifecycle
State persistence
RackGraphManager
VisionReceiver
GestureMappingEngine
Plugin Slot public API
```

不要继续让它承担所有内部实现。

---

# 33. GestureMappingEngine 的输入 / 输出

输入：

```text
DualHandVisionSnapshot
selectedSlot
slot mappings
```

输出：

```text
Slot action commands
Child parameter normalized updates
```

伪流程：

```text
CONTROL TICK

read vision snapshot

if left stable digit changed:
    select slot
    disarm right hand

resolve right stable gesture

if right controller not armed:
    wait until previous gesture released/changed
    then arm

if new right gesture entered:
    execute trigger mappings for selected slot

if right gesture remains active:
    calculate smoothed height
    execute continuous mappings for selected slot
```

---

# 34. 高度控制的默认行为

用户明确要求：

```text
手抬高 → Knob 高
手降低 → Knob 低
```

因此第一阶段使用 **Absolute Height**，不是 Relative Delta。

也就是说：

```text
Height 0.0 → Mapping Min
Height 1.0 → Mapping Max
```

手势丢失后：

```text
Hold Last Parameter Value
```

不要自动回到原 Knob 值。

以后可以新增：

```text
Relative
Spring Return
Bipolar
Pickup
```

但不是当前需求。

---

# 35. 进入 Gesture 时 Parameter Jump

Absolute Height 有一个天然现象：

原参数：

```text
Mix = 20%
```

用户在画面较高处进入 Victory：

```text
height = 0.8
```

参数会马上向 80% 平滑移动。

这符合 Absolute 模式，但可能出现明显跳变。

因此 Mapping Editor 未来应预留：

```text
Pickup / Soft Takeover
```

但当前第一阶段按用户要求：

```text
Gesture stable → 直接进入当前 Height 对应值，经过 smoothing
```

不要偷偷加入 Pickup 改变交互语义。

---

# 36. Right Gesture Stabilizer

建议继续保留当前稳定思想。

默认：

```text
confidence >= 0.80
hold >= 120 ms
```

但每只手独立。

右手 Unknown / Hand Missing：

```text
不执行新的 trigger
连续参数停止更新
保持最后值
```

---

# 37. Selection 和 Mapping UI 都必须支持鼠标

手势不是唯一控制方式。

用户必须始终可以鼠标：

- 点击 Slot 1~9 选择
- 点击 Active / Bypass
- 手动修改 Parameter
- 创建 / 删除 Mapping
- Gesture globally ON/OFF

手势只是附加 Control Layer。

这样摄像头没开时 Rack 仍然是一个可正常使用的 Plugin Host。

---

# 38. Gesture Global ON/OFF

当前 `GESTURE ON/OFF` 保留。

OFF 时：

```text
左手不切 Slot
右手不触发 Mapping
Vision UI 仍可显示
鼠标仍可操作全部 Rack
Audio 正常处理
```

关闭 Gesture 不应该强制改变任何 Slot Bypass 或 Parameter。

---

# 39. 每个 Slot 也建议有 Gesture Lock

后续建议增加：

```text
Slot Gesture Enable
```

默认 true。

如果某个 Slot 被 Lock：

- 左手仍可以选它
- 右手 Mapping 不执行
- 鼠标照常可用

这个不是本次必须 UI，但数据结构可以预留。

---

# 40. 性能要求

9 个 Plugin Slot + Camera + Gesture Tracking 后，必须保证：

- Vision inference 仍只有一个外部进程
- Rack 的每个 instance 不重复跑 MediaPipe
- 60 Hz Control Mapping 不在 audio callback
- Parameter Inspector 不每帧重新枚举 Child Parameters
- Plugin parameter metadata 在 load 时缓存
- UI repaint 与 Mapping evaluation 分离

---

# 41. 多个 GestureRack 实例

当前 multicast 设计允许多个 Rack instance 接收同一个 Vision Engine。

升级后继续如此。

每个 Rack instance 自己拥有：

```text
selectedSlot
9 child plugins
mapping configuration
right-hand re-arm state
gesture enable
```

Vision Engine 只广播：

```text
Left hand state
Right hand state
```

不要让 Vision Engine 知道任何 DAW Plugin Slot。

否则多个 Rack instance 会互相污染。

---

# 42. 识别层和业务层必须完全解耦

Vision Engine 只负责输出：

```text
Left slot gesture
Right control gesture
Right height
Landmarks
Confidence
```

Vision Engine 不应该输出：

```text
"bypass slot 3"
"set mix to 80%"
```

这些属于 Rack Mapping Engine。

这样以后：

- 修改手势模型
- 增加 Pinch
- 增加 Rotation
- 增加 X Axis

都不需要改 Plugin Hosting 核心。

---

# 43. 将来扩展 Signal 的方式

`ControlSignal` 后续可以扩成：

```text
Gesture Gate
Hand Height Y
Hand X
Hand Depth Z
Pinch
Palm Openness
Palm Rotation
Hand Velocity
Two-hand Distance
```

Mapping Engine 不应该假设只有 Y。

当前只先实现：

```text
Gesture Gate + Right Hand Height
```

---

# 44. 开发顺序

## Phase A — 9 Slot Rack Core

先完全不碰新 Vision。

完成：

1. `PluginSlot`
2. 9 个 Slot
3. 9 Slot serial graph
4. 每个 Slot Load / Replace / Remove
5. 每个 Slot Active / Bypass
6. 每个 Slot Open Editor
7. State save / restore
8. Latency sum

鼠标测试稳定后才能继续。

---

## Phase B — Parameter Discovery / Mapping Core

完成：

1. 枚举 Child Parameters
2. Parameter Inspector
3. GestureBinding 数据结构
4. Mapping save / restore
5. 先用鼠标 / test signal 模拟 `height 0~1`
6. 验证参数能平滑变化
7. Parameter Learn

此时还不用摄像头。

---

## Phase C — Vision Protocol v2

完成：

1. `num_hands = 2`
2. handedness role resolver
3. left / right landmarks 分开
4. packet v2
5. Rack DualHand snapshot
6. UI 同时画左右手

---

## Phase D — Left 1~9 Slot Selector

完成：

1. 先确认数字 6~9 的实际手型定义
2. LeftSlotGestureClassifier
3. 1~9 stabilizer
4. select Slot
5. left hand disappear → hold
6. visual selection feedback

---

## Phase E — Right Gesture Classifier

完成：

```text
Open Palm
Closed Fist
Victory
Thumb Up
Thumb Down
Point Right
Point Left
```

并验证 mirrored direction。

---

## Phase F — Gesture Mapping Runtime

完成：

1. GestureEntered event
2. Right re-arm
3. trigger Slot Action
4. height continuous control
5. min/max
6. invert
7. smoothing
8. mapping visual feedback

---

# 45. 第一组验收案例

## Test 1 — 9 Slot Loading

```text
Slot 1 = EQ
Slot 2 = Compressor
Slot 3 = Delay
Slot 4 = Reverb
Slot 5~9 = Empty
```

音频顺序正确。

---

## Test 2 — Left Slot Select

```text
Left = 1 → Slot 1
Left = 4 → Slot 4
Left disappears → still Slot 4
```

不能随机跳 Slot。

---

## Test 3 — Palm / Fist Active Bypass

Slot 3 mapping：

```text
✋ → ACTIVE
✊ → BYPASS
```

执行：

```text
Left = 3
✊ → Slot 3 bypass
✋ → Slot 3 active
```

不能影响 Slot 2 / 4。

---

## Test 4 — Re-arm Safety

```text
Slot 3 selected
Right = ✊ held
Left changes to Slot 4
```

预期：

```text
Slot 4 不立刻 Bypass
```

必须右手重新释放 / 进入 Gesture 后才触发。

---

## Test 5 — Height Modulation

Slot 2：

```text
✌ → Delay Feedback
MIN = 20%
MAX = 80%
```

Victory active：

```text
手最低 → ~20%
手中间 → ~50%
手最高 → ~80%
```

稳定、不乱跳。

---

## Test 6 — Gesture Missing

```text
Victory height = 0.65
参数 = 59%
```

右手离开画面：

```text
参数保持 59%
```

不能跳回 Base / 0 / Default。

---

## Test 7 — Multiple Targets

```text
✌ → Filter Cutoff
✌ → Reverb Mix
✌ → Delay Feedback (invert)
```

手上移：

```text
Cutoff ↑
Mix ↑
Feedback ↓
```

---

## Test 8 — Save / Reload

DAW 工程保存后重开：

- 9 Slot plugin 恢复
- Child state 恢复
- Slot bypass 状态恢复
- Mapping 恢复
- min/max/invert/smoothing 恢复
- selected slot 恢复

---

# 46. 当前必须明确但不阻塞架构的一个问题

**左手数字 6 / 7 / 8 / 9 的具体手型还没有在需求中定义。**

这不是 Rack 架构问题，只影响：

```text
LeftSlotGestureClassifier
```

所以开发时：

- 9 Slot Core 可以先做
- Mapping Core 可以先做
- Dual Hand Protocol 可以先做
- 正式写 6~9 classifier 前再确认手型

不要因为这个未定义点把整个系统写死。

---

# 47. 当前功能默认定义汇总

```text
LEFT HAND
1 → Slot 1
2 → Slot 2
3 → Slot 3
4 → Slot 4
5 → Slot 5
6 → Slot 6
7 → Slot 7
8 → Slot 8
9 → Slot 9

RIGHT HAND SOURCES
✋ Open Palm
✊ Closed Fist
✌ Victory
👍 Thumb Up
👎 Thumb Down
👉 Point Right
👈 Point Left

DEFAULT EXAMPLE MAPPING
✋ → Selected Slot ACTIVE
✊ → Selected Slot BYPASS

CONTINUOUS MAPPING
Gesture active + hand moves upward
→ parameter increases

Gesture active + hand moves downward
→ parameter decreases

Gesture lost
→ hold last value
```

---

# 48. 最终原则

这个功能的核心不是“增加 7 个 if 判断”。

真正应该形成的结构是：

```text
VISION
  ↓
LEFT / RIGHT HAND ROLES
  ↓
CONTROL SIGNALS
  ├─ Left Slot Select 1~9
  ├─ Right Gesture Gate
  └─ Right Height 0~1
  ↓
GESTURE MAPPING ENGINE
  ↓
CURRENT SELECTED SLOT
  ↓
TARGET
  ├─ ACTIVE / BYPASS
  └─ CHILD PLUGIN PARAMETER
  ↓
9-SLOT AUDIO RACK
```

只要这一层做好，未来增加：

```text
Pinch → Q
Hand X → Pan
Rotation → Filter Type
Two-hand distance → Reverb Size
Hand speed → Feedback
```

都只是新增 `ControlSignal`，而不是重写 Rack。
