#ifndef _activity_timer_H_
#define _activity_timer_H_

#include "activity_timer.h"
#include "sensors/sensor.h"
#include "sensors/digital_input.h"

  /**
   * @brief Sensor determining the cumulative time an input is active high
   *
   * Time the duration to which a GPIO Pin has been high. Typically useful for engine hours,
   * though potentially useful for other puroposes
   *
   * 
   * @param[in] resolution Time delay between consecutive readings, in seconds
   *
   * @param[in] config_path Configuration path for the sensor
   *
   * @param[in] start_hrs The start in floating hrs where to start counting. Typically you would use this to 
   * synchronise the value of the activity timer with some physical hour meter
   */
 class ActivityTimer : public NumericSensor {
 public:
  ActivityTimer(double *startV, uint read_delay = 1000U, String config_path = "");
  void enable() override final;

  void start();
  void stop();
  bool isActive();

 private:
  bool isRunning = false;
  uint read_delay;
  unsigned long last_millis = 0;
  double active_slice = 0.0;
  double *persistStartHrs;
  
  virtual void get_configuration(JsonObject& doc) override;
  virtual bool set_configuration(const JsonObject& config) override;
  virtual String get_config_schema() override;
  void update();
  void persist_start_hrs();
};

#endif
