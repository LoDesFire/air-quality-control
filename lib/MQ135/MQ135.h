#include <MQ.h>

class MQ135 : public MQ
{
  float HaTFunction(float, int);

public:
  MQ135(int port);

  float getValue(float, float);
};