/*
  Utilizing code from the Adafruit BNO08x Arduino Library by Bryan Siepert 
  for Adafruit Industries. Found here:
  https://github.com/adafruit/Adafruit_BNO08x

  License:
  Written by Bryan Siepert for Adafruit Industries. 
  MIT license, check license.txt for more information.
  All text above must be included in any redistribution.

  Note: Text above is the complete README.md of the repository.
  I added a copy of the readme to doc/Adafruit_BNO08x/README.md. 
*/

#ifdef __cplusplus
extern "C" {
#endif

#include <sh2/sh2.h>
#include <sh2/sh2_SensorValue.h>

bool bno08x_init(sh2_ProductIds_t *prodIds);
bool bno08x_enableReport(sh2_SensorId_t sensorId, uint32_t interval_us);
bool bno08x_wasReset(void);
bool bno08x_getSensorEvent(sh2_SensorValue_t *value);

#ifdef __cplusplus
}
#endif