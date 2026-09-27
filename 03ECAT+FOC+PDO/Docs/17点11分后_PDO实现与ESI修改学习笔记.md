# EtherCAT PDO 改造与 ESI XML 修改学习笔记

> 整理范围：2026 年 9 月 27 日 17:11（北京时间）之后的讲解内容。  
> 面向对象：第一次接触 EtherCAT 对象字典、PDO 映射和 ESI XML 的初学者。  
> 对应工程：`03ECAT+FOC+PDO`。  
> 当前阶段：只验证 PDO 数据传输，EtherCAT 命令尚未接管 FOC，电机功率输出保持禁止。

## 1. 这一阶段到底要完成什么

原来的示例工程使用：

- TwinCAT 通过 RxPDO 控制 LED；
- STM32 通过 TxPDO 把按键状态返回 TwinCAT。

现在要把演示数据改成电机数据：

```text
原来：LED / Switch
现在：电机命令 / 电机反馈
```

但第一阶段故意不让 TwinCAT 真正驱动电机，只建立以下数据通路：

```text
TwinCAT 输出
    ↓ RxPDO
LAN9252
    ↓
APPL_OutputMapping()
    ↓
gEcatMotorRxPdo 调试缓存
    ╳ 暂时不写入 FOC.foc.Target_Speed

FOC 状态、速度、电流
    ↓
APPL_Application()
    ↓
gEcatMotorTxPdo
    ↓
APPL_InputMapping()
    ↓ TxPDO
TwinCAT 输入
```

这样做的原因是安全：先确认字段顺序、数据类型、正负号和缩放系数全部正确，再加入电机使能、通信看门狗、速度限幅和速度斜坡。

## 2. 必须先理解的几个 EtherCAT 概念

### 2.1 RxPDO 和 TxPDO 的方向

Rx 和 Tx 是站在 EtherCAT 从站，也就是 STM32/LAN9252 的角度命名的。

| 名称 | 数据方向 | 在 TwinCAT 中通常显示为 |
|---|---|---|
| RxPDO | TwinCAT → STM32 | Outputs，输出 |
| TxPDO | STM32 → TwinCAT | Inputs，输入 |

因此：

- `0x7000` 是 STM32 接收的命令；
- `0x6000` 是 STM32 返回的反馈；
- `0x1600` 描述 RxPDO 中映射了哪些对象；
- `0x1A00` 描述 TxPDO 中映射了哪些对象。

### 2.2 对象字典、映射对象和 ESI 的区别

这三个概念很容易混淆。

#### 对象字典

对象字典描述从站里有什么变量，例如：

```text
0x7000:01 ControlWord
0x7000:03 TargetVelocity
0x6000:03 ActualVelocity
```

它主要存在于 STM32 固件中。

#### PDO 映射对象

映射对象描述哪些对象会在每个 EtherCAT 周期中自动发送。

```text
0x1600 → RxPDO 映射
0x1A00 → TxPDO 映射
```

#### ESI XML

ESI 是给 TwinCAT 看的设备说明书。它告诉 TwinCAT：

- 从站叫什么名字；
- ProductCode 和 RevisionNo 是什么；
- PDO 中有哪些字段；
- 每个字段是什么类型、多少位；
- SyncManager 使用多少字节。

烧录 STM32 固件不会自动修改 TwinCAT 的 ESI，也不会自动刷新 TwinCAT 项目中已经创建的旧节点。

## 3. 设计新的 PDO 数据格式

为了避免修改当前 SyncManager 的过程数据长度，新的 RxPDO 和 TxPDO 都继续保持 16 字节，也就是 128 bit。

### 3.1 RxPDO：TwinCAT 发给 STM32

| 偏移 | 对象 | 类型 | 长度 | 含义 |
|---:|---|---|---:|---|
| 0 | `0x7000:01` | `UINT16` | 2 字节 | ControlWord，控制字 |
| 2 | `0x7000:02` | `UINT16` | 2 字节 | CommandCounter，命令计数 |
| 4 | `0x7000:03` | `INT32` | 4 字节 | TargetVelocity，单位 0.001 rad/s |
| 8 | `0x7000:04` | `UINT16` | 2 字节 | Reserved1 |
| 10 | `0x7000:05` | `UINT16` | 2 字节 | Reserved2 |
| 12 | `0x7000:06` | `UINT32` | 4 字节 | Reserved3 |

长度计算：

```text
2 + 2 + 4 + 2 + 2 + 4 = 16 字节
```

### 3.2 TxPDO：STM32 返回 TwinCAT

| 偏移 | 对象 | 类型 | 长度 | 含义 |
|---:|---|---|---:|---|
| 0 | `0x6000:01` | `UINT16` | 2 字节 | StatusWord |
| 2 | `0x6000:02` | `UINT16` | 2 字节 | FOC.status |
| 4 | `0x6000:03` | `INT32` | 4 字节 | 实际速度，单位 0.001 rad/s |
| 8 | `0x6000:04` | `INT16` | 2 字节 | 实际 Iq，单位 0.01 A |
| 10 | `0x6000:05` | `INT16` | 2 字节 | 实际 Id，单位 0.01 A |
| 12 | `0x6000:06` | `UINT16` | 2 字节 | 故障代码 |
| 14 | `0x6000:07` | `UINT16` | 2 字节 | 母线电压，单位 0.01 V |

长度计算：

```text
2 + 2 + 4 + 2 + 2 + 2 + 2 = 16 字节
```

## 4. 固件对象字典是如何修改的

文件：`EtherCAT/Src/LAN9252_SPI_SLAVE_IOObjects.h`

### 4.1 0x7000 从 LED 改成电机命令

原来是 8 个 `UINT16 LED`，现在改成：

```c
typedef struct OBJ_STRUCT_PACKED_START {
    UINT16 u16SubIndex0;
    UINT16 ControlWord;
    UINT16 CommandCounter;
    INT32  TargetVelocity;
    UINT16 Reserved1;
    UINT16 Reserved2;
    UINT32 Reserved3;
} OBJ_STRUCT_PACKED_END TOBJ7000;
```

`u16SubIndex0` 不是 PDO 中的实际控制数据。它记录这个对象拥有多少个子索引，本对象为 6。

对象条目类型也同步修改：

```c
DEFTYPE_UNSIGNED16   // ControlWord
DEFTYPE_UNSIGNED16   // CommandCounter
DEFTYPE_INTEGER32    // TargetVelocity
DEFTYPE_UNSIGNED16   // Reserved1
DEFTYPE_UNSIGNED16   // Reserved2
DEFTYPE_UNSIGNED32   // Reserved3
```

`OBJACCESS_RXPDOMAPPING` 表示这个对象允许映射进 RxPDO。

### 4.2 0x6000 从 Switch 改成电机反馈

```c
typedef struct OBJ_STRUCT_PACKED_START {
    UINT16 u16SubIndex0;
    UINT16 StatusWord;
    UINT16 MotorStatus;
    INT32  ActualVelocity;
    INT16  ActualIq;
    INT16  ActualId;
    UINT16 ErrorCode;
    UINT16 BusVoltage;
} OBJ_STRUCT_PACKED_END TOBJ6000;
```

`OBJACCESS_TXPDOMAPPING` 表示该字段允许映射进 TxPDO。

## 5. PDO 映射数字应该怎么读

RxPDO 映射内容为：

```c
={6,
  0x70000110,
  0x70000210,
  0x70000320,
  0x70000410,
  0x70000510,
  0x70000620}
```

以 `0x70000320` 为例，可拆成：

```text
7000  对象索引
03    子索引
20    位长度，0x20 = 32 bit
```

所以它表示：

```text
对象 0x7000 的子索引 3，长度 32 bit
```

同理：

```text
0x70000110 → 0x7000:01，16 bit
0x60000320 → 0x6000:03，32 bit
```

最后两个十六进制数字不是字节数，而是位数：

```text
0x10 = 16 bit
0x20 = 32 bit
```

## 6. 为什么 PDO 中不直接传 float

FOC 内部大量使用 `float`：

```c
FOC.encoder.filterVel
FOC.current.Real_Iq
FOC.current.Real_Id
FOC.foc.Real_VBUS
```

PDO 使用定点整数传输：

```text
速度 × 1000
电流 × 100
电压 × 100
```

例如：

```text
1.500 rad/s → 1500
-1.500 rad/s → -1500
2.35 A → 235
24.12 V → 2412
```

TwinCAT 收到后再除以对应倍率：

```text
ActualVelocity / 1000.0 = rad/s
ActualIq / 100.0 = A
BusVoltage / 100.0 = V
```

采用定点整数的优点：

- 字节长度明确；
- PLC 与 MCU 容易保持一致；
- 正负号清晰；
- 避免浮点格式和对齐差异；
- 方便以后迁移到 CiA 402。

代码中提供了带饱和保护的转换函数，避免浮点数超出整数范围后发生溢出。

## 7. TwinCAT 命令如何进入 STM32

入口函数：

```c
void APPL_OutputMapping(UINT16 *pData)
```

虽然函数名是 `OutputMapping`，这里的 Output 指主站输出，所以方向是 TwinCAT → STM32。

读取 16 位字段：

```c
gRxPdoOutputs0x7000.ControlWord = SWAPWORD(*pTmpData++);
```

读取 32 位字段：

```c
gRxPdoOutputs0x7000.TargetVelocity =
    (INT32)PDO_ReadU32(&pTmpData);
```

SSC 提供的 `pData` 类型是 `UINT16 *`，一个 32 位数会占用两个连续的 16 位字，因此使用：

```c
static UINT32 PDO_ReadU32(UINT16 **pData)
{
    UINT16 lowWord = SWAPWORD((*pData)[0]);
    UINT16 highWord = SWAPWORD((*pData)[1]);

    *pData += 2;
    return ((UINT32)highWord << 16) | (UINT32)lowWord;
}
```

例如 1500：

```text
1500 = 0x000005DC
低 16 位 = 0x05DC
高 16 位 = 0x0000
合并后 = 0x000005DC
```

`TargetVelocity` 最终转换为 `INT32`，所以负速度也可以正确表示。

## 8. 为什么增加 gEcatMotorRxPdo 缓存

接收数据没有直接写进：

```c
FOC.foc.Target_Speed
```

而是先进入：

```c
volatile ECAT_MOTOR_RX_PDO gEcatMotorRxPdo;
```

其中包含：

```c
controlWord
commandCounter
targetVelocity
reserved1
reserved2
reserved3
```

这样做是为了隔离“通信正确性”和“电机控制正确性”。第一阶段只验证通信，错误数据不会直接让电机转动。

Keil Watch 中重点观察：

```c
gEcatMotorRxPdo.controlWord
gEcatMotorRxPdo.commandCounter
gEcatMotorRxPdo.targetVelocity
gEcatRxPdoCount
```

`gEcatRxPdoCount` 每收到一帧 RxPDO 就增加。如果它持续增长，说明周期输出数据确实到达了 STM32。

## 9. STM32 如何向 TwinCAT 返回反馈

`APPL_Application()` 从 FOC 中读取实际状态：

```c
gEcatMotorTxPdo.actualVelocity =
    PDO_FloatToI32(FOC.encoder.filterVel, 1000.0f);

gEcatMotorTxPdo.actualIq =
    PDO_FloatToI16(FOC.current.Real_Iq, 100.0f);

gEcatMotorTxPdo.actualId =
    PDO_FloatToI16(FOC.current.Real_Id, 100.0f);

gEcatMotorTxPdo.errorCode =
    (UINT16)FOC.safe.errorFlag;
```

随后 `APPL_InputMapping()` 按固定顺序把反馈写入 LAN9252 过程数据。

32 位实际速度通过两个 16 位字写出：

```c
PDO_WriteU32(&pTmpData, (UINT32)ActualVelocity);
```

TwinCAT 的 TxPDO 字段顺序必须和这里完全一致。即使总字节数都是 16，只要字段顺序或位宽不一致，后面的数据就会整体错位。

## 10. StatusWord 的当前定义

当前状态字使用低 5 位：

| 位 | 宏 | 含义 |
|---:|---|---|
| bit 0 | `ECAT_STATUS_OP_ACTIVE` | EtherCAT 输出处理已启动，即进入 OP |
| bit 1 | `ECAT_STATUS_FOC_READY` | FOC 状态处于正常运行区间 |
| bit 2 | `ECAT_STATUS_STAGE1_INHIBIT` | 第一阶段强制禁止电机 |
| bit 3 | `ECAT_STATUS_FAULT` | 存在 FOC 故障 |
| bit 4 | `ECAT_STATUS_ENCODER_VALID` | 编码器数据有效 |

第一阶段 bit 2 始终为 1，提醒主站：现在只能验证通信，不允许使能电机。

## 11. 如何保证第一阶段电机不会被 EtherCAT 启动

`FOC_Init()` 完成后执行：

```c
FOC.foc.Target_Speed = 0.0f;
FOC.foc.Target_Iq = 0.0f;
User_Disable_Motor();
```

`User_Disable_Motor()` 会把三相比较值清零并关闭 TIM1 的 MOE：

```c
User_PwmDuty_Set(0, 0, 0);
__HAL_TIM_MOE_DISABLE_UNCONDITIONALLY(&htim1);
```

因此当前状态是：

- ADC 继续采样；
- 编码器继续更新；
- FOC 可以继续计算；
- 反馈可以通过 TxPDO 返回；
- 功率 PWM 不会输出；
- `ControlWord = 1` 也不会启动电机。

## 12. ESI XML 是如何修改的

新 ESI 文件由原来的 `ssc/apptest.xml` 复制得到，保存为：

```text
03ECAT+FOC+PDO/ESI/LAN9252_FOC_PDO.xml
```

### 12.1 修改设备显示名称

```xml
<Type ProductCode="#x00009252" RevisionNo="#x1">
    G474 EtherCAT FOC PDO
</Type>
<Name>G474 EtherCAT FOC PDO</Name>
```

ProductCode 和 RevisionNo 没有随意修改，因为它们必须与 LAN9252 EEPROM 中的从站身份一致。只改 XML 而不更新 EEPROM，会造成扫描匹配失败。

### 12.2 增加有符号数据类型

```xml
<DataType>
    <Name>INT</Name>
    <BitSize>16</BitSize>
</DataType>

<DataType>
    <Name>DINT</Name>
    <BitSize>32</BitSize>
</DataType>
```

对应关系：

| XML 类型 | C 类型 |
|---|---|
| `UINT` | `uint16_t` / `UINT16` |
| `INT` | `int16_t` / `INT16` |
| `UDINT` | `uint32_t` / `UINT32` |
| `DINT` | `int32_t` / `INT32` |

速度必须是 `DINT`，否则负速度会被 TwinCAT 当成很大的正数。

### 12.3 修改 RxPdo

原来 `<RxPdo>` 中是 `LED1` 到 `LED8`，现在改为：

```xml
<Entry>
    <Index>#x7000</Index>
    <SubIndex>3</SubIndex>
    <BitLen>32</BitLen>
    <Name>Target Velocity 0.001 rad-s</Name>
    <DataType>DINT</DataType>
</Entry>
```

其余字段按照新的 0x7000 对象依次填写。

### 12.4 修改 TxPdo

原来 `<TxPdo>` 中是 `Switch1` 到 `Switch8`，现在改为：

```xml
<Entry>
    <Index>#x6000</Index>
    <SubIndex>4</SubIndex>
    <BitLen>16</BitLen>
    <Name>Actual Iq 0.01 A</Name>
    <DataType>INT</DataType>
</Entry>
```

### 12.5 SyncManager 为什么暂时不改

```xml
<Sm DefaultSize="16" StartAddress="#x1100"
    ControlByte="#x64" Enable="1">Outputs</Sm>

<Sm DefaultSize="16" StartAddress="#x1400"
    ControlByte="#x20" Enable="1">Inputs</Sm>
```

新旧 PDO 都是 16 字节，所以 `DefaultSize="16"` 可以保持不变。

## 13. 当前 ESI 的重要限制

当前验证版主要更新了：

- 设备显示名称；
- `<RxPdo>`；
- `<TxPdo>`；
- `INT` 和 `DINT` 基础类型。

原 XML 的静态 `<Dictionary><DataTypes>` 中仍有一部分旧的 Switch/LED 描述。因此它适合当前 PDO 通信验证，但还不是最终规范版本。

最终应保证三部分全部一致：

```text
Dictionary/DataTypes
Dictionary/Objects
RxPdo/TxPdo
```

推荐最终使用 Beckhoff EtherCAT SSC Tool 修改对象字典源，并重新生成：

```text
LAN9252_SPI_SLAVE_IOObjects.h
ESI XML
```

不要长期依赖手工同时维护 C 头文件和 XML，否则后续字段增加时很容易出现位宽或顺序不一致。

## 14. 为什么 TwinCAT 仍然显示 Switch 和 LED

截图中的从站名称仍然是：

```text
PIC32 EtherCAT Slave
```

这说明 TwinCAT 展示的是旧 ESI 创建的旧从站实例，而不是新 ESI 描述。

常见误区是：

```text
烧录新 HEX ≠ TwinCAT 自动更新 PDO 名称
替换 ESI XML ≠ 已存在节点自动刷新
```

TwinCAT 会把已创建的从站和 PDO 树保存进工程。即使后来替换 XML，旧节点通常仍保留旧名称。

本机 ESI 目录为：

```text
C:\Program Files (x86)\Beckhoff\TwinCAT\3.1\Config\Io\EtherCAT
```

正确处理流程：

1. 关闭 TwinCAT XAE 或 Visual Studio。
2. 在 ESI 目录中查找包含 `PIC32 EtherCAT Slave`、`Switch1` 或 `LED1` 的旧 XML。
3. 备份旧 XML，并将它移出 EtherCAT ESI 目录。
4. 将 `LAN9252_FOC_PDO.xml` 复制到该目录。
5. 重新打开 TwinCAT。
6. 删除项目中的旧 `PIC32 EtherCAT Slave` 节点。
7. 对 EtherCAT 主站执行重新扫描或 `Scan Boxes`。
8. 新从站应显示为 `G474 EtherCAT FOC PDO`。
9. PDO 应显示 `Control Word`、`Target Velocity`、`Actual Velocity`、`Actual Iq` 等字段。

新旧 XML 使用相同 ProductCode 和 RevisionNo 时，不应让两份描述同时留在 ESI 目录，否则 TwinCAT 可能继续选中旧描述。

## 15. 第一阶段测试步骤

### 15.1 烧录

烧录：

```text
MDK-ARM/ECAT/ECAT.hex
```

### 15.2 TwinCAT 写入测试值

进入 OP 后写入：

```text
Control Word = 1
Command Counter = 123
Target Velocity = 1500
```

当前 `Control Word = 1` 不会启动电机，只用于验证数据传输。

### 15.3 Keil Watch 检查

```c
gEcatMotorRxPdo.controlWord       // 应为 1
gEcatMotorRxPdo.commandCounter    // 应为 123
gEcatMotorRxPdo.targetVelocity    // 应为 1500
gEcatRxPdoCount                   // 应持续增加
```

再写入：

```text
Target Velocity = -1500
```

Keil 中必须看到 `-1500`，而不是一个很大的正数。

### 15.4 反馈缩放检查

例如 TwinCAT 显示：

```text
Actual Velocity = 1250
Actual Iq = 83
Bus Voltage = 2412
```

换算为：

```text
速度 = 1.250 rad/s
Iq = 0.83 A
母线电压 = 24.12 V
```

### 15.5 安全检查

第一阶段必须确认：

- TIM1 MOE 保持关闭；
- 电机不产生转矩；
- 写 ControlWord 不会使能 PWM；
- RxPDO 正数和负数传输均正确；
- TxPDO 每个字段没有错位；
- EtherCAT 可以稳定进入 OP。

## 16. 编译验证结果

当前工程已经使用 Keil MDK ARMCC 编译验证：

```text
0 Error(s), 0 Warning(s)
```

RxPDO 校验结果：

```text
6 个字段
128 bit
16 字节
```

TxPDO 校验结果：

```text
7 个字段
128 bit
16 字节
```

XML 也已经通过基本解析检查。

## 17. 下一阶段准备做什么

只有第一阶段测试完全通过后，才进入第二阶段：

1. 增加 `enableRequest`；
2. 只有 EtherCAT 在 OP 时才允许使能；
3. 加入 100 ms RxPDO 通信看门狗；
4. 通信超时立即清零目标并关闭 PWM；
5. 限制目标速度范围；
6. 增加目标速度斜坡；
7. 将处理放在本地 1 kHz 控制任务中；
8. 最后才让 `TargetVelocity` 写入 `FOC.foc.Target_Speed`。

第二阶段允许电机转动的条件应同时满足：

```c
motorEnable =
    EtherCAT处于OP &&
    RxPDO看门狗正常 &&
    ControlWord使能位为1 &&
    FOC没有故障;
```

任何一个条件失效，都必须清零转矩命令并关闭 PWM。

## 18. 初学者自检问题

学完本阶段后，应该能够回答：

1. 为什么 TwinCAT Outputs 对应从站 RxPDO？
2. `0x70000320` 中每一部分代表什么？
3. 为什么 `TargetVelocity` 使用 `DINT/INT32`？
4. 为什么速度乘以 1000 后再传输？
5. 为什么一个 32 位字段需要读写两个 `UINT16`？
6. 为什么修改 STM32 固件后 TwinCAT 节点名称不会自动变化？
7. 为什么当前阶段使用隔离缓存，而不是直接写 FOC 目标？
8. 为什么 RxPDO 和 TxPDO 总长度必须与 SyncManager 长度一致？
9. 为什么新旧相同身份的 ESI 不应该同时存在？
10. 为什么必须在通信验证后再开放电机使能？

如果这些问题能够解释清楚，就已经理解了本次 PDO 改造的主要逻辑。

