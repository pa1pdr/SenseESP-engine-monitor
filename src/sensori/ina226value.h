#ifndef _ina226_value_H_
#define _ina226_value_H_

#include <Arduino.h>
#include <Wire.h>
#include "sensori/INA226.h"

#include "sensesp/sensors/sensor.h"

namespace sensesp {

// The INA226value class is based on https://github.com/jarzebski/Arduino-INA226.
// There is no INA226 class defined by SensESP, as its methods would be almost identical to those
// in the INA226 library. So, in main.cpp, you create a pointer to an INA226, configure it, and
// calibrate it. The pointer will be used by INA226value.

// See /examples/ina226_example.cpp for guidance.

// INA226value represents a value read from a Texaxs Instruments INA226 High Side DC Current Sensor.

// Pass one of these in the constructor to INA226value() to tell which type of value you want to output
enum INA226ValType { bus_voltage, shunt_voltage, current, power, load_voltage };

// INA226value reads and outputs the specified value of the sensor.
class INA226value : public FloatSensor {
  public:
    INA226value(INA226* pINA226, INA226ValType val_type, uint read_delay = 500, String config_path="");
    void start() override final;
    INA226* pINA226;

  private:
    
    INA226ValType val_type;
    uint read_delay;
    void update();
    virtual void get_configuration(JsonObject& root) override;
    virtual bool set_configuration(const JsonObject& config) override;
    virtual String get_config_schema() override;

};
}
#endif
