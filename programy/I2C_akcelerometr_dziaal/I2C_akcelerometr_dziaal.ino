#include "SparkFun_LIS331.h"
#include <Wire.h>

LIS331 xl;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);     // specify SDA, SCL for ESP32

  xl.setI2CAddr(0x19);    // must come before .begin()
  xl.begin(LIS331::USE_I2C);  // selects I2C mode, no return value

  Serial.println("LIS331 initialized (I2C mode).");
}

void loop() {
  int16_t x, y, z;
  xl.readAxes(x, y, z);
  Serial.print("X: "); Serial.print(x);
  Serial.print("  Y: "); Serial.print(y);
  Serial.print("  Z: "); Serial.println(z);
  delay(100);
}