#ifndef __ASIC_H__
#define __ASIC_H__

#include "libstorm.h"
#include "libfirestorm.h"
#include "wind_v8.rawbin.h"

using namespace storm;

#define PROG_ADDR 0x85
#define PROG_CNT  0x87
#define PROG_CTL  0xC4
#define PROG_DATA 0xC6
#define PROG_CPU  0xC2
//#define DEF_ADDR  0x45
#define DEF_ADDR  0x8a

#define OPMODE 0x01
#define OPMODE_SZ 01
#define TICK_INTERVAL 0x02
#define TICK_INTERVAL_SZ 2
#define PERIOD 0x05
#define PERIOD_SZ 1
#define CAL_TRIG 0x06
#define CAL_TRIG_SZ 1
#define MAX_RANGE 0x07
#define MAX_RANGE_SZ 1
#define CAL_RESULT 0x0A
#define CAL_RESULT_SZ 2
#define HOLDOFF    0x11
#define HOLDOFF_SZ 1
#define ST_RANGE 0x12
#define ST_RANGE_SZ 1
#define ST_COEFF 0x13
#define ST_COEFF_SZ 1
#define READY 0x14
#define READY_SZ 1
#define TOF_SF 0x16
#define TOF_SF_SZ 2
#define TOF 0x18
#define TOF_SZ 2
#define INTENSITY 0x1A
#define INTENSITY_SZ 2

#define MODE_TXRX 0x10
#define MODE_RX 0x20

class ChirpASIC
{
public:
  ChirpASIC(gpio::Pin prog, gpio::Pin irq, gpio::Pin rst)
    : prog(prog), irq(irq), rst(rst)
  {
    gpio::set_mode(rst, gpio::OUT);
    gpio::set(rst, 0);
    gpio::set_mode(prog, gpio::OUT);
    gpio::set(prog, 0);
    gpio::set_mode(irq, gpio::IN);
    gpio::set(irq, 0);
  //  gpio::set_pull(irq, gpio::DOWN);
  }
  ~ChirpASIC()
  {
    printf("deconstructing\n");
  }
  void rst_active()
  {
    gpio::set(rst, 0);
  }
  void rst_idle()
  {
    gpio::set(rst, 1);
  }
  void prog_active()
  {
    gpio::set(prog, 1);
  }
  void prog_idle()
  {
    gpio::set(prog, 0);
  }
  void irq_input()
  {
    gpio::set_mode(irq, gpio::IN);
  }
  void irq_output()
  {
    gpio::set_mode(irq, gpio::OUT);
  }
  void irq_active()
  {
  //  irq_output();
    gpio::set(irq, 1);
  }
  void irq_idle()
  {
  //  irq_input();
    gpio::set(irq, 0);
  }
  void _upload(uint16_t ptr, std::function<void(int)> const & ondone)
  {
    //Called with the i2c lock held
    printf("uploading ptr=%d\n", ptr);
    auto buf = mkbuf(128);
    for (int i = 0; i < 128; i++)
    {
      (*buf)[i] = wind_v8_rawbin[ptr + i];
    }
    auto flag = (ptr+256 > 2048) ? i2c::STOP : i2c::NONE;
    i2c::write(i2c::external(DEF_ADDR), flag, buf, 128, [=](int status, buf_t buf)
    {
      printf("complete s=%d\n", status);
      if (status != 0) {
        ondone(status);
      } else if (ptr + 128 >= 2048) {
        ondone(0);
      } else {
        _upload(ptr+128, ondone);
      }
    });
  }
  void gang_irq_active()
  {
    //The address of PORTB OVR SET
    uint32_t addr = 0x400E1000 + 0x200 + 0x054;
    uint32_t mask = 3 << 4;
    *((volatile uint32_t*)addr) = mask;
  }
  void gang_irq_idle()
  {
    //The address of PORTB OVR CLR
    uint32_t addr = 0x400E1000 + 0x200 + 0x058;
    uint32_t mask = 3 << 4;
    *((volatile uint32_t*)addr) = mask;
  }
  // void _readready(std::function<void(int, bool)>ondone)
  // {
  //   i2c::lock.acquire([=]
  //   {
  //     auto buf = mkbuf({OPMODE, OPMODE_SZ, mode});
  //     i2c::write(i2c::external(this->addr), i2c::START | i2c::STOP, buf, 3, [ondone](int status, buf_t buf)
  //     {
  //       i2c::lock.release();
  //       ondone(status);
  //     });
  //   });
  // }
  // void onready(std::function<void(int)>ondone)
  // {
  //
  // }
  //Assumes i2c lock is held
  void _w_reg(uint8_t regaddr, buf_t contents, std::function<void(int)> ondone)
  {
      auto buf = mkbuf(2+contents->size());
      (*buf)[0] = regaddr;
      (*buf)[1] = contents->size();
      for (uint16_t i = 0; i < contents->size(); i++)
      {
        (*buf)[2+i] = (*contents)[i];
      }
      i2c::write(i2c::external(this->addr), i2c::START | i2c::STOP, buf, buf->size(), [ondone](int status, buf_t buf)
      {
        if (status != 0)
        {
          printf("WRN: _w_reg i2c stat nonzero\n");
        }
        ondone(status);
      });
  }
  void _r_reg(uint8_t regaddr, uint8_t sz, std::function<void(int, buf_t)> ondone)
  {
    auto buf = mkbuf({regaddr});
    printf("r_reg addr1 %d %d\n", this, this->addr);
    i2c::write(i2c::external(this->addr), i2c::START, buf, 1, [this,ondone,sz](int status, buf_t buf)
    {
      if (status != 0)
      {
        printf("WRN: _r_reg i2c stat1 nonzero\n");
      }
      auto rvbuf = mkbuf(sz);
      printf("r_reg addr2 %d %d\n", this, this->addr);
      i2c::read(i2c::external(this->addr), i2c::RSTART | i2c::STOP, rvbuf, sz, [this,ondone](int status, buf_t rv)
      {
        if (status != 0)
        {
          printf("WRN: _r_reg i2c stat2 nonzero\n");
        }
        ondone(status, rv);
      });
    });
  }
  void print_state() {
    i2c::lock.acquire([=]
    {
      this->_r_reg(READY, READY_SZ, [=](int status, buf_t rv)
      {
        printf("ready reads as 0x%02x\n",(*rv)[0]);
        this->_r_reg(0x02, 2, [=](int status, buf_t rv)
        {
          i2c::lock.release();
          printf("other reads as 0x%02x, 0x%02x\n",(*rv)[0],(*rv)[1]);
        });
      });
    });
  }
  void set_opmode(uint8_t mode, std::function<void(int)> ondone)
  {
    i2c::lock.acquire([=]
    {
      this->_w_reg(OPMODE, mkbuf({mode}), [=](int status)
      {
        i2c::lock.release();
        ondone(status);
      });
    });
  }
  void read_ready(std::function<void(bool)> result)
  {
    i2c::lock.acquire([=]
    {
      this->_r_reg(READY, READY_SZ, [=](int status, buf_t rv)
      {
        i2c::lock.release();
        result((*rv)[0] == 0x02);
      });
    });
  }
  void prime_calibrate(std::function<void()> ondone)
  {
    i2c::lock.acquire([=]
    {
      this->_w_reg(CAL_TRIG, mkbuf({1}), [=](int status)
      {
        i2c::lock.release();
        ondone();
      });
    });
  }
  void read_cal_result(std::function<void(int)> result)
  {
    i2c::lock.acquire([=]
    {
      this->_r_reg(CAL_RESULT, CAL_RESULT_SZ, [=](int status, buf_t rv)
      {
        i2c::lock.release();
        int r = (*rv)[0];
        r += ((int)(*rv)[1]) << 8;
        result(r);
      });
    });
  }
  void set_maxrange(uint8_t val, std::function<void()> ondone)
  {
    i2c::lock.acquire([=]
    {
      this->_w_reg(MAX_RANGE, mkbuf({val}), [=](int status)
      {
        i2c::lock.release();
        ondone();
      });
    });
  }
  void set_opmode(uint8_t val, std::function<void()> ondone)
  {
    i2c::lock.acquire([=]
    {
      this->_w_reg(OPMODE, mkbuf({val}), [=](int status)
      {
        i2c::lock.release();
        ondone();
      });
    });
  }
  void enable_irq(std::function<void()> handler)
  {
    auto ph = std::make_shared<std::function<void()>>(handler);
    gpio::enable_irq(irq, gpio::RISING, ph);
  }
  void disable_irq()
  {
    gpio::disable_irq(irq);
  }
  // void gang_sample(std::function<void()> ondone)
  // {
  //   irq_idle();
  //   irq_output();
  //   irq_active();
  //   irq_idle();
  //   irq_input();
  //   gpio::enable_irq(irq, gpio::RISING, [=]
  //   {
  //     printf("got IRQ\n");
  //     gpio::disable_irq(irq);
  //     ondone();
  //   });
  // }
  void read_sample_data(std::function<void(buf_t)> ondone)
  {
    i2c::lock.acquire([=]
    {
      this->_r_reg(0x16, 70, [=](int status, buf_t rv)
      {
        i2c::lock.release();
        ondone(rv);
      });
    });
  }
  void wait_and_check_ready(std::function<void()> ondone)
  {
    Timer::once(60*Timer::MILLISECOND, [this,ondone](auto)
    {
      printf("reading ready\n");
      this->read_ready([this,ondone](bool res)
      {
        if (!res)
        {
          printf("ASIC not ready!\n");
          while(1);
        }
        ondone();
      });
    });
  }
  //Called from program with the i2c lock held
  void set_addr(uint8_t new_addr, std::function<void(int)> const & ondone)
  {
      addr = new_addr;
      auto buf = mkbuf({PROG_ADDR, 0xC5, 0x01});
      i2c::write(i2c::external(DEF_ADDR), i2c::START | i2c::STOP, buf, 3, [this,ondone](int status, buf_t buf)
      {
        if (status != 0) {
          ondone(status);
          return;
        }
        (*buf)[0] = PROG_DATA;
        (*buf)[1] = this->addr >> 1;
        (*buf)[2] = 0x00;
        i2c::write(i2c::external(DEF_ADDR), i2c::START | i2c::STOP, buf, 2, [this,ondone](int status, buf_t buf)
        {
          if (status != 0) {
            ondone(status);
            return;
          }
          (*buf)[0] = PROG_CTL;
          (*buf)[1] = 0x0B;
          i2c::write(i2c::external(DEF_ADDR), i2c::START | i2c::STOP, buf, 2, [this,ondone](int status, buf_t buf)
          {
            if (status != 0) {
              ondone(status);
              return;
            }
            (*buf)[0] = PROG_CPU;
            (*buf)[1] = 0x02;
            i2c::write(i2c::external(DEF_ADDR), i2c::START | i2c::STOP, buf, 2, [this,ondone](int status, buf_t buf)
            {
              if (status != 0) {
                ondone(status);
                return;
              }
              this->prog_idle();
              i2c::lock.release();
              ondone(0);
            });
          });
        });
      });
  }
  void program(uint8_t addr, std::function<void(int)> const & ondone)
  {
    rst_active();
    Timer::once(100*Timer::MILLISECOND, [this,ondone,addr](auto)
    {
      printf("finished RST v\n");
      this->prog_active();
      Timer::once(100*Timer::MILLISECOND, [this,ondone,addr](auto)
      {
        printf("finished RST ^\n");
        this->rst_idle();
        Timer::once(100*Timer::MILLISECOND,[this,ondone,addr](auto)
        {
          i2c::lock.acquire([this,ondone,addr]
          {
            printf("i2c acquired\n");
            auto buf = mkbuf({PROG_ADDR, 0x00, 0xF8});
            i2c::write(i2c::external(DEF_ADDR), i2c::START | i2c::STOP, buf, 3, [this,ondone,addr](int status, buf_t buf)
            {
              printf("i2c1\n");
              if (status != 0) {
                ondone(status);
                return;
              }
              (*buf)[0] = PROG_CNT;
              (*buf)[1] = 0xFF;
              (*buf)[2] = 0x07;
              i2c::write(i2c::external(DEF_ADDR), i2c::START | i2c::STOP, buf, 3, [this,ondone,addr](int status, buf_t buf)
              {
                if (status != 0) {
                  ondone(status);
                  return;
                }
                (*buf)[0] = PROG_CTL;
                (*buf)[1] = 0x0B;
                i2c::write(i2c::external(DEF_ADDR), i2c::START, buf, 2, [this,ondone,addr](int status, buf_t buf)
                {
                  if (status != 0) {
                    ondone(status);
                    return;
                  }
                  this->_upload(0, [this,ondone,addr](int status)
                  {
                    if (status != 0) {
                      ondone(status);
                      return;
                    }
                    this->set_addr(addr, ondone);
                    //i2c::lock.release();
                    //ondone(0);
                  });
                });
              });
            });
          });
        });
      });
    });

  }
private:
  storm::gpio::Pin prog;
  storm::gpio::Pin irq;
  storm::gpio::Pin rst;
  uint8_t addr;
};

#endif
