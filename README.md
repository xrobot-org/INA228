# INA228

德州仪器 INA228 数字功率监测芯片（I2C）驱动模块 / Driver Module for the Texas Instruments INA228 digital power monitor over I2C

## 1. 模块作用 / Purpose

INA228 配置芯片，并在 `OnMonitor()` 中采样分流电压、总线电压、电流、功率、能量、电荷和芯片温度，再把测量结果发布到 Topic。

`auto_init` 为 `true` 时，构造函数写入 `CONFIG`（ADC 量程）、`ADC_CONFIG`（`0xFB68`：总线、分流和温度连续转换，每项 1052 µs，无平均）和固定的 `SHUNT_CAL`（`0x1000`）。失败时软复位芯片，每 100 ms 重试，在构造函数中阻塞直到成功。`auto_init` 为 `false` 时构造函数不访问芯片，由 `ConfigureDevice()` 完成配置。

`SHUNT_CAL` 固定，量纲系数由 `shunt_resistor_uohm` 和 ADC 量程计算：分流电压 LSB 为 312.5 nV（`adcrange_div4` 时为 78.125 nV），总线电压 LSB 为 195.3125 µV，电流 LSB 为 `62.5 µA × 5000 / shunt_resistor_uohm`（`adcrange_div4` 时为其四分之一），功率 LSB 为电流 LSB 的 3.2 倍，能量 LSB 为电流 LSB 的 51.2 倍，电荷 LSB 等于电流 LSB。

`OnMonitor()` 最多每 `sample_interval_ms` 毫秒采样一次，每次采样都发布。以下情况 `valid` 为 `false`：寄存器读取失败，`DIAG_ALRT` 报告存储器校验错误或没有完成的转换，或能量、电荷、数学溢出标志置位；未读取的字段保持上一次的值。

模块提供以下公共方法：

- `const Data& GetData() const`：返回最近一次采样。
- `bool ConfigureDevice()`：写入 `CONFIG`、`ADC_CONFIG` 和 `SHUNT_CAL`。
- `bool ResetAccumulators()`：清零能量与电荷累加器。

INA228 configures the chip, samples the shunt voltage, bus voltage, current, power, energy, charge and die temperature in `OnMonitor()`, and publishes the measurement to a Topic.

With `auto_init` set to `true`, the constructor writes `CONFIG` (ADC range), `ADC_CONFIG` (`0xFB68`: continuous bus, shunt and temperature conversion, 1052 µs each, no averaging) and a fixed `SHUNT_CAL` of `0x1000`. On failure it soft-resets the chip and retries every 100 ms, blocking in the constructor until it succeeds. With `auto_init` set to `false`, the constructor does not access the chip and `ConfigureDevice()` performs the configuration.

`SHUNT_CAL` is fixed, and the scale factors are computed from `shunt_resistor_uohm` and the ADC range: the shunt-voltage LSB is 312.5 nV (78.125 nV with `adcrange_div4`), the bus-voltage LSB is 195.3125 µV, the current LSB is `62.5 µA × 5000 / shunt_resistor_uohm` (a quarter of that with `adcrange_div4`), the power LSB is 3.2 times the current LSB, the energy LSB is 51.2 times the current LSB, and the charge LSB equals the current LSB.

`OnMonitor()` samples at most every `sample_interval_ms` ms and publishes every sample. `valid` is `false` when a register read fails, when `DIAG_ALRT` reports a memory checksum error or no completed conversion, or when the energy, charge or math overflow flag is set; fields that were not read keep their previous values.

The Module provides the following public methods:

- `const Data& GetData() const`: return the last sample.
- `bool ConfigureDevice()`: write `CONFIG`, `ADC_CONFIG` and `SHUNT_CAL`.
- `bool ResetAccumulators()`: clear the energy and charge accumulators.

## 2. 采样数据 / Sample Data

`INA228::Data` 的字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `shunt_voltage_v` | `float` | 分流电压，单位 V |
| `bus_voltage_v` | `float` | 总线电压，单位 V |
| `current_a` | `float` | 电流，单位 A |
| `power_w` | `float` | 功率，单位 W |
| `energy_j` | `float` | 复位以来累计的能量，单位 J |
| `charge_c` | `float` | 复位以来累计的电荷，单位 C |
| `die_temperature_c` | `float` | 芯片温度，单位 °C |
| `timestamp_ms` | `uint32_t` | 采样时刻的 `LibXR::Timebase` 毫秒值 |
| `valid` | `bool` | 本次采样有效 |

The fields of `INA228::Data`:

| Field | Type | Meaning |
| --- | --- | --- |
| `shunt_voltage_v` | `float` | Shunt voltage in V |
| `bus_voltage_v` | `float` | Bus voltage in V |
| `current_a` | `float` | Current in A |
| `power_w` | `float` | Power in W |
| `energy_j` | `float` | Energy accumulated since reset, in J |
| `charge_c` | `float` | Charge accumulated since reset, in C |
| `die_temperature_c` | `float` | Die temperature in °C |
| `timestamp_ms` | `uint32_t` | `LibXR::Timebase` milliseconds at the sample |
| `valid` | `bool` | The sample is valid |

## 3. 构造接口 / Constructor

```cpp
INA228(LibXR::I2C& i2c,
       const Param& param = {.i2c_addr = 64,
                             .shunt_resistor_uohm = 5000,
                             .adcrange_div4 = false,
                             .sample_interval_ms = 100,
                             .data_topic_name = "ina228_data",
                             .auto_init = true});
```

依赖：

- `i2c`：INA228 所在的 `LibXR::I2C` 总线，取自 BSP 的硬件注册（`XR_REGISTER`）。

配置参数（`Param`）：

- `i2c_addr`：7 位器件地址，不含读写位，默认 64（`0x40`）。
- `shunt_resistor_uohm`：分流电阻，单位 µΩ，默认 5000（5 mΩ）；0 按 5000 处理。
- `adcrange_div4`：为 `true` 时选择 ±40.96 mV 的 ADC 量程，否则为 ±163.84 mV，默认 `false`。
- `sample_interval_ms`：两次采样的最小间隔，单位 ms，默认 100；0 按 1 处理。实际采样频率同时受 monitor 循环周期限制，该周期由配置中的 `settings.monitor_sleep_ms` 设定，默认 1000 ms。
- `data_topic_name`：发布的 Topic 名称，默认 `"ina228_data"`。
- `auto_init`：为 `true` 时在构造函数中配置芯片，默认 `true`。

Dependencies:

- `i2c`: the `LibXR::I2C` bus of the INA228, taken from the BSP's Registration (`XR_REGISTER`).

Configuration parameters (`Param`):

- `i2c_addr`: 7-bit device address without the R/W bit, default 64 (`0x40`).
- `shunt_resistor_uohm`: shunt resistance in µΩ, default 5000 (5 mΩ); 0 is treated as 5000.
- `adcrange_div4`: when `true`, the ±40.96 mV ADC range is selected, otherwise ±163.84 mV, default `false`.
- `sample_interval_ms`: minimum interval between two samples in ms, default 100; 0 is treated as 1. The actual sample rate is also bounded by the monitor loop period, which is set by `settings.monitor_sleep_ms` in the configuration, 1000 ms by default.
- `data_topic_name`: name of the published Topic, default `"ina228_data"`.
- `auto_init`: when `true`, the chip is configured in the constructor, default `true`.

## 4. Topic

| Topic | 方向 | 类型 | 说明 |
| --- | --- | --- | --- |
| `data_topic_name`（默认 `ina228_data`） | 发布 | `INA228::Data` | 功率监测采样，字段见第 2 节 |

| Topic | Direction | Type | Meaning |
| --- | --- | --- | --- |
| `data_topic_name` (default `ina228_data`) | Publish | `INA228::Data` | Power-monitor sample, fields in section 2 |

## 5. 配置示例 / Configuration Example

`xrobot instance add xrobot-org/INA228` 写入的实例，`i2c` 填写为 BSP 通过 `XR_REGISTER`（硬件注册）注册的 I2C 名称：

An instance written by `xrobot instance add xrobot-org/INA228`, with `i2c` set to an I2C name registered by the BSP with `XR_REGISTER` (Registration):

```yaml
modules:
  - module: xrobot-org/INA228
    id: ina228_0
    args:
      - i2c: i2c1
      - param:
          i2c_addr: 64
          shunt_resistor_uohm: 5000
          adcrange_div4: false
          sample_interval_ms: 100
          data_topic_name: "ina228_data"
          auto_init: true
```

## 6. 依赖与硬件 / Dependencies and Hardware

依赖：LibXR。

硬件：一片通过 I2C 连接的 INA228 和外部分流电阻；I2C 由 BSP 通过 `XR_REGISTER` 注册。

Dependencies: LibXR.

Hardware: one INA228 connected over I2C with an external shunt resistor; the I2C is registered by the BSP with `XR_REGISTER`.
