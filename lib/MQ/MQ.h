#pragma once

class MQ
{
protected:
    int Port;

    float linearFunction(float, float, float, float, float);

    float getPercent(float, float, float, float, float);

    float getResistance();

    float virtual HaTFunction(float, int);

    float getHaTCoefficient(float, float);

public:
    const float R0;

    const float RL;

    void begin();

    MQ(int, float, float);

    float virtual getValue(float, float);
};
