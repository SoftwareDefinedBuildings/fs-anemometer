#include <stdio.h>
#include <functional>
#include "libstorm.h"
#include "selfcheck.h"
#include "asic.h"
#include "math.h"

#define COUNT_TX (-4)

using namespace storm;

extern void undeffunc();

int ACAL;
int BCAL;
int CAL_PULSELEN;
ChirpASIC asicA = ChirpASIC(gpio::A2, gpio::A0, gpio::D6);
ChirpASIC asicB = ChirpASIC(gpio::A3, gpio::A1, gpio::D7);

void do_sample(ChirpASIC *tx, ChirpASIC *rx, std::function<void(buf_t)> ondone)
{
  tx->irq_idle();
  rx->irq_idle();
  tx->irq_output();
  rx->irq_output();
  tx->set_opmode(MODE_TXRX, [=] () mutable
  {
    rx->set_opmode(MODE_RX, [=] () mutable
    {
      Timer::once(15*Timer::MILLISECOND, [=](auto)
      {
        rx->read_sample_data([=](buf_t rx_dat) mutable
        {
          ondone(rx_dat);
        });
      });
      tx->gang_irq_active();
      volatile int i;
      for (i = 0; i < 100; i++);
      tx->gang_irq_idle();


      //rx->irq_input();
      //tx->irq_input();
    });
  });
}
void get_tof(buf_t p, uint32_t calres)
{
  auto b = *p;
  int16_t iz[16];
  int16_t qz[16];
  uint64_t magsqr[16];
  uint64_t magmax = 0;
  uint16_t tof_sf;

  tof_sf = b[0] + (((uint16_t)b[1]) << 8);
  for (int i = 0; i < 16; i++)
  {
    qz[i] = (int16_t) (b[6+i*4] + (((uint16_t)b[6+ i*4 + 1]) << 8));
    iz[i] = (int16_t) (b[6+i*4 + 2] + (((uint16_t)b[6+ i*4 + 3]) << 8));
    magsqr[i] = (uint64_t)(((int64_t)qz[i])*((int64_t)qz[i]) + ((int64_t)iz[i])*((int64_t)iz[i]));
    if (magsqr[i] > magmax)
    {
      magmax = magsqr[i];
    }
  }
  //Now we know the max, find the first index to be greater than half max
  uint64_t quarter = magmax >> 2;
  int ei = 0;
  int si = 0;
  for (int i = 0; i < 16; i++)
  {
    if (magsqr[i] < quarter)
    {
      si = i;
    }
    if (magsqr[i] > quarter)
    {
      ei = i;
      break;
    }
  }
  double s = sqrt((double)magsqr[si]);
  double e = sqrt((double)magsqr[ei]);
  double h = sqrt((double)quarter);
  double freq = tof_sf/2048.0*calres/CAL_PULSELEN;
  double count = si + (h - s)/(e - s);
  double tof = (count + COUNT_TX) / freq * 8;

  //Now "linearly" interpolate
  printf("count %d /1000\n", (int)(count*1000));
  printf("tof_sf %d\n", tof_sf);
  printf("freq %d uHz\n", (int)(freq*1000));
  printf("tof %d uS\n", (int)(tof*1000));
  printf("tof 50us estimate %duS\n", (int)(count*50));
  for (int i = 0; i < 16; i++)
  {
    printf("data %d = %d + %di\n", i, qz[i], iz[i]);
  }
  printf(".\n");
}
void dopair()
{
  do_sample(&asicA, &asicB, [=](buf_t a2b)
  {
    do_sample(&asicB, &asicA, [=](buf_t b2a)
    {
      get_tof(a2b, BCAL);
      get_tof(b2a, ACAL);
    });
  });
}
void calibrate(std::function<void()> ondone)
{
  asicA.irq_idle();
  asicA.irq_output();
  asicB.irq_idle();
  asicB.irq_output();
  asicA.prime_calibrate([=]
  {
    asicB.prime_calibrate([=]
    {
        asicA.gang_irq_active();
        Timer::once(160*Timer::MILLISECOND, [=](auto)
        {
          asicA.gang_irq_idle();
          asicA.read_cal_result([=](int result)
          {
            ACAL = result;
            asicB.read_cal_result([=](int result)
            {
              BCAL = result;
              CAL_PULSELEN = 160; //TODO switch to accurate pulse length
              asicA.set_maxrange(0x10, [=]
              {
                asicB.set_maxrange(0x10, [=]
                {
                  printf("both calibrate's finished: A=%d B=%d\n", ACAL, BCAL);
                  tq::add(ondone);
                });
              });
            });
          });
        });
    });
  });
}
int main()
{
  printf("Anemometer booted\n");
  sys::kick_wdt();
  gpio::set_mode(gpio::A5, gpio::OUT);
  gpio::set(gpio::A5, 1);

  asicA.program(0x30, [&](int status)
  {
    printf("program ASIC A: %s\n", i2c::decode(status));
    asicA.wait_and_check_ready([&]
    {
      asicB.program(0x40, [&](int status)
      {
        printf("program ASIC B: %s\n", i2c::decode(status));
        asicB.wait_and_check_ready([&]
        {
          calibrate([&]
          {
            printf("Calibrate complete\n");
            dopair();
          });
        });
      });
    });
  });


  Timer::periodic(1*Timer::SECOND, [](auto)
  {
    sys::kick_wdt();
  });
  tq::scheduler();
}
