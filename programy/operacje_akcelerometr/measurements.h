#ifndef MEASUREMENTS
#define MEASUREMENTS

#include "SparkFun_LIS331.h"
#include <L3G4200D.h>
#include "circularBuffer.h"
#include "normalize.h"

class measurements
{
  L3G4200D gyroscope;
  LIS331 xl;
  
  //czas pomiedzy pomiarami
  unsigned long lastTime = 0;
  //pomiary z czujnikow
  TripleF  readGyro();
    TripleF readAccel();
    TripleF convertItoF(Triple source);
  
  public:
  //aktywyny pomiar w petli
//czas poprzedniego pomiaru
  Sample realMeasure();

  measurements()
  {
            // Initialize L3G4200D - gyroscope
      // Set scale 2000 dps and 400Hz ODR (cut-off 50Hz)
      //czekamy az begin zwroci true
      while (!gyroscope.begin(L3G4200D_SCALE_2000DPS, L3G4200D_DATARATE_400HZ_50)) {
         
          delay(100); // krótsza przerwa, szybciej reaguje
      }


      //swiecimy podczas konfiguracji
      // Optional: calibrate gyro (must be at rest)
      gyroscope.calibrate(100);

 


      //konfiguracja akcelerometru
      xl.setI2CAddr(0x19);    // must come before .begin()
      xl.begin(LIS331::USE_I2C);  // selects I2C mode, no return value
  }

};





#endif