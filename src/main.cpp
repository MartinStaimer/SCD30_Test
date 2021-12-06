#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include "bsec.h"
#include <SPS30.h>
#include "SparkFun_SCD30_Arduino_Library.h"

SCD30 airSensor;
SPS30 Sensor;

const uint8_t bsec_config_iaq[] = { 
  #include "config/generic_33v_3s_4d/bsec_iaq.txt" 
};

#define STATE_SAVE_PERIOD	UINT32_C(360 * 60 * 1000) // 360 minutes - 4 times a day

// Helper functions declarations
void checkIaqSensorStatus(void);
void errLeds(void);
void loadState(void);
void updateState(void);

// Create an object of the class Bsec
Bsec iaqSensor;
uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE] = {0};
uint16_t stateUpdateCounter = 0;

String output;
int count;

void setup()
{
  EEPROM.begin(BSEC_MAX_STATE_BLOB_SIZE + 1); // 1st address for the length
  Serial.begin(9600);
  Wire.begin(); // Start the wire hardware that may be supported by your platform

  // SCD30 Setup
  Serial.println("SCD30 Example");
  if (airSensor.begin(Wire) == false) // Pass the Wire port to the .begin() function
  {
    Serial.println("Air sensor not detected. Please check wiring. Freezing...");
    while (1)
      ;
  }

  uint16_t settingVal; // The settings will be returned in settingVal

  if (airSensor.getForcedRecalibration(&settingVal) == true) // Get the setting
  {
    Serial.print("Forced recalibration factor (ppm) is ");
    Serial.println(settingVal);
  }

  if (airSensor.getMeasurementInterval(&settingVal) == true) // Get the setting
  {
    Serial.print("Measurement interval (s) is ");
    Serial.println(settingVal);
  }

  if (airSensor.getTemperatureOffset(&settingVal) == true) // Get the setting
  {
    Serial.print("Temperature offset (C) is ");
    Serial.println(((float)settingVal) / 100.0, 2);
  }

  airSensor.setAltitudeCompensation(427);

  // BME680 Setup
  iaqSensor.begin(0x77, Wire);
  output = "\nBSEC library version " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
  Serial.println(output);
  checkIaqSensorStatus();

  Serial.println("debugg _ 1");

  iaqSensor.setConfig(bsec_config_iaq);
  checkIaqSensorStatus();

  Serial.println("debugg _ 2");

  loadState();

  bsec_virtual_sensor_t sensorList1[2] = {
      BSEC_OUTPUT_RAW_GAS,
      BSEC_OUTPUT_IAQ,
  };

  iaqSensor.updateSubscription(sensorList1, 2, BSEC_SAMPLE_RATE_ULP);
  checkIaqSensorStatus();

  Serial.println("debugg _ 3");

  /*
  bsec_virtual_sensor_t sensorList2[5] = {
      BSEC_OUTPUT_RAW_TEMPERATURE,
      BSEC_OUTPUT_RAW_PRESSURE,
      BSEC_OUTPUT_RAW_HUMIDITY,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
      BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
  };

  iaqSensor.updateSubscription(sensorList2, 5, BSEC_SAMPLE_RATE_LP);
  checkIaqSensorStatus();
  */

  bsec_virtual_sensor_t sensorList[10] = {
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
  };

  iaqSensor.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);
  checkIaqSensorStatus();


  Serial.println("debugg _ 4");

  // SPS30 Setup
  Sensor.begin(Wire);

  //Init
  count = 0;
}

void loop()
{
  float tempSCD30;
  float tempBME680;
  float offsetTemp;

  

  //SCD30 Region
  if (airSensor.dataAvailable())
  {
    Serial.print("co2(ppm):");
    Serial.print(airSensor.getCO2());
    
    tempSCD30 = airSensor.getTemperature();

    Serial.print(" temp(C):");
    Serial.print(tempSCD30, 1);

    Serial.print(" humidity(%):");
    Serial.print(airSensor.getHumidity(), 1);

    Serial.println();
  }
  else
  {
    Serial.print(".");
  }

  //BME680 Region
  unsigned long time_trigger = millis();
  if (iaqSensor.run())
  { // If new data is available#
    // Print the header
    output = "Sensor, Timestamp [ms], raw temperature [°C], pressure [hPa], raw relative humidity [%], gas [Ohm], IAQ, IAQ accuracy, temperature [°C], relative humidity [%], Co2, Co2 Accuray, breath VOC equivalent";
    Serial.println(output);

    tempBME680 = iaqSensor.temperature;

    output = String(time_trigger);
    output += ", " + String(iaqSensor.rawTemperature);
    output += ", " + String(iaqSensor.pressure);
    output += ", " + String(iaqSensor.rawHumidity);
    output += ", " + String(iaqSensor.gasResistance);
    output += ", " + String(iaqSensor.iaq);
    output += ", " + String(iaqSensor.iaqAccuracy);
    output += ", " + String(iaqSensor.temperature);
    output += ", " + String(iaqSensor.humidity);
    output += ", " + String(iaqSensor.co2Equivalent);
    output += ", " + String(iaqSensor.co2Accuracy);
    output += ", " + String(iaqSensor.breathVocEquivalent);
    Serial.println(output);
    updateState();
  }
  else
  {
    checkIaqSensorStatus();
  }

  //SPS30 Region
  if (Sensor.dataAvailable())
  {
    float mass_concen[4];
    Sensor.getMass(mass_concen);

    float num_concen[5];
    Sensor.getNum(num_concen);

    
    char *pm[5] = {"PM0.5", "PM1.0", "PM2.5", "PM4.0", "PM10"};

    Serial.println("--- Mass Concentration ---");

    for (int i = 0; i < 4; i++)
    {
      Serial.printf("%s: %.2f\n", pm[i + 1], mass_concen[i]);
    }

    Serial.println("--- Number Concentration ---");

    for (int i = 0; i < 5; i++)
    {
      Serial.printf("%s: %.2f\n", pm[i], num_concen[i]);
    }

    Serial.println(Sensor.typPartSize);
  }

  //Info
  offsetTemp = tempSCD30-tempBME680;
  Serial.println("Offset zwischen BME und SCD");
  Serial.println(String(offsetTemp));

  //Set Temp Offset to SCD30 every 20 Zykles
  if (count == 20 && offsetTemp > 0)
  {
    airSensor.setTemperatureOffset(offsetTemp);
    Serial.println("Set offset");
    Serial.println("Offset from SCD30");
    Serial.println(String(airSensor.getTemperatureOffset()));
    count = 0;
  }

  delay(3000);
  count++;
}

// Helper function definitions
void checkIaqSensorStatus(void)
{
  if (iaqSensor.status != BSEC_OK) {
    if (iaqSensor.status < BSEC_OK) {
      output = "BSEC error code : " + String(iaqSensor.status);
      Serial.println(output);
      for (;;)
        errLeds(); /* Halt in case of failure */
    } else {
      output = "BSEC warning code : " + String(iaqSensor.status);
      Serial.println(output);
    }
  }


  if (iaqSensor.bme680Status != BME680_OK) {
    if (iaqSensor.bme680Status < BME680_OK) {
      output = "BME680 error code : " + String(iaqSensor.bme680Status);
      Serial.println(output);
      for (;;)
        errLeds();
    } else {
      output = "BME680 warning code : " + String(iaqSensor.bme680Status);
      Serial.println(output);
    }
  }


}

void errLeds(void)
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
}

void loadState(void)
{
  if (EEPROM.read(0) == BSEC_MAX_STATE_BLOB_SIZE) {
    // Existing state in EEPROM
    Serial.println("Reading state from EEPROM");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++) {
      bsecState[i] = EEPROM.read(i + 1);
      Serial.println(bsecState[i], HEX);
    }

    iaqSensor.setState(bsecState);
    checkIaqSensorStatus();
  } else {
    // Erase the EEPROM with zeroes
    Serial.println("Erasing EEPROM");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE + 1; i++)
      EEPROM.write(i, 0);

    EEPROM.commit();
  }
}

void updateState(void)
{
  bool update = false;
  if (stateUpdateCounter == 0) {
    /* First state update when IAQ accuracy is >= 3 */
    if (iaqSensor.iaqAccuracy >= 3) {
      update = true;
      stateUpdateCounter++;
    }
  } else {
    /* Update every STATE_SAVE_PERIOD minutes */
    if ((stateUpdateCounter * STATE_SAVE_PERIOD) < millis()) {
      update = true;
      stateUpdateCounter++;
    }
  }

  if (update) {
    iaqSensor.getState(bsecState);
    checkIaqSensorStatus();

    Serial.println("Writing state to EEPROM");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE ; i++) {
      EEPROM.write(i + 1, bsecState[i]);
      Serial.println(bsecState[i], HEX);
    }

    EEPROM.write(0, BSEC_MAX_STATE_BLOB_SIZE);
    EEPROM.commit();
  }
}