#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: XRobot Module for Texas Instruments INA228 digital power monitor
  sensor
depends: []
=== END MANIFEST === */
// clang-format on

#include <cstdint>
#include <memory>

#include "i2c.hpp"
#include "message.hpp"
#include "thread.hpp"
#include "timebase.hpp"

class INA228
{
 public:
  struct Data
  {
    float shunt_voltage_v = 0.0f;
    float bus_voltage_v = 0.0f;
    float current_a = 0.0f;
    float power_w = 0.0f;
    float energy_j = 0.0f;
    float charge_c = 0.0f;
    float die_temperature_c = 0.0f;
    uint32_t timestamp_ms = 0;
    bool valid = false;
  };

  INA228(LibXR::I2C& external_i2c_name, uint16_t i2c_addr, uint32_t shunt_resistor_uohm,
         bool adcrange_div4, uint32_t sample_interval_ms, const char* data_topic_name,
         bool auto_init)
      : i2c_addr_(static_cast<uint16_t>(i2c_addr & 0x7Fu)),
        shunt_resistor_uohm_(shunt_resistor_uohm == 0 ? 5000 : shunt_resistor_uohm),
        adcrange_div4_(adcrange_div4),
        sample_interval_ms_(sample_interval_ms == 0 ? 1 : sample_interval_ms),
        topic_data_(LibXR::Topic::CreateTopic<Data>(data_topic_name)),
        i2c_(std::addressof(external_i2c_name)),
        op_read_block_(sem_i2c_),
        op_write_block_(sem_i2c_)
  {
    const float GAIN = adcrange_div4_ ? 1.0f : 4.0f;

    // 根据 ADCRANGE 与分流电阻预计算量纲系数 / Precompute scaling factors from
    // ADCRANGE and the shunt resistance.
    shunt_voltage_lsb_v_ = (SHUNT_VOLTAGE_LSB_NV_BASE * GAIN / 4.0f / 16.0f) * 1.0e-9f;

    const float CURRENT_LSB_UA_BASE =
        (250.0f * FIXED_SHUNT_UOHM * GAIN) / static_cast<float>(shunt_resistor_uohm_);
    current_lsb_a_ = (CURRENT_LSB_UA_BASE / 16.0f) * 1.0e-6f;
    power_lsb_w_ = (CURRENT_LSB_UA_BASE * 0.2f) * 1.0e-6f;
    energy_lsb_j_ = (CURRENT_LSB_UA_BASE * 3.2f) * 1.0e-6f;
    charge_lsb_c_ = (CURRENT_LSB_UA_BASE / 16.0f) * 1.0e-6f;

    if (auto_init)
    {
      // 构造阶段阻塞重试初始化，失败时软复位后重新探活 / Retry initialization
      // in the constructor; soft-reset and probe again after a failed attempt.
      while (!ConfigureDevice())
      {
        WriteReg16(REG_CONFIG, CONFIG_RST);
        LibXR::Thread::Sleep(100);

        uint16_t device_id = 0;
        ReadReg16(REG_DEVICE_ID, device_id);
        (void)device_id;
        LibXR::Thread::Sleep(10);
      }
    }

    // 注册到应用管理器，后续由 OnMonitor 周期采样 / Register to the application
    // manager for periodic sampling in OnMonitor.
  }

  void OnMonitor()
  {
    const uint32_t NOW_MS = static_cast<uint32_t>(LibXR::Timebase::GetMilliseconds());
    if ((NOW_MS - last_sample_ms_) < sample_interval_ms_)
    {
      return;
    }

    last_sample_ms_ = NOW_MS;
    data_.timestamp_ms = NOW_MS;
    data_.valid = ReadAll(data_);

    topic_data_.Publish(data_);
  }

  const Data& GetData() const { return data_; }

  bool ConfigureDevice()
  {
    const uint16_t CONFIG = adcrange_div4_ ? CONFIG_ADCRANGE : 0u;
    if (!WriteReg16(REG_CONFIG, CONFIG))
    {
      return false;
    }

    if (!WriteReg16(REG_ADC_CONFIG, ADC_CONFIG_DEFAULT))
    {
      return false;
    }

    if (!WriteReg16(REG_SHUNT_CAL, SHUNT_CALIBRATION_DEFAULT))
    {
      return false;
    }

    return true;
  }

  bool ResetAccumulators()
  {
    uint16_t config = 0;
    if (!ReadReg16(REG_CONFIG, config))
    {
      return false;
    }

    const uint16_t RESET_CONFIG = static_cast<uint16_t>(config | CONFIG_RSTACC);
    if (!WriteReg16(REG_CONFIG, RESET_CONFIG))
    {
      return false;
    }

    return WriteReg16(REG_CONFIG, static_cast<uint16_t>(config & ~CONFIG_RSTACC));
  }

 private:
  static constexpr uint8_t REG_CONFIG = 0x00;
  static constexpr uint8_t REG_ADC_CONFIG = 0x01;
  static constexpr uint8_t REG_SHUNT_CAL = 0x02;
  static constexpr uint8_t REG_SHUNT_VOLTAGE = 0x04;
  static constexpr uint8_t REG_BUS_VOLTAGE = 0x05;
  static constexpr uint8_t REG_DIE_TEMP = 0x06;
  static constexpr uint8_t REG_CURRENT = 0x07;
  static constexpr uint8_t REG_POWER = 0x08;
  static constexpr uint8_t REG_ENERGY = 0x09;
  static constexpr uint8_t REG_CHARGE = 0x0A;
  static constexpr uint8_t REG_DIAG_ALRT = 0x0B;
  static constexpr uint8_t REG_DEVICE_ID = 0x3F;

  static constexpr uint16_t CONFIG_ADCRANGE = 1u << 4;
  static constexpr uint16_t CONFIG_RSTACC = 1u << 14;
  static constexpr uint16_t CONFIG_RST = 1u << 15;
  static constexpr uint16_t DIAG_ALRT_ENERGYOF = 1u << 11;
  static constexpr uint16_t DIAG_ALRT_CHARGEOF = 1u << 10;
  static constexpr uint16_t DIAG_ALRT_MATHOF = 1u << 9;
  static constexpr uint16_t DIAG_ALRT_CNVRF = 1u << 1;
  static constexpr uint16_t DIAG_ALRT_MEMSTAT = 1u << 0;
  static constexpr uint16_t ADC_CONFIG_DEFAULT = 0xFB68;
  static constexpr uint16_t SHUNT_CALIBRATION_DEFAULT = 0x1000;

  static constexpr float SHUNT_VOLTAGE_LSB_NV_BASE = 5000.0f;
  static constexpr float BUS_VOLTAGE_LSB_NV_BASE = 3125000.0f;
  static constexpr float FIXED_SHUNT_UOHM = 5000.0f;

  static int32_t SignExtend20(uint32_t val)
  {
    if ((val & (1u << 19)) != 0u)
    {
      return static_cast<int32_t>(val | 0xFFF00000u);
    }
    return static_cast<int32_t>(val);
  }

  static int64_t SignExtend40(uint64_t val)
  {
    if ((val & (1ULL << 39)) != 0ULL)
    {
      return static_cast<int64_t>(val | 0xFFFFFF0000000000ULL);
    }
    return static_cast<int64_t>(val);
  }

  bool ReadReg16(uint8_t reg, uint16_t& out)
  {
    uint8_t raw[2] = {};
    const auto EC =
        i2c_->MemRead(i2c_addr_, reg, LibXR::RawData(raw, sizeof(raw)), op_read_block_);
    if (EC != LibXR::ErrorCode::OK)
    {
      return false;
    }

    out = static_cast<uint16_t>((static_cast<uint16_t>(raw[0]) << 8) | raw[1]);
    return true;
  }

  bool WriteReg16(uint8_t reg, uint16_t value)
  {
    uint8_t raw[2] = {
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value & 0xFFu),
    };
    const auto ERROR_CODE = i2c_->MemWrite(
        i2c_addr_, reg, LibXR::ConstRawData(raw, sizeof(raw)), op_write_block_);
    return ERROR_CODE == LibXR::ErrorCode::OK;
  }

  bool ReadReg24(uint8_t reg, uint32_t& out)
  {
    uint8_t raw[3] = {};
    const auto ERROR_CODE =
        i2c_->MemRead(i2c_addr_, reg, LibXR::RawData(raw, sizeof(raw)), op_read_block_);
    if (ERROR_CODE != LibXR::ErrorCode::OK)
    {
      return false;
    }

    out = (static_cast<uint32_t>(raw[0]) << 16) | (static_cast<uint32_t>(raw[1]) << 8) |
          static_cast<uint32_t>(raw[2]);
    return true;
  }

  bool ReadReg40(uint8_t reg, uint64_t& out)
  {
    uint8_t raw[5] = {};
    const auto EC =
        i2c_->MemRead(i2c_addr_, reg, LibXR::RawData(raw, sizeof(raw)), op_read_block_);
    if (EC != LibXR::ErrorCode::OK)
    {
      return false;
    }

    out = (static_cast<uint64_t>(raw[0]) << 32) | (static_cast<uint64_t>(raw[1]) << 24) |
          (static_cast<uint64_t>(raw[2]) << 16) | (static_cast<uint64_t>(raw[3]) << 8) |
          static_cast<uint64_t>(raw[4]);
    return true;
  }

  bool ReadSigned20(uint8_t reg, int32_t& out)
  {
    uint32_t raw = 0;
    if (!ReadReg24(reg, raw))
    {
      return false;
    }

    // bits [3:0] are reserved in 20-bit data registers.
    out = SignExtend20(raw >> 4);
    return true;
  }

  bool ReadAll(Data& out)
  {
    uint16_t diag_alrt = 0;
    int32_t raw_shunt = 0;
    int32_t raw_bus = 0;
    int32_t raw_current = 0;
    uint32_t raw_power = 0;
    uint16_t raw_temp = 0;
    uint64_t raw_energy = 0;
    uint64_t raw_charge = 0;

    if (!ReadReg16(REG_DIAG_ALRT, diag_alrt))
    {
      return false;
    }
    if ((diag_alrt & DIAG_ALRT_MEMSTAT) == 0u || (diag_alrt & DIAG_ALRT_CNVRF) == 0u)
    {
      return false;
    }

    if (!ReadSigned20(REG_SHUNT_VOLTAGE, raw_shunt) ||
        !ReadSigned20(REG_BUS_VOLTAGE, raw_bus) ||
        !ReadSigned20(REG_CURRENT, raw_current) || !ReadReg24(REG_POWER, raw_power) ||
        !ReadReg16(REG_DIE_TEMP, raw_temp) || !ReadReg40(REG_ENERGY, raw_energy) ||
        !ReadReg40(REG_CHARGE, raw_charge))
    {
      return false;
    }

    out.shunt_voltage_v = static_cast<float>(raw_shunt) * shunt_voltage_lsb_v_;
    out.bus_voltage_v = static_cast<float>(raw_bus) * bus_voltage_lsb_v_;
    out.current_a = static_cast<float>(raw_current) * current_lsb_a_;
    out.power_w = static_cast<float>(raw_power) * power_lsb_w_;
    out.energy_j = static_cast<float>(raw_energy) * energy_lsb_j_;
    out.charge_c = static_cast<float>(SignExtend40(raw_charge)) * charge_lsb_c_;
    out.die_temperature_c = static_cast<float>(static_cast<int16_t>(raw_temp)) / 128.0f;

    const uint16_t INVALID_MASK =
        DIAG_ALRT_ENERGYOF | DIAG_ALRT_CHARGEOF | DIAG_ALRT_MATHOF;
    return (diag_alrt & INVALID_MASK) == 0u;
  }

  uint16_t i2c_addr_ = 0x40;
  uint32_t shunt_resistor_uohm_ = 5000;
  bool adcrange_div4_ = false;
  uint32_t sample_interval_ms_ = 100;
  uint32_t last_sample_ms_ = 0;

  float shunt_voltage_lsb_v_ = 3.125e-7f;
  float bus_voltage_lsb_v_ = 1.953125e-4f;
  float current_lsb_a_ = 1.0e-3f;
  float power_lsb_w_ = 2.0e-4f;
  float energy_lsb_j_ = 3.2e-3f;
  float charge_lsb_c_ = 1.0e-3f;

  Data data_;
  LibXR::Topic topic_data_;

  LibXR::I2C* i2c_ = nullptr;
  LibXR::Semaphore sem_i2c_;
  LibXR::ReadOperation op_read_block_;
  LibXR::WriteOperation op_write_block_;
};
