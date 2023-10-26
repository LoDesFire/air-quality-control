#include <MQ.h>
#include <cmath>
#include <Arduino.h>

float MQ::linearFunction(float y1, float y2, float x1, float x2, float x)
{
    return y2 + (x - x2) * (y2 - y1) / (x2 - x1);
}

float MQ::getHaTCoefficient(float t, float rh)
{
    return HaTFunction(t, 33) - (rh - 33.0) / 52.0 * (HaTFunction(t, 33) - HaTFunction(t, 85));
}

float MQ::getPercent(float x1, float x2, float y1, float y2, float y)
{
    float m = (log10f(y1) - log10f(y2)) / (log10f(x1) - log10f(x2));
    return powf(powf(x2, m) / y2 * y, 1.0f / m) / 10000.f;
}

float MQ::getResistance()
{
    return (1.f - analogRead(Port) / 4095.f) * RL;
}

MQ::MQ(int port, float r0, float rl):
    Port(port),
    R0(r0),
    RL(rl)
{}

void MQ::begin()
{
    pinMode(Port, INPUT);
}

float MQ::HaTFunction(float, int) {
    return infinityf();
}

float MQ::getValue(float, float) {
    return infinityf();
}