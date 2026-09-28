# INA228

XRobot Module for the Texas Instruments INA228 digital power monitor over I2C.

The Module configures the INA228 and samples shunt voltage, bus voltage, current,
power, energy, charge and die temperature from `OnMonitor()`, then publishes the
measurement on a topic. It creates no thread.

## Behaviour

- With `auto_init` the constructor writes `CONFIG` (ADC range), `ADC_CONFIG`
  (`0xFB68`: continuous bus, shunt and temperature conversion, 1052 µs each, no
  averaging) and a fixed `SHUNT_CAL` of `0x1000`. On failure it soft-resets the chip
  and retries every 100 ms, blocking until it succeeds. Without `auto_init` the
  device is not touched; call `ConfigureDevice()` yourself.
- Scale factors are computed from `shunt_resistor_uohm` and the ADC range, so the
  fixed `SHUNT_CAL` works for any shunt: shunt-voltage LSB 312.5 nV (78.125 nV with
  `adcrange_div4`), bus-voltage LSB 195.3125 µV, current LSB
  `62.5 µA × 5000 / shunt_resistor_uohm` (a quarter of that with `adcrange_div4`),
  power LSB 3.2 × current LSB, energy LSB 51.2 × current LSB, charge LSB = current
  LSB.
- `OnMonitor()` samples at most every `sample_interval_ms` and publishes every
  sample. `valid` is false when a register read fails, `DIAG_ALRT` reports a memory
  checksum error or no completed conversion, or the energy, charge or math overflow
  flag is set; fields that were not read keep their previous values.

## Topic

| Topic | Type |
| --- | --- |
| `data_topic_name` (default `ina228_data`) | `INA228::Data` |

```cpp
struct Data {
  float shunt_voltage_v;
  float bus_voltage_v;
  float current_a;
  float power_w;
  float energy_j;         // accumulated since reset
  float charge_c;         // accumulated since reset
  float die_temperature_c;
  uint32_t timestamp_ms;  // LibXR::Timebase milliseconds of the sample
  bool valid;
};
```

## Public API

- `const Data& GetData() const`: last sample.
- `bool ConfigureDevice()`: write `CONFIG`, `ADC_CONFIG` and `SHUNT_CAL`.
- `bool ResetAccumulators()`: clear the energy and charge accumulators.

## Dependencies

No other Modules; LibXR only.

## Constructor

```cpp
INA228(LibXR::I2C& i2c,
       const Param& param = {.i2c_addr = 64,
                             .shunt_resistor_uohm = 5000,
                             .adcrange_div4 = false,
                             .sample_interval_ms = 100,
                             .data_topic_name = "ina228_data",
                             .auto_init = true});
```

Dependencies:

- `i2c`: `LibXR::I2C` bus the INA228 is connected to.

Configuration (`Param` fields):

- `i2c_addr`: 7-bit device address without the R/W bit, default 64 (`0x40`).
- `shunt_resistor_uohm`: shunt resistance in µΩ, default 5000 (5 mΩ); 0 is replaced
  by 5000.
- `adcrange_div4`: select the ±40.96 mV ADC range instead of ±163.84 mV, default
  `false`.
- `sample_interval_ms`: minimum interval between samples in ms, default 100; 0 is
  replaced by 1. The actual rate is also bounded by the monitor loop.
- `data_topic_name`: name of the published topic, default `"ina228_data"`.
- `auto_init`: configure the device in the constructor, default `true`.

## Use

```sh
xrobot module add xrobot-org/INA228
xrobot setup
xrobot instance add xrobot-org/INA228
```

`xrobot instance add` writes an instance to `User/xrobot.yaml` with empty
dependencies and the source defaults; set `i2c` to the name of an I2C object the BSP
registers with `XR_REGISTER`:

```yaml
modules:
  - module: xrobot-org/INA228
    id: ina228_0
    args:
      - i2c: i2c1
      - param:
          i2c_addr: '64'
          shunt_resistor_uohm: '5000'
          adcrange_div4: 'false'
          sample_interval_ms: '100'
          data_topic_name: '"ina228_data"'
          auto_init: 'true'
```

BSP side:

```cpp
XR_REGISTER(i2c1, LibXR::I2C);
```

Run `xrobot setup` again to generate `User/xrobot_main.hpp`.

`xrobot module show .` in this repository, or
`xrobot module show Modules/xrobot-org/INA228` in a BSP, prints the manifest and the
current constructor.
