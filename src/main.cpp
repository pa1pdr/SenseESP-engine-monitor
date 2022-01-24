#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <N2kMessages.h>
#include <NMEA2000_esp32.h>
#include <Wire.h>

#include "sensesp_app.h"
#include "sensesp_app_builder.h"
#include "sensors/onewire_temperature.h"
#include "signalk/signalk_output.h"
#include "sensors/sensor.h"
#include "sensors/digital_input.h"
#include "sensors/activity_timer.h"
#include "transforms/frequency.h"
#include "transforms/linear.h"
#include "transforms/transform.h"

// 1-Wire data pin on SH-ESP32
#define ONEWIRE_PIN 4

// SDA and SCL pins on SH-ESP32
#define SDA_PIN 16
#define SCL_PIN 17

// CAN bus (NMEA 2000) pins on SH-ESP32
#define CAN_RX_PIN GPIO_NUM_34
#define CAN_TX_PIN GPIO_NUM_32

// IO Pins we'd like to use
//
#define BOOT_BUTTON 0  // GPIO 0
#define ADC_VOLTAGE 36 // this pin will be the Alternator voltage


// OLED display width and height, in pixels
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64



// define temperature display units
#define TEMP_DISPLAY_FUNC KelvinToCelsius
//#define TEMP_DISPLAY_FUNC KelvinToFahrenheit

TwoWire *i2c;
Adafruit_SSD1306 *display;

tNMEA2000 *nmea2000;

/// Clear a text row on an Adafruit graphics display
void ClearRow(int row) { display->fillRect(0, 8 * row, SCREEN_WIDTH, 8, 0); }

float KelvinToCelsius(float temp) { return temp - 273.15; }

float KelvinToFahrenheit(float temp) { return (temp - 273.15) * 9. / 5. + 32.; }

void PrintValue(int row, String title, float value)
{
    ClearRow(row);
    display->setCursor(0, 8 * row);
    display->printf("%s: %.1f", title.c_str(), value);
    display->display();
}

void PrintTemperature(int row, String title, float temperature)
{
    PrintValue(row, title, TEMP_DISPLAY_FUNC(temperature));
}

double oil_temperature = N2kDoubleNA;
double coolant_temperature = N2kDoubleNA;
double alternator_volts = N2kDoubleNA;
double engine_runtime;
unsigned long last_millis = 0;
unsigned long delta;
float engine_revs;
ActivityTimer *engineRuntimeSensor = NULL; 



/**
 * @brief Send Engine Dynamic Parameter data
 *
 * Send engine temperature data using the Engine Dynamic Parameter PGN.
 * All unused fields that are sent with undefined value except the status
 * bit fields are sent as zero. Hopefully we're not resetting anybody's engine
 * warnings...
 */
void SendEngineTemperatures()
{
    tN2kMsg N2kMsg;
    SetN2kEngineDynamicParam(N2kMsg,
                             0,           // instance of a single engine is always 0
                             N2kDoubleNA, // oil pressure
                             oil_temperature, coolant_temperature,
                             alternator_volts, // alternator voltage
                             N2kDoubleNA,      // fuel rate
                             engine_runtime,     // engine hours
                             N2kDoubleNA,      // engine coolant pressure
                             N2kDoubleNA,      // engine fuel pressure
                             N2kInt8NA,        // engine load
                             N2kInt8NA,        // engine torque
                             (tN2kEngineDiscreteStatus1)0,
                             (tN2kEngineDiscreteStatus2)0);
    nmea2000->SendMsg(N2kMsg);
}

ReactESP app([]()
             {
// Some initialization boilerplate when in debug mode...
#ifndef SERIAL_DEBUG_DISABLED
                 SetupSerialDebug(115200);
#endif

                 SensESPAppBuilder builder;

                 sensesp_app = new SensESPApp("enginehealth", "Zevecote",
                                              "1023bm_cafebabe_", "192.168.1.133", 3000);

                 //  sensesp_app = builder.set_hostname("temperatures")
                 //                   ->set_standard_sensors(NONE)
                 //                    ->get_app();

                 DallasTemperatureSensors *dts = new DallasTemperatureSensors(ONEWIRE_PIN);

                 // define three 1-Wire temperature sensors that update every 1000 ms
                 // and have specific web UI configuration paths

                 auto main_engine_oil_temperature =
                     new OneWireTemperature(dts, 1000, "/mainEngineOilTemp/oneWire");
                 auto main_engine_coolant_temperature =
                     new OneWireTemperature(dts, 1000, "/mainEngineCoolantTemp/oneWire");
                 auto main_engine_exhaust_temperature =
                     new OneWireTemperature(dts, 1000, "/mainEngineWetExhaustTemp/oneWire");
                 auto main_alternator_temperature =
                     new OneWireTemperature(dts, 1000, "/mainAlternatorTemp/oneWire");

                 // define metadata for sensors

                 auto main_engine_oil_temperature_metadata =
                     new SKMetadata("K",                      // units
                                    "Engine Oil Temperature", // display name
                                    "Engine Oil Temperature", // description
                                    "Oil Temp",               // short name
                                    10.                       // timeout, in seconds
                     );
                 auto main_engine_coolant_temperature_metadata =
                     new SKMetadata("K",                          // units
                                    "Engine Coolant Temperature", // display name
                                    "Engine Coolant Temperature", // description
                                    "Coolant Temp",               // short name
                                    10.                           // timeout, in seconds
                     );
                 auto main_engine_temperature_metadata =
                     new SKMetadata("K",                  // units
                                    "Engine Temperature", // display name
                                    "Engine Temperature", // description
                                    "Engine Temp",        // short name
                                    10.                   // timeout, in seconds
                     );
                 auto main_engine_exhaust_temperature_metadata =
                     new SKMetadata("K",                       // units
                                    "Wet Exhaust Temperature", // display name
                                    "Wet Exhaust Temperature", // description
                                    "Exhaust Temp",            // short name
                                    10.                        // timeout, in seconds
                     );

                 auto main_alternator_temperature_metadata =
                     new SKMetadata("K",                      // units
                                    "Alternator Temperature", // display name
                                    "Alternator Temperature", // description
                                    "Alternator Temp",        // short name
                                    10.                       // timeout, in seconds
                     );

                 auto main_alternator_voltage_metadata =
                     new SKMetadata("K",                         // units
                                    "Alternator Voltage",        // display name
                                    "Alternator Output Voltage", // description
                                    "Alternator V",              // short name
                                    10.                          // timeout, in seconds
                     );

                    // Propagate /vessels/<RegExp>/propulsion/<RegExp>/runTime
                    // Units: s (Second)
                    // Description: Total running time for engine (Engine Hours in seconds)
                 auto main_engine_runtime_metadata =
                     new SKMetadata("s",                       // units
                                    "Engine Runtime", // display name
                                    "Total running time main engine", // description
                                    "Engine running",            // short name
                                    10.                        // timeout, in seconds
                     );
    

                 // connect the sensors to Signal K output paths
                 // Oil temp
                 main_engine_oil_temperature->connect_to(new SKOutput<float>(
                     "propulsion.main.oilTemperature", "/mainEngineOilTemp/skPath",
                     main_engine_oil_temperature_metadata));
                 // Coolant temp
                 main_engine_coolant_temperature->connect_to(new SKOutput<float>(
                     "propulsion.main.coolantTemperature", "/mainEngineCoolantTemp/skPath",
                     main_engine_coolant_temperature_metadata));
                 // transmit coolant temperature as overall engine temperature as well
                 main_engine_coolant_temperature->connect_to(new SKOutput<float>(
                     "propulsion.main.temperature", "/mainEngineTemp/skPath",
                     main_engine_temperature_metadata));
                 // propulsion.*.ExhaustTemperature is a standard path: /vessels/<RegExp>/propulsion/<RegExp>/exhaustTemperature
                 main_engine_exhaust_temperature->connect_to(
                     new SKOutput<float>("propulsion.main.exhaustTemperature",
                                         "/mainEngineExhaustTemp/skPath",
                                         main_engine_exhaust_temperature_metadata));

                 // send the alternator temperature /vessels/<RegExp>/electrical/alternators/<RegExp>/temperature
                 main_alternator_temperature->connect_to(new SKOutput<float>(
                     "electrical.main.alternators.temperature", "/mainAlternatorTemp/skPath",
                     main_alternator_temperature_metadata));

                

                 // initialize the display
                 i2c = new TwoWire(0);
                 i2c->begin(SDA_PIN, SCL_PIN);
                 display = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, i2c, -1);
                 if (!display->begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
                     Serial.println(F("SSD1306 allocation failed"));
                 }
                 delay(100);
                 display->setRotation(2);
                 display->clearDisplay();
                 display->setTextSize(1);
                 display->setTextColor(SSD1306_WHITE);
                 display->setCursor(0, 0);
                 display->printf("Host: %s\n", sensesp_app->get_hostname().c_str());



                 // RPM Measurement down here, we expect a RPM proportional signal (the generator
                 // W output) and connected to the ESP HAT board opto coupler input GPIO 35
                 // We'll also start the activity timer when RPM's > 0
                 //
                 // The crank pulley diameter is 127.86mm
                 // The alternator pulley diameter is 58mm
                 // Ration of 2.2044 to 1
                 // 12 pole alternator, 6 pairs of poles
                 // Pulses per rev= number of pairs of poles x the pulley ratio.
                 // 6 x 2.2044 = 13.23 pulse per rev, for reference the standard number of pulses per rev for the original alternator is 10.29
                 // This convert to frequencies of:
                 // 220 Hz at 1,000 rpm; 440 HZ at 2,000rpm and 660Hz at 3000 rpm
                 
                 const float rpm_multiplier = 60.0 / 13.23;  // in Hz
                 uint8_t rpm_pin = 35;
                 // SignalK wants it in Hz, so divide by 60..
                 auto *dic = new DigitalInputCounter(rpm_pin, INPUT_PULLUP, RISING, 500U);
                 dic
                     ->connect_to(new Frequency(rpm_multiplier/60.0, "/sensors/engine_rpm/calibrate"))
                     ->connect_to(new SKOutputNumber("propulsion.main.revolutions", new SKMetadata("Hz", "rpm")));

                 // Send the RPM's to the display
                 dic
                     ->connect_to(new Frequency(rpm_multiplier))
                     ->connect_to(new LambdaConsumer<float>([](float rpm)
                                                            { engine_revs = rpm;
                                                            PrintValue(6, "RPM", rpm); }));

                 // Send the RPM's to the N2K network
                 dic
                     ->connect_to(new Frequency(rpm_multiplier))
                     ->connect_to(new LambdaConsumer<float>([](float rpm)
                                                            {
                                                                tN2kMsg N2kMsg;
                                                                SetN2kEngineParamRapid(N2kMsg,
                                                                                       1, // SID
                                                                                       rpm);

                                                            }));


                 // Engine hour meter: we update every second using the cofigurable offset
                 engineRuntimeSensor = new ActivityTimer (&engine_runtime, 1000U,"/mainEngineRuntime/configure");

                 app.onRepeat(100, []()
                              { if (engine_revs >= 0)       //CHANGE ME TO > !!
                                    engineRuntimeSensor->start();
                                else
                                     engineRuntimeSensor->stop();
                              });
                 
                 engineRuntimeSensor->connect_to(new SKOutputNumber("propulsion.main.runTime", new SKMetadata("s", "runtime")));                 
                
                 
                 engineRuntimeSensor->connect_to (new LambdaConsumer<float>([](float enginesecs) {   
                        float runtime = enginesecs / 3600.0;
                        if (engineRuntimeSensor->isActive()) {
                            PrintValue (7,"Engine hrs",runtime);
                        } else {
                            PrintValue (7,"engine hrs",runtime);  // make a small visual distinction
                        }
                 }));



                 // Add display updaters for temperature values
                 main_engine_oil_temperature->connect_to(new LambdaConsumer<float>(
                     [](float temperature)
                     { PrintTemperature(1, "Oil", temperature); }));
                 main_engine_coolant_temperature->connect_to(new LambdaConsumer<float>(
                     [](float temperature)
                     { PrintTemperature(2, "Coolant", temperature); }));
                 main_engine_exhaust_temperature->connect_to(new LambdaConsumer<float>(
                     [](float temperature)
                     { PrintTemperature(3, "Exhaust", temperature); }));
                 main_alternator_temperature->connect_to(new LambdaConsumer<float>(
                     [](float temperature)
                     { PrintTemperature(4, "Alternator", temperature); }));

                 // initialize the NMEA 2000 subsystem

                 // instantiate the NMEA2000 object
                 nmea2000 = new tNMEA2000_esp32(CAN_TX_PIN, CAN_RX_PIN);

                 // Reserve enough buffer for sending all messages. This does not work on small
                 // memory devices like Uno or Mega
                 nmea2000->SetN2kCANSendFrameBufSize(250);
                 nmea2000->SetN2kCANReceiveFrameBufSize(250);

                 // Set Product information
                 nmea2000->SetProductInformation(
                     "20210405",             // Manufacturer's Model serial code (max 32 chars)
                     103,                    // Manufacturer's product code
                     "SH-ESP32 Temp Sensor", // Manufacturer's Model ID (max 33 chars)
                     "0.1.0.0 (2021-04-05)", // Manufacturer's Software version code (max 40
                                             // chars)
                     "0.0.3.1 (2021-03-07)"  // Manufacturer's Model version (max 24 chars)
                 );
                 // Set device information
                 nmea2000->SetDeviceInformation(
                     1,   // Unique number. Use e.g. Serial number.
                     130, // Device function=Analog to NMEA 2000 Gateway. See codes on
                          // http://www.nmea.org/Assets/20120726%20nmea%202000%20class%20&%20function%20codes%20v%202.00.pdf
                     75,  // Device class=Inter/Intranetwork Device. See codes on
                          // http://www.nmea.org/Assets/20120726%20nmea%202000%20class%20&%20function%20codes%20v%202.00.pdf
                     2046 // Just choosen free from code list on
                          // http://www.nmea.org/Assets/20121020%20nmea%202000%20registration%20list.pdf
                 );

                 nmea2000->SetMode(tNMEA2000::N2km_NodeOnly, 22);
                 // Disable all msg forwarding to USB (=Serial)
                 nmea2000->EnableForward(false);
                 nmea2000->Open();

                 // No need to parse the messages at every single loop iteration; 10 ms will do
                 app.onRepeat(10, []()
                              { nmea2000->ParseMessages(); });

                 // Implement the N2K PGN sending. Engine (oil) temperature and coolant
                 // temperature are a bit more complex because they're sent together
                 // as part of a Engine Dynamic Parameter PGN.

                 main_engine_oil_temperature->connect_to(
                     new LambdaConsumer<float>([](float temperature)
                                               {
                                                   oil_temperature = temperature;
                                                   SendEngineTemperatures();
                                               }));
                 main_engine_coolant_temperature->connect_to(
                     new LambdaConsumer<float>([](float temperature)
                                               {
                                                   coolant_temperature = temperature;
                                                   SendEngineTemperatures();
                                               }));
                 // hijack the exhaust gas temperature for wet exhaust temperature
                 // measurement
                 main_engine_exhaust_temperature->connect_to(
                     new LambdaConsumer<float>([](float temperature)
                                               {
                                                   tN2kMsg N2kMsg;
                                                   SetN2kTemperature(N2kMsg,
                                                                     1,                           // SID
                                                                     2,                           // TempInstance
                                                                     N2kts_ExhaustGasTemperature, // TempSource
                                                                     temperature                  // actual temperature
                                                   );

                                                   // Set the alternator measurement, note there is no place in ny message on egine
                                                   // an alternative could be to use PGN130312 'Temperature as measured by a specific temperature source'
                                                   // TODO

                                                   nmea2000->SendMsg(N2kMsg);
                                               }));

                 sensesp_app->enable();
             });