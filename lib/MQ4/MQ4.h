#include <MQ.h>

class MQ4: public MQ
{
    float HaTFunction(float, int) override;

public:
    MQ4(int);

    float getValue(float, float) override;
};