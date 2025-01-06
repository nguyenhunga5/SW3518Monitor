#pragma once
#include <stdint.h>
#include <Wire.h>

#ifndef BIT
#define BIT(x) (1 << (x))
#endif

namespace h1_SW35xx {

class SW35xx {
public:
  enum fastChargeType_t {
    NOT_FAST_CHARGE = 0,
    QC2,
    QC3,
    FCP,
    SCP,
    PD_FIX,
    PD_PPS,
    MTKPE1,
    MTKPE2,
    LVDC,
    SFCP,
    AFC
  };

  enum PDCmd_t {
    HARDRESET = 1
  };

  enum QuickChargeConfig {
    QC_CONF_NONE  = 0,
    QC_CONF_PE    = BIT(0),
    QC_CONF_SCP   = BIT(2),
    QC_CONF_FCP   = BIT(3),
    QC_CONF_QC    = BIT(4),
    QC_CONF_PD    = BIT(5),
    QC_CONF_PORT2 = BIT(6),
    QC_CONF_PORT1 = BIT(7),
    QC_CONF_AFC   = BIT(8 + 6),
    QC_CONF_SFCP  = BIT(8 + 7),
    QC_CONF_ALL   = QC_CONF_PE | QC_CONF_SCP | QC_CONF_FCP | QC_CONF_QC | QC_CONF_PD |
      QC_CONF_PORT1 | QC_CONF_PORT2 | QC_CONF_AFC | QC_CONF_SFCP
  };

  enum QuickChargePowerClass {
    QC_PWR_9V,
    QC_PWR_12V,
    QC_PWR_20V_1,
    QC_PWR_20V_2
  };

private:
  enum ADCDataType {
    ADC_VIN = 1,
    ADC_VOUT = 2,
    ADC_IOUT_USB_C = 3,
    ADC_IOUT_USB_A = 4,
    ADC_TEMPERATURE = 6,
  };

  TwoWire &_i2c;

  int i2cReadReg8(const uint8_t reg);
  int i2cWriteReg8(const uint8_t reg, const uint8_t data);

  void unlock_i2c_write();
  void lock_i2c_write();

  uint16_t readADCDataBuffer(const enum ADCDataType type);

public:
  SW35xx(TwoWire &i2c = Wire);
  ~SW35xx();
  void begin();
  /**
   * @brief Read the current charging status
   * 
   */
  void readStatus(const bool useADCDataBuffer=false);
  /**
   * @brief Returns the voltage of the NTC in mV
   */
  float readTemperature(const bool useADCDataBuffer=false);
  /**
   * @brief Send PD command
   * 
   * @note This chip seems to be able to send many types of PD commands, but the register documentation only mentions hardreset. If you have a PD sniffer tool, you can try different parameters from 2 to 15 to explore the corresponding commands. Remember to open a PR to tell me!
   */
  void sendPDCmd(PDCmd_t cmd);
  /**
   * @brief Rebroadcast PDO. After changing the maximum current, you need to call this function or replug the USB cable to make the settings take effect.
   */
  void rebroadcastPDO();
  /**
   * @brief Enable or disable the support for certain quick charge features
   * @param flags Multiple values of QuickChargeConfig combined with bitwise or
   */
  void setQuickChargeConfiguration(const uint16_t flags,
      const enum QuickChargePowerClass power);
  /**
   * @brief Set the maximum output current of all PD groups to 5A. Use with caution if your chip is not sw3518s
   */
  void setMaxCurrent5A();
   /**
   * @brief Set the maximum output current of fixed voltage groups
   * 
   * @param ma_xx Maximum output current of each group in mA, minimum step is 50mA, set to 0 to disable
   * @note 5V cannot be disabled
   */
  void setMaxCurrentsFixed(uint32_t ma_5v, uint32_t ma_9v, uint32_t ma_12v, uint32_t ma_15v, uint32_t ma_20v);
  /**
   * @brief Set the maximum output current of PPS groups
   * 
   * @param ma_xxx Maximum output current of each group in mA, minimum step is 50mA, set to 0 to disable
   * @note Note that when the maximum power configured by PD is greater than 60W, pps1 will not be broadcast (TODO: the datasheet says so, but I haven't tried it)
   *       The maximum voltage of pps1 needs to be greater than that of pps0, otherwise pps1 will not be broadcast;
   */
  void setMaxCurrentsPPS(uint32_t ma_pps1, uint32_t ma_pps2);
  /**
  //  * @brief Reset the maximum output current
  //  * 
  //  * @note The current of the 20V group will not be reset
  //  */
  // void resetMaxCurrents();
  // /**
  //  * @brief Enable Emarker detection
  //  */
  // void enableEmarker();
  // /**
  //  * @brief Disable Emarker detection
  //  */
  // void disableEmarker();
public:
  /**
   * @brief Input voltage
   */
  uint16_t vin_mV;
  /**
   * @brief Output voltage
   */
  uint16_t vout_mV;
  /**
   * @brief Output current 1 (type-C)
   */
  uint16_t iout_usbc_mA;
  /**
   * @brief Output current 2 (type-A)
   */
  uint16_t iout_usba_mA;
  /**
   * @brief Fast charge protocol
   */
  enum fastChargeType_t fastChargeType;
  /**
   * @brief PD version (2 or 3)
   */
  uint8_t PDVersion;

public:
//TODO

private:
  bool _last_config_read_success;
};

} // namespace h1_SW35xx
