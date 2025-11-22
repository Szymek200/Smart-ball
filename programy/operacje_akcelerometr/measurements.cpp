#include "measurements.h"


Sample measurements::realMeasure()
{
  

     unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0; // convert ms → s
    lastTime = now;

    TripleF gyro = readGyro();
    TripleF accel = readAccel();
    TripleF worldAccel;

    worldAccel = updateWorldAccel(gyro, accel, dt);

    Sample result;
    result.accel = worldAccel;
    result.gyro = gyro;
    result.timestamp = now;
  return result;
}

//pomiary z czujnikow

  TripleF  measurements::readGyro()
  {
      TripleF katy;
  // Read normalized values
      Vector norm = gyroscope.readNormalize();
 
      katy.x = norm.XAxis * DEG_TO_RAD;
      katy.y = norm.YAxis * DEG_TO_RAD;
      katy.z = norm.ZAxis * DEG_TO_RAD;

      return katy;
  }


    TripleF measurements::readAccel()
    {
      Triple curData;
        xl.readAxes(curData.x, curData.y, curData.y);
      return   convertItoF(curData);
    }

    TripleF measurements::convertItoF(Triple source)
    {
      TripleF result;
      result.x = static_cast<float>(source.x);
      result.y = static_cast<float>(source.y);
      result.z = static_cast<float>(source.z);

      return result;
    }