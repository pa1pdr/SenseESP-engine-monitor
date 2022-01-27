// TODO: Make a transform out of this

#include "activity_timer.h"

#include <Arduino.h>
#include "sensesp.h"
#include "sensesp/transforms/transform.h"

namespace sensesp {

// ActivityTimer

ActivityTimer::ActivityTimer(float offset, String config_path)
    : FloatTransform (config_path), offset{offset} {
  load_configuration();
}


void ActivityTimer::set_input(float value, uint8_t input_channel) {
  if (value > 0.0) {
      isRunning = true;
      double delta = millis() - last_millis; // diff in us since last update

      active_slice += delta / 3600000.0; // add the last delta to the active_slice in hrs
  
    // when we've been running for an hour, update the persisted offset
    if (active_slice >= 1.0) {
      persist_offset();
    }
  } else {
      if (isRunning) {       // we were previously active?
         persist_offset();   // save the last value as offset
       }
     isRunning = false;
  }
 this->emit ((offset + active_slice));                    // tell everyone about it,
 last_millis = millis(); // track time

}


bool ActivityTimer::isActive() {
  return isRunning;
}

void ActivityTimer::persist_offset() {
    offset += active_slice;
    active_slice = 0.0;
    save_configuration ();
}


void ActivityTimer::get_configuration(JsonObject &root) {
   root["start_hrs"] = offset;
};

static const char SCHEMA[] PROGMEM = R"###({
    "type": "object",
    "properties": {
        "start_hrs": { "title": "start_hrs", "type": "number", "description": "Offset in hours to start this ActivityTimer" }
    }
  })###";

String ActivityTimer::get_config_schema() { return FPSTR(SCHEMA); }

bool ActivityTimer::set_configuration(const JsonObject &config) {
  String expected[] = {"start_hrs"};
  for (auto str : expected) {
    if (!config.containsKey(str)) {
      return false;
    }
  }
  offset = config["start_hrs"];
  return true;
};
} // namespace