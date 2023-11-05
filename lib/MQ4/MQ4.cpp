#include <cmath>
#include <Arduino.h>
#include "MQ4.h"

// Temperature and humidity influence
// RH = 33%
// f1(-10) = 1.283
// f1(4.9) = 1.091
// f1(20) = 1.0
// f1(50) = 0.9

// RH = 85%
// f2(-10) = 1.091
// f2(4.9) = 0.946
// f2(20) = 0.849
// f2(50) = 0.72

MQ4::MQ4(int port) : MQ(port, 70.f, 69.9f)
{
}

float MQ4::HaTFunction(float t, int rh)
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
    return infinityf(); // out of the top border
  case 1:
    return rh == 33 ? linearFunction(1.091f, 1.283f, 4.9f, -10.f, t) : linearFunction(0.946f, 1.091f, 4.9f, -10.f, t);
  case 2:
    return rh == 33 ? linearFunction(1.f, 1.091f, 20.f, 4.9f, t) : linearFunction(0.849f, 0.946f, 20.f, 4.9f, t);
  case 3:
    return rh == 33 ? linearFunction(0.9f, 1.f, 50.f, 20.f, t) : linearFunction(0.72f, 0.849f, 50.f, 20.f, t);
  case 4:
    return -infinityf(); // out of the bottom border
  }
  return -1; // undefined error
}

float MQ4::getValue(float temperature, float humidity)
{
  float y = getResistance() / R0 * getHaTCoefficient(temperature, humidity);
  float const epsilon = 0.0001f;
  int const points_length = 4;
  float points[points_length] = {0.45f, 0.575f, 1.f, 1.84f};
  int conditions = 0;
  for (int i = 0; i < points_length; i++)
    conditions += i == points_length - 1 ? int(y > points[i] + epsilon) : int(y > points[i] - epsilon);

  switch (conditions)
  {
  case 0:
    return infinityf(); // out of the top border
  case 1:
    return getPercent(10000.f, 5000.f, 0.45f, 0.575f, y);
  case 2:
    return getPercent(5000.f, 1000.f, 0.575f, 1.f, y);
  case 3:
    return getPercent(1000.f, 200.f, 1.f, 1.84f, y);
  case 4:
    return -infinityf(); // out of the bottom border
  }
  return -1; // undefined error
}