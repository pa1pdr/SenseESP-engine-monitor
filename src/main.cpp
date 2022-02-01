#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <N2kMessages.h>
#include <NMEA2000_esp32.h>
#include <Wire.h>

#include "sensesp_app.h"
#include "sensesp_app_builder.h"
#include "sensesp_minimal_app.h"
#include "sensesp_minimal_app_builder.h"

#include "sensesp_onewire/onewire_temperature.h"
#include "sensesp/signalk/signalk_output.h"
#include "sensesp/sensors/sensor.h"
#include "sensesp/sensors/digital_input.h"
#include "sensesp/transforms/frequency.h"
#include "sensesp/transforms/linear.h"
#include "sensesp/transforms/transform.h"

#include "sensori/activity_timer.h"
#include "sensori/difference.h"
#include "sensori/ina226value.h"
#include "sensori/INA226.h"

#include "sensesp_minimal_app_builder.h"

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

// OLED display width and height, in pixels
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

using namespace sensesp;


// define temperature display units
#define TEMP_DISPLAY_FUNC KelvinToCelsius
//#define TEMP_DISPLAY_FUNC KelvinToFahrenheit

TwoWire *i2c;
Adafruit_SSD1306 *display;

tNMEA2000 *nmea2000;

bool show_display = true;


void scan_i2c(TwoWire *i2c) {

 // Serial.begin(115200); //  LdB this is different to main.cpp
  debugI("I2C Scanner");

  // LdB Set up SCA and SCL lines
  //  int SDA = 4;	// Wemos D1 Mini Pro
  //  int SCL = 5;  // Wemos D1 Mini Pro
  // Wire.begin(SDA, SCL);	// 	Connect the scanner to the connect GPIO pins. 

  // LdB not sure what this does except for just scan 5 times?
  // for(uint8_t n = 0; n < 5; n++) {

    uint8_t error, address;
    int nDevices = 0;

    debugI("Scanning...");

    for(address = 1; address < 127; address++ ) {
    // The i2c_scanner uses the return value of the Write.endTransmisstion
    //  to see if a device did acknowledge to the address.
      i2c->beginTransmission(address);
      error = i2c->endTransmission();

      if (error == 0) {
        debugI("I2C device found at address 0x");
        if (address<16) 
          debugI("0");
        debugI("%x",address);  
        nDevices++;
      }
      else if (error==4) {
        debugI("Unknow error at address 0x");
        if (address<16) 
          debugI("0");
        debugI("%x", address);
      }    
  }
  
    debugI("Finished scanning. ");
    if (nDevices == 0)
      debugI("No I2C devices found");

}


/// Clear a text row on an Adafruit graphics display
void ClearRow(int row) { display->fillRect(0, 8 * row, SCREEN_WIDTH, 8, 0); }

float KelvinToCelsius(float temp) { return temp - 273.15; }

float KelvinToFahrenheit(float temp) { return (temp - 273.15) * 9. / 5. + 32.; }

void PrintValue(int row, String title, float value)
{
    ClearRow(row);
    if (show_display) {
      display->setCursor(0, 8 * row);
      display->printf("%s: %.1f", title.c_str(), value);
      }
    display->display();
}

void PrintTemperature(int row, String title, float temperature)
{
    PrintValue(row, title, TEMP_DISPLAY_FUNC(temperature));
}

double oil_temperature = N2kDoubleNA;
double coolant_temperature = N2kDoubleNA;
double alternator_volts = N2kDoubleNA;
double engine_runtime = N2kDoubleNA;
const String engine = "main";
DigitalInputCounter *rpminput;


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

ReactESP app;

void setup () {
// Some initialization boilerplate when in debug mode...
#ifndef SERIAL_DEBUG_DISABLED
                 SetupSerialDebug(115200);
#endif

  
  SensESPAppBuilder builder;
  sensesp_app = (&builder)
                    // Set a custom hostname for the app.
                    ->set_hostname(engine + " Enginehealth")
                    ->enable_ota ("mypassword")
                    // Optionally, hard-code the WiFi and Signal K server
                    // settings. This is normally not needed.
                      ->get_app();


                   DallasTemperatureSensors *dts = new DallasTemperatureSensors(ONEWIRE_PIN);

                 // define three 1-Wire temperature sensors that update every 1000 ms
                 // and have specific web UI configuration paths

                 auto main_engine_oil_temperature =
                     new OneWireTemperature(dts, 1000, "/" + engine + "EngineOilTemp/oneWire");
                 auto main_engine_coolant_temperature =
                     new OneWireTemperature(dts, 1000, "/" + engine + "EngineCoolantTemp/oneWire");
                 auto main_engine_exhaust_temperature =
                     new OneWireTemperature(dts, 1000, "/" + engine + "EngineWetExhaustTemp/oneWire");
                 auto main_alternator_temperature =
                     new OneWireTemperature(dts, 1000, "/" + engine + "AlternatorTemp/oneWire");

                 // define metadata for sensors

                 auto main_engine_oil_temperature_metadata =
                     new SKMetadata("K",                      // units
                                    engine + " Oil Temperature", // display name
                                    "Engine Oil Temperature", // description
                                    "Oil Temp",               // short name
                                    10.                       // timeout, in seconds
                     );
                 auto main_engine_coolant_temperature_metadata =
                     new SKMetadata("K",                          // units
                                    engine + " Coolant Temperature", // display name
                                    "Engine Coolant Temperature", // description
                                    "Coolant Temp",               // short name
                                    10.                           // timeout, in seconds
                     );
                 auto main_engine_temperature_metadata =
                     new SKMetadata("K",                  // units
                                    engine + " Temperature", // display name
                                    "Engine Temperature", // description
                                    "Engine Temp",        // short name
                                    10.                   // timeout, in seconds
                     );
                 auto main_engine_exhaust_temperature_metadata =
                     new SKMetadata("K",                       // units
                                    engine + " Exhaust Temperature", // display name
                                    "Wet Exhaust Temperature", // description
                                    "Exhaust Temp",            // short name
                                    10.                        // timeout, in seconds
                     );

                 auto main_alternator_temperature_metadata =
                     new SKMetadata("K",                      // units
                                    engine + " Alternator Temperature", // display name
                                    "Alternator Temperature", // description
                                    "Alternator Temp",        // short name
                                    10.                       // timeout, in seconds
                     );

                 auto main_alternator_voltage_metadata =
                     new SKMetadata("V",                         // units
                                    engine + " Alternator Voltage",        // display name
                                    "Alternator Output Voltage", // description
                                    "Alternator V",              // short name
                                    10.                          // timeout, in seconds
                     );

                auto main_alternator_current_metadata =
                     new SKMetadata("A",                         // units
                                    engine + " Alternator Current",        // display name
                                    "Alternator Output Current", // description
                                    "Alternator A",              // short name
                                    10.                          // timeout, in seconds
                     );
                    // Propagate /vessels/<RegExp>/propulsion/<RegExp>/runTime
                    // Units: s (Second)
                    // Description: Total running time for engine (Engine Hours in seconds)
                 auto engine_runtime_metadata =
                     new SKMetadata("s",                       // units
                                    engine + " Engine Runtime", // display name
                                    "Total hrs running", // description
                                    "Engine hrs",            // short name
                                    10.                        // timeout, in seconds
                     );
    
                    
                 auto engine_revs_metadata =
                     new SKMetadata("Hz",                       // units
                                    engine + " Engine speed",   // display name
                                    "Engine revs",              // description
                                    "Speed",                    // short name
                                    10.                        // timeout, in seconds
                     );

                 // connect the sensors to Signal K output paths
                 // Oil temp
                 main_engine_oil_temperature->connect_to(new SKOutput<float>(
                     "propulsion." + engine + ".oilTemperature", main_engine_oil_temperature_metadata));
                 // Coolant temp
                 main_engine_coolant_temperature->connect_to(new SKOutput<float>(
                     "propulsion." + engine + ".coolantTemperature", main_engine_coolant_temperature_metadata));
                 // transmit coolant temperature as overall engine temperature as well
                 main_engine_coolant_temperature->connect_to(new SKOutput<float>(
                     "propulsion." + engine + ".temperature", main_engine_temperature_metadata));
                 // propulsion.*.ExhaustTemperature is a standard path: /vessels/<RegExp>/propulsion/<RegExp>/exhaustTemperature
                 main_engine_exhaust_temperature->connect_to(
                     new SKOutput<float>("propulsion." + engine +".exhaustTemperature", main_engine_exhaust_temperature_metadata));

                 // send the alternator temperature /vessels/<RegExp>/electrical/alternators/<RegExp>/temperature
                 main_alternator_temperature->connect_to(
                     new SKOutput<float>("electrical." + engine + ".alternators.temperature", main_alternator_temperature_metadata));


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

                // put the hostname on display
                app.onRepeat (500U,[](){
                    if (show_display) {
                      display->setCursor(0, 0);
                      display->printf("%s", sensesp_app->get_hostname().c_str());
                    }
                });

                // if the BOOT button is pressed, activate the display for 10 seconds
                auto *dispButton = new DigitalInputChange (BOOT_BUTTON, PULLDOWN,CHANGE,"");
                dispButton->connect_to(new LambdaConsumer<bool>([](bool btnstate)
                                                            {
                                                                if (!btnstate) {
                                                                    show_display = true;
                                                                    app.onDelay (10U*1000U,[](){
                                                                        show_display = false;
                                                                        display->clearDisplay();
                                                                    });

                                                                }
                                                              }));


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
                 auto *dic = new DigitalInputCounter(rpm_pin, INPUT_PULLUP, RISING, 200U);  // fast changing parameter
                 dic
                     ->connect_to(new Frequency(rpm_multiplier/60.0, "/" + engine + "_engine_rpm/calibrate"))
                     ->connect_to(new SKOutputFloat ("propulsion." + engine + ".revolutions", engine_revs_metadata));

                 // Send the RPM's to the display
                 dic
                     ->connect_to(new Frequency(rpm_multiplier))
                     ->connect_to(new LambdaConsumer<float>([](float rpm)
                                                            { 
                                                            PrintValue(6, "RPM", rpm); }));

                 // Send the RPM's to the N2K network
                 // note N2K expects Engine Speed to be the rotational speed of the engine in units of 1/4 RPM.
                 dic
                     ->connect_to(new Frequency(rpm_multiplier))
                     ->connect_to(new LambdaConsumer<float>([](float rpm)
                                                            {
                                                                tN2kMsg N2kMsg;
                                                                SetN2kEngineParamRapid(N2kMsg,
                                                                                       1, // SID
                                                                                       rpm * 4.0);

                                                            }));
                // Update the hour meter for this engine and add to the startvalue
                auto *main_engine_timer = new ActivityTimer(1.0,"/" + engine + "_engine_hrs/begin_value");

                dic
                  ->connect_to (main_engine_timer)
                  ->connect_to (new LambdaConsumer<float>([](float running_hrs)
                                                            { 
                                                              engine_runtime = running_hrs;
                                                              PrintValue(7, "Hours", engine_runtime);
                                                            }));

                main_engine_timer
                      ->connect_to (new Linear (3600.0,0.0,""))
                      ->connect_to (new SKOutputFloat("propulsion." + engine + ".runTime", engine_runtime_metadata));                 
                
                // start the INA266 current & voltage measurements for the alternator

                auto *alternatorINA = new INA226 (i2c);
                alternatorINA->begin(0x40);  // uses the default address of 0x40
 
                // configure with defaults
    
                alternatorINA->configure(INA226_AVERAGES_1, INA226_BUS_CONV_TIME_1100US, INA226_SHUNT_CONV_TIME_1100US, INA226_MODE_SHUNT_BUS_CONT);  // uses the default values
                alternatorINA->calibrate(0.01, 4);  // uses the default values
                // Now the INA226 is ready for reading, which will be done by the INA226value class.
                auto altVmeter = new INA226value (alternatorINA,bus_voltage,1000U,"/" + engine + "_Alternator/Electrics/Voltage");
                debugD ("we have a voltmeter");

   
                altVmeter->connect_to (new SKOutputFloat("electrical.alternators." + engine + ".voltage", main_alternator_voltage_metadata));

                altVmeter->connect_to (new LambdaConsumer<float>([](float altV)
                                                           {   debugD ("Alternator volts: %f V",altV);
                                                               PrintValue (2,"AltV",altV); }));
                auto altAmmeter = new INA226value (alternatorINA,current,1000U,"/" + engine + "_Alternator/Electrics/Current");
                debugD ("we have an Ammeter");

   
                altAmmeter->connect_to (new SKOutputFloat("electrical.alternators." + engine + ".current", main_alternator_current_metadata));

                altAmmeter->connect_to (new LambdaConsumer<float>([](float altA)
                                                           {   debugD ("Alternator amps: %f A",altA);
                                                               PrintValue (3,"AltA",altA); }));

 
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

                sensesp_app->start();
             }



             void loop() {
                 app.tick();
             }
    

