#include <cmath>
#include <Arduino.h>
#include "MQ135.h"

// Temperature and humidity influence
// RH = 33%
// f1(-10) = 1.708
// f1(4.9) = 1.251
// f1(20) = 1.0
// f1(50) = 0.917

// RH = 85%
// f2(-10) = 1.533
// f2(4.9) = 1.138
// f2(20) = 0.913
// f2(50) = 0.821

MQ135::MQ135(int port) : MQ(port, 75.f, 75.f)
{
}

float MQ135::HaTFunction(float t, int rh)
{
  if (rh != 33 & rh != 85)
    return 0;

  float const epsilon = 0.0001f;
  int const points_length = 4;
  float points[points_length] = {-10.f, 4.9f, 20.f, 50.f};
  int conditions = 0;
  for (int i = 0; i < points_length; i++)
    conditions += i == points_length - 1 ? int(t > points[i] + epsilon) : int(t > points[i] - epsilon);

  switch (conditions)
  {
  case 0:
    return infinityf();
  case 1:
    return rh == 33 ? linearFunction(1.251f, 1.708f, 4.9f, -10.f, t) : linearFunction(1.138f, 1.533f, 4.9f, -10.f, t);
  case 2:
    return rh == 33 ? linearFunction(1.f, 1.251f, 20.f, 4.9f, t) : linearFunction(0.913f, 1.138f, 20.f, 4.9f, t);
  case 3:
    return rh == 33 ? linearFunction(0.917f, 1.f, 50.f, 20.f, t) : linearFunction(0.821f, 0.913f, 50.f, 20.f, t);
  case 4:
    return -infinityf();
  }
  return -1;
}

float MQ135::getValue(float temperature, float humidity)
{
  float y = getResistance() / R0 * getHaTCoefficient(temperature, humidity);
  float const epsilon = 0.0001;
  if (y > 0.8 - epsilon)
  {
    if (y > 1.07 - epsilon)
    {
      if (y > 2.45 + epsilon)
        return -infinityf();
      else
        return getPercent(100.f, 10.f, 1.07f, 2.45f, y);
    }
    else
      return getPercent(200.f, 100.f, 0.8f, 1.07f, y);
  }
  else
    return infinityf();
}