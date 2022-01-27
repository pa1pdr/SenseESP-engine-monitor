#ifndef _activity_timer_H_
#define _activity_timer_H_

#include "activity_timer.h"
#include "sensesp/transforms/transform.h"


namespace sensesp {

  /**
   * @brief Sensor determining the cumulative time an input is active high
   *
   * Time the duration to which a GPIO Pin has been high. Typically useful for engine hours,
   * though potentially useful for other puroposes
   *
   *
   * @param[in] config_path Configuration path for the sensor
   *
   * @param[in] start_hrs The start in floating hrs where to start counting. Typically you would use this to 
   * synchronise the value of the activity timer with some physical hour meter
   */
 class ActivityTimer : public FloatTransform {
 public:
  ActivityTimer(float offset, String config_path = "");

  virtual void set_input(float value, uint8_t inputChannel) override;
  virtual void get_configuration(JsonObject& doc) override;
  virtual bool set_configuration(const JsonObject& config) override;
  virtual String get_config_schema() override;

  bool isActive();

 private:
  bool isRunning = false;
  float offset;
  uint read_delay;
  unsigned long last_millis = 0;
  double active_slice = 0.0;


  void persist_offset();
};
} // namespace

#endif
