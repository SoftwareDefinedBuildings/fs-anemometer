
#include "interface.h"
#include "libstorm.h"
#include <array>
#include <queue>
#include <cstring>
#include <stdio.h>

namespace storm
{
  std::shared_ptr<std::vector<uint8_t>> mkbuf(size_t size)
  {
    return std::make_shared<std::vector<uint8_t>>(size);
  }
  std::shared_ptr<std::vector<uint8_t>> mkbuf(std::initializer_list<uint8_t> contents)
  {
    auto rv = std::make_shared<std::vector<uint8_t>>(contents.size());
    int idx = 0;
    for (auto v : contents) (*rv)[idx++] = v;
    return rv;
  }
  namespace _priv
  {
    uint32_t __attribute__((naked)) syscall_ex(...)
    {
      asm volatile (\
          "push {r4-r11}\n\t"\
          "svc 8\n\t"\
          "pop {r4-r11}\n\t"\
          "bx lr":::"memory", "r0");
    }
    void irq_callback(uint32_t idx);
  }
  namespace tq
  {
    void Task::fire()
    {
      (*target)();
    }
    std::queue<Task> dyn_tq;
    template <> bool add(std::shared_ptr<std::function<void(void)>> target)
    {
      dyn_tq.push(Task(target));
      return true;
    }
    bool run_one()
    {
      if (dyn_tq.empty())
      {
        return false;
      }
      dyn_tq.front().fire();
      dyn_tq.pop();
      return true;
    }
    void __attribute__((noreturn)) scheduler()
    {
      while(1)
      {
        while(run_one());
        k_wait_callback();
      }
    }
  }
  namespace util
  {
    Resource::Resource()
      :active(false)
    {}
    void Resource::acquire(std::function<void()> cb)
    {
      if (!active && queue.empty())
      {
        active = true;
        tq::add(cb);
      }
      else
      {
        queue.push(cb);
      }
    }
    void Resource::release()
    {
      if (queue.empty())
      {
        active = false;
      }
      else
      {
        auto cb = queue.front();
        queue.pop();
        tq::add(cb);
      }
    }
  }
  namespace gpio
  {
    //Pin directions
    const Dir OUT={0};
    const Dir IN={1};

    //Pin values
    const uint8_t HIGH = 1;
    const uint8_t LOW = 0;
    const uint8_t TOGGLE = 2;
    //Edges
    const Edge RISING = {1};
    const Edge FALLING = {2};
    const Edge CHANGE = {0};
    //Pulls
    const Pull UP = {1};
    const Pull DOWN = {2};
    const Pull KEEP = {3};
    const Pull NONE = {0};

    static std::shared_ptr<std::function<void()>> irq_ptrs [20] = {nullptr};

    uint32_t set_mode(Pin pin, Dir dir)
    {
      return _priv::syscall_ex(0x101, dir.dir, pin.spec);
    }
    uint32_t set(Pin pin, uint8_t value)
    {
      return _priv::syscall_ex(0x102, value, pin.spec);
    }
    uint8_t get(Pin pin)
    {
      return _priv::syscall_ex(0x103, pin.spec);
    }
    void set_pull(Pin pin, Pull pull)
    {
      _priv::syscall_ex(0x104, pull.dir, pin.spec);
    }
    void disable_irq(Pin pin)
    {
      if (irq_ptrs[pin.idx])
      {
        _priv::syscall_ex(0x108, pin.spec);
        irq_ptrs[pin.idx].reset();
      }
    }
    void enable_irq(Pin pin, Edge edge, std::shared_ptr<std::function<void(void)>> callback)
    {
      disable_irq(pin);
      irq_ptrs[pin.idx] = callback;
      _priv::syscall_ex(0x106, pin.spec, edge.edge, static_cast<void(*)(uint32_t)>(_priv::irq_callback), pin.idx);
    }
  }
  namespace _priv
  {
    void irq_callback(uint32_t idx)
    {
      if (idx <= 20 && gpio::irq_ptrs[idx])
      {
        tq::add(gpio::irq_ptrs[idx]);
      }
    }
  }

  namespace _priv
  {
    void tmr_callback(Timer *self);
  }


  void Timer::cancel()
  {
    //TODO: purge any queued callbacks from the task queue
    if(self)
    {
      _priv::syscall_ex(0x205, id);
      self.reset();
    }
  }
  void Timer::fire()
  {
    auto t = self;
    tq::add([=](){
      (*callback)(t);
    });
    if (!is_periodic)
    {
      self.reset(); //undangle ourselves to be deconstructed
    }
  }
  Timer::Timer(bool periodic, uint32_t ticks, std::shared_ptr<std::function<void(std::shared_ptr<Timer>)>> callback):
    is_periodic(periodic), callback(callback)
  {
    uint32_t rv = _priv::syscall_ex(0x201, ticks, periodic, static_cast<void(*)(Timer*)>(_priv::tmr_callback), this);
    if (rv == (uint32_t)-1)
    {
      while(1);
    }
    id = rv;
  }
  namespace _priv
  {
    /*
     I expect this usage of a raw pointer to be safe under the following conditions:
      - The timer will not be deleted until cancel() is called
      - Cancel stops the kernel from creating more timer callbacks +
        also purges any previously issued callbacks from the task queue
     */
    void tmr_callback(Timer *self)
    {
      self->fire();
    }
    struct udp_recv_params_t
    {
        uint32_t reserved1;
        uint32_t reserved2;
        uint8_t* buffer;
        uint32_t buflen;
        uint8_t src_address [16];
        uint32_t port;
        uint8_t lqi;
        uint8_t rssi;
    } __attribute__((__packed__));
  }
  namespace flash
  {
    void erase_chip()
    {
      _priv::syscall_ex(0xA03);
    }
    util::Resource lock;
  }
  namespace sys
  {
    uint32_t now()
    {
      return _priv::syscall_ex(0x202);
    }
    uint32_t now(Shift shift)
    {
      return _priv::syscall_ex(shift.code);
    }
    void kick_wdt()
    {
      _priv::syscall_ex(0xb01);
    }
    void reset()
    {
      _priv::syscall_ex(0x404);
    }
    const Shift SHIFT_0 = {0x202};
    const Shift SHIFT_16 = {0x203};
    const Shift SHIFT_48 = {0x204};
  }

  template<> std::shared_ptr<UDPSocket> UDPSocket::open(uint16_t port, std::shared_ptr<std::function<void(std::shared_ptr<UDPSocket::Packet>)>> callback)
  {
    auto rv = std::shared_ptr<UDPSocket>(new UDPSocket(port, callback));
    if (!rv->okay)
    {
      return std::shared_ptr<UDPSocket>();
    }
    rv->self = rv; //Circle reference, we cannot be deconstructed
    return rv;
  }
  void UDPSocket::close()
  {
    if(self)
    {
      //TODO purge callbacks from tq
      _priv::syscall_ex(0x303, id);
      self.reset();
    }
  }
  void UDPSocket::_handle(_priv::udp_recv_params_t *recv, char *addrstr)
  {
    auto rv = std::make_shared<Packet>();
    rv->payload = std::string(reinterpret_cast<const char*>(recv->buffer), static_cast<size_t>(recv->buflen));
    rv->strsrc = std::string(addrstr);
    std::memcpy(rv->src, recv->src_address, 16);
    rv->port = recv->port;
    rv->lqi = recv->lqi;
    rv->rssi = recv->rssi;
    (*callback)(rv);
  }
  UDPSocket::UDPSocket(uint16_t port, std::shared_ptr<std::function<void(std::shared_ptr<UDPSocket::Packet>)>> callback)
    :okay(false), callback(callback)
  {
    //create
    id = (int32_t) _priv::syscall_ex(0x301);
    if (id == -1) {
      return;
    }
    //bind
    int rv = _priv::syscall_ex(0x302, id, port);
    if (rv == -1) {
      return;
    }
    rv = _priv::syscall_ex(0x305, id, static_cast<void(*)(UDPSocket*, _priv::udp_recv_params_t*, char*)>(_priv::udp_callback), this);
    if (rv == -1) {
      return;
    }
    okay = true;
  }
  bool UDPSocket::sendto(const std::string &addr, uint16_t port, const std::string &payload)
  {
    return sendto(addr, port, reinterpret_cast<const uint8_t*>(&payload[0]), payload.size());
  }
  bool UDPSocket::sendto(const std::string &addr, uint16_t port, const uint8_t *payload, size_t length)
  {
    int rv = _priv::syscall_ex(0x304, id, payload, length, addr.data(), port);
    return rv == 0;
  }
  bool UDPSocket::sendto(const std::string &addr, uint16_t port, buf_t payload, size_t length)
  {
    return sendto(addr, port, reinterpret_cast<const uint8_t*>(&((*payload)[0])), length);
  }
  namespace _priv
  {
    void udp_callback(UDPSocket *sock, udp_recv_params_t *recv, char *addrstr)
    {
      sock->_handle(recv, addrstr);
    }
  }
  namespace _priv
  {
    void i2c_wcallback(i2c::I2CWOperation *op, int status)
    {
      tq::add([op, status]
      {
        op->invoke(status);
      });
    }
    void i2c_rcallback(i2c::I2CROperation *op, int status)
    {
      tq::add([op, status]
      {
        op->invoke(status);
      });
    }
    void flash_wcallback(flash::FlashWOperation *op, int status)
    {
      storm::Timer::once(40*storm::Timer::MILLISECOND, [=](auto)
      {
        tq::add([op, status]
        {
          op->invoke(status);
        });
      });
    }
    void flash_rcallback(flash::FlashROperation *op, int status)
    {
      tq::add([op, status]
      {
        op->invoke(status);
      });
    }
  }
  namespace i2c
  {
    util::Resource lock;
    const char* decode(int code)
    {
      if (code == OK) return "OK";
      if (code == DNAK) return "DNAK";
      if (code == ANAK) return "ANAK";
      if (code == ERR) return "ERR";
      if (code == ARBLST) return "ARBLST";
      return "UNK";
    }
  }
}
