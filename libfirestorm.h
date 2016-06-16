
#ifndef __LIB_FIRESTORM__
#define __LIB_FIRESTORM__

#include <stdio.h>
#include "interface.h"
#include <functional>
#include "libstorm.h"
#include <cstring>
#include <time.h>

using namespace storm;

namespace firestorm
{
  template <uint16_t devaddress, uint8_t regaddr>
  class I2CRegister
  {
  public:
    I2CRegister()
    {
    }

    static void read_offset(uint8_t offset, buf_t target, uint16_t length, std::function<void(int,buf_t)> const& callback)
    {
      i2c::lock.acquire([=]
      {
        auto addrbuf = mkbuf({(uint8_t)(regaddr+offset)});
        auto srv = i2c::write(devaddress, i2c::START, move(addrbuf), 1,
          [length,callback = move(callback),target = move(target)](int status, buf_t buf)
        {
          if (status != i2c::OK)
          {
            i2c::lock.release();
            callback(status, move(buf));
            return;
          }
          auto srv = i2c::read(devaddress, i2c::RSTART | i2c::STOP, move(target), length,
            [callback = move(callback),target = move(target)](int status, buf_t buf)
          {
            i2c::lock.release();
            callback(status, move(buf));
            return;
          });
          if (srv == nullptr)
          {
            i2c::lock.release();
            callback(i2c::SYSCALL_ERR, nullptr);
          }
        });
        if (srv == nullptr)
        {
          i2c::lock.release();
          callback(i2c::SYSCALL_ERR, nullptr);
        }
      });
    }

    static void read(buf_t target, uint16_t length, std::function<void(int,buf_t)> const& callback)
    {
      read_offset(0, target, length, callback);
    }

    static void write_offset(uint8_t offset, buf_t msg, uint16_t length, std::function<void(int,buf_t)>  const& callback)
    {
      i2c::lock.acquire([=]
      {
        auto msgbuf = mkbuf(length+1);
        std::memcpy(&(*msgbuf)[1], &(*msg)[0], length);
        (*msgbuf)[0] = regaddr + offset;

        auto srv = i2c::write(devaddress, i2c::START | i2c::STOP, move(msgbuf), length+1,
          [callback,msg = move(msg)](int status, buf_t buf)
        {
          //We don't use the new buffer we made, rather return the buffer the user
          //gave us
          i2c::lock.release();
          callback(status, move(buf));
        });
        if (srv == nullptr)
        {
          i2c::lock.release();
          callback(i2c::SYSCALL_ERR, nullptr);
        }
      });
    }
    static void write(buf_t msg, uint16_t length, std::function<void(int,buf_t)>  const& callback)
    {
      write_offset(0, msg, length, callback);
    }
  };

  class TMP006
  {
  public:
    TMP006()
     : okay(false)
    {
      //Reset the chip and sample at 1/sec
      buf_t cfg = mkbuf({0b11110100});
      config.write(cfg, 1, [&](int status,auto){
        okay = (status == i2c::OK);
      });
    }
    void getDieTemp(std::function<void(double)> const& result)
    {
      buf_t rv = mkbuf(2);
      temp.read(move(rv), 2, [result](int status, buf_t buf){
        if (status != i2c::OK)
        {
          result(-1);
          return;
        }
        uint16_t temp = (((uint16_t)(*buf)[0] << 8) + (*buf)[1]) >> 2;
        double rtemp = (double)temp * 0.03125;
        result(rtemp);
      });
    }
  private:
    bool okay;
    static I2CRegister<i2c::TMP006, 2> config;
    static I2CRegister<i2c::TMP006, 2> temp;
    static I2CRegister<i2c::TMP006, 0> sensor;
  };
}

#endif
