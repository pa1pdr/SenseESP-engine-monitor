#include "activity_timer.h"

#include <Arduino.h>
#include "sensesp.h"
#include "sensors/sensor.h"
#include "sensors/digital_input.h"
#include "sensors/analog_input.h"
#include "transforms/transform.h"

ActivityTimer::ActivityTimer(double *startV, uint read_delay, String config_path)
    : NumericSensor(config_path),
      read_delay{read_delay} {
  persistStartHrs = startV;
  load_configuration();
}

void ActivityTimer::start() {
  isRunning = true;
}

void ActivityTimer::stop() {
   if (isRunning) {
      persist_start_hrs();    // store the start_hrs
   }
   isRunning = false;
}

bool ActivityTimer::isActive() {
  return isRunning;
}

// we expect to be called every second (1000 millis)
void ActivityTimer::update() {

  if (isActive()) {
    double delta = millis() - last_millis; // diff in us since last update

    if (delta > ((int)(read_delay * 0.9))) {                                      // not initialized // returned from sleep?
      active_slice += delta / 3600000.0; // in hrs
    }
    // when we've been running for an hour, update the flash value too
    if (active_slice >= 1.0) {
      persist_start_hrs();
    }
   }
   
   this->emit ((*persistStartHrs + active_slice)*3600.0);             // tell everyone about it, IN SECONDS..
   last_millis = millis(); // track time

}

void ActivityTimer::persist_start_hrs() {
    *persistStartHrs += active_slice;
    active_slice = 0.0;
}

void ActivityTimer::enable() {
    app.onRepeat(read_delay, [this]()
                 { this->update(); });
                 
}


void ActivityTimer::get_configuration(JsonObject &root) {
  root["read_delay"] = read_delay;
  root["start_hrs"] = *persistStartHrs;
};

static const char SCHEMA[] PROGMEM = R"###({
    "type": "object",
    "properties": {
        "read_delay": { "title": "Read delay", "type": "number", "description": "Number of milliseconds between each update of the timer" },
        "start_hrs": { "title": "start_hrs", "type": "number", "description": "Offset in hours to start this ActivityTimer" }
    }
  })###";

String ActivityTimer::get_config_schema() { return FPSTR(SCHEMA); }

bool ActivityTimer::set_configuration(const JsonObject &config) {
  String expected[] = {"read_delay", "start_hrs"};
  for (auto str : expected) {
    if (!config.containsKey(str)) {
      return false;
    }
  }
  read_delay = config["read_delay"];
  *persistStartHrs = config["start_hrs"];
  return true;
}
