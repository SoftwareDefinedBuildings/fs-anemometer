#ifndef __LIBSTORM_H__
#define __LIBSTORM_H__
#include <memory>
#include <functional>
#include <queue>
#include <stdio.h>
using std::move;
namespace storm
{
  using buf_t = std::shared_ptr<std::vector<uint8_t>>;
  std::shared_ptr<std::vector<uint8_t>> mkbuf(size_t size);
  std::shared_ptr<std::vector<uint8_t>> mkbuf(std::initializer_list<uint8_t> contents);

  namespace _priv
  {
    uint32_t __attribute__((naked)) syscall_ex(...);
  }
  namespace tq
  {
    class Task
    {
    public:
      Task(std::shared_ptr<std::function<void(void)>> target) : target(target){}
      void fire();
    private:
      const std::shared_ptr<std::function<void(void)>> target;
    };
    extern std::queue<Task> dyn_tq;
    template <typename T> bool add(T target)
    {
      dyn_tq.push(Task(std::make_shared<std::function<void(void)>>(target)));
      return true;
    }
    void __attribute__((noreturn)) scheduler();
  }
  namespace util
  {
    class Resource
    {
    public:
      Resource();
      void acquire(std::function<void()>);
      void release();
    private:
      bool active;
      std::queue<std::function<void()>> queue;
    };
  }


  namespace gpio
  {
    struct Pin {
      const uint16_t idx;
      const uint16_t spec;
    };
    struct Dir {
      const uint8_t dir;
    };
    struct Edge {
      const uint8_t edge;
    };
    struct Pull {
      const uint8_t dir;
    };

    //Pin directions
    extern const Dir OUT;
    extern const Dir IN;
    //Pins

    constexpr Pin D0 = {0,0x0109};
    constexpr Pin D1 = {1,0x010A};
    constexpr Pin D2 = {2,0x0010};
    constexpr Pin D3 = {3,0x000C};
    constexpr Pin D4 = {4,0x0209};
    constexpr Pin D5 = {5,0x000A};
    constexpr Pin D6 = {6,0x000B};
    constexpr Pin D7 = {7,0x0013};
    constexpr Pin D8 = {8,0x000D};
    constexpr Pin D9 = {9,0x010B};
    constexpr Pin D10 = {10,0x010C};
    constexpr Pin D11 = {11,0x010F};
    constexpr Pin D12 = {12,0x010E};
    constexpr Pin D13 = {13,0x010D};
    constexpr Pin A0 = {14,0x0105};
    constexpr Pin A1 = {15,0x0104};
    constexpr Pin A2 = {16,0x0103};
    constexpr Pin A3 = {17,0x0102};
    constexpr Pin A4 = {18,0x0007};
    constexpr Pin A5 = {19,0x0005};
    constexpr Pin GP0 = {20,0x020A};

    //Pin values
    extern const uint8_t HIGH;
    extern const uint8_t LOW;
    extern const uint8_t TOGGLE;
    //Edges
    extern const Edge RISING;
    extern const Edge FALLING;
    extern const Edge CHANGE;
    //Pull
    extern const Pull UP;
    extern const Pull DOWN;
    extern const Pull KEEP;
    extern const Pull NONE;

    //GPIO functions
    uint32_t set_mode(Pin pin, Dir dir);
    uint32_t set(Pin pin, uint8_t value);
    uint8_t get(Pin pin);
    uint8_t get_shadow(Pin pin);
    void set_pull(Pin pin, Pull pull);
    void enable_irq(Pin pin, Edge edge, std::shared_ptr<std::function<void()>> callback);
    void disable_irq(Pin pin);
  }

  class Timer
  {
  public:
    Timer() = delete;
    Timer(const Timer& that) = delete;
    template<typename T> static std::shared_ptr<Timer> once(uint32_t ticks, T callback)
    {
      auto rv = std::shared_ptr<Timer>(new Timer(false, ticks, std::make_shared<std::function<void(std::shared_ptr<Timer>)>>(callback)));
      rv->self = rv; //Circle reference, we cannot be deconstructed
      return rv;
    }
    template<typename T> static std::shared_ptr<Timer> periodic(uint32_t ticks, T callback)
    {
      auto rv = std::shared_ptr<Timer>(new Timer(true, ticks, std::make_shared<std::function<void(std::shared_ptr<Timer>)>>(callback)));
      rv->self = rv; //Circle reference, we cannot be deconstructed
      return rv;
    }

    void cancel();
    void fire();
    static constexpr uint32_t MILLISECOND = 375;
    static constexpr uint32_t SECOND = MILLISECOND*1000;
    static constexpr uint32_t MINUTE = SECOND*60;
    static constexpr uint32_t HOUR = MINUTE*60;


  private:
    Timer(bool periodic, uint32_t ticks, std::shared_ptr<std::function<void(std::shared_ptr<Timer>)>> callback);
    uint16_t id;
    bool is_periodic;
    const std::shared_ptr<std::function<void(std::shared_ptr<Timer>)>> callback;
    std::shared_ptr<Timer> self;
  };
  namespace sys
  {
    struct Shift {
      const uint16_t code;
    };
    uint32_t now();
    uint32_t now(Shift shift);
    void reset();
    void kick_wdt();
    extern const Shift SHIFT_0;
    extern const Shift SHIFT_16;
    extern const Shift SHIFT_48;
  }
  class UDPSocket;
  namespace _priv
  {
    struct udp_recv_params_t;
    void udp_callback(UDPSocket *sock, udp_recv_params_t *recv, char *addrstr);
  }
  class UDPSocket
  {
  public:
    class Packet
    {
    public:
      std::string payload;
      std::string strsrc;
      uint8_t src[16];
      uint16_t port;
      uint8_t lqi;
      uint8_t rssi;
    };
    template<typename T> static std::shared_ptr<UDPSocket> open(uint16_t port, T callback)
    {
      auto rv = std::shared_ptr<UDPSocket>(new UDPSocket(port, std::make_shared<std::function<void(std::shared_ptr<Packet>)>>(callback)));
      if (!rv->okay)
      {
        return std::shared_ptr<UDPSocket>();
      }
      rv->self = rv; //Circle reference, we cannot be deconstructed
      return rv;
    }
    void close();
    void _handle(_priv::udp_recv_params_t *recv, char *addrstr);
    bool sendto(const std::string &addr, uint16_t port, const std::string &payload);
    bool sendto(const std::string &addr, uint16_t port, const uint8_t *payload, size_t length);
    bool sendto(const std::string &addr, uint16_t port, buf_t payload, size_t length);
  private:
    UDPSocket(uint16_t port, std::shared_ptr<std::function<void(std::shared_ptr<Packet>)>> callback);
    int32_t id;
    bool okay;
    const std::shared_ptr<std::function<void(std::shared_ptr<Packet>)>> callback;
    std::shared_ptr<UDPSocket> self;
  };
  namespace flash
  {
    class FlashWOperation;
    class FlashROperation;
    extern util::Resource lock;
  }
  namespace _priv
  {
    void flash_wcallback(flash::FlashWOperation *op, int status);
    void flash_rcallback(flash::FlashROperation *op, int status);
  }
  namespace flash
  {
    class FlashWOperation
    {
    public:
      FlashWOperation(buf_t payload, std::function<void(int, buf_t)> callback)
        :payload(move(payload)), callback(callback)
      {
      }
      void invoke(int status)
      {
        callback(status, move(payload));
        self.reset();
      }
      buf_t payload;
      std::function<void(int, buf_t)> callback;
      std::shared_ptr<FlashWOperation> self;
    };
    class FlashROperation
    {
    public:
      FlashROperation(buf_t payload, size_t length, std::function<void(int, buf_t)> callback)
        : length(length), payload(move(payload)), callback(callback)
      {
      }
      void invoke(int status)
      {
        callback(status, move(payload));
        self.reset();
      }
      size_t length;
      buf_t payload;
      std::function<void(int, buf_t)> callback;
      std::shared_ptr<FlashROperation> self;
    };
    template <typename T> std::shared_ptr<FlashWOperation> write(uint32_t address, buf_t payload, uint8_t length, T callback)
    {
      auto rv = std::make_shared<FlashWOperation>(move(payload), std::function<void(int, buf_t)>(callback));
      rv->self = rv; //circular reference to prevent dealloc.
      int sysrv = _priv::syscall_ex(0xA02, address, &((*rv->payload)[0]), length, _priv::flash_wcallback, rv.get());
      return sysrv ? nullptr : rv;
    }
    template <typename T> std::shared_ptr<FlashROperation> read(uint32_t address, buf_t target, uint8_t length, T callback)
    {
      auto rv = std::make_shared<FlashROperation>(move(target), length, std::function<void(int, buf_t)>(callback));
      rv->self = rv; //circular reference to prevent dealloc.
      int sysrv = _priv::syscall_ex(0xA01, address, &((*rv->payload)[0]), length, _priv::flash_rcallback, rv.get());
      return sysrv ? nullptr : rv;
    }
    void erase_chip();
  }
  namespace i2c
  {
    class I2CWOperation;
    class I2CROperation;
  }
  namespace _priv
  {
    void i2c_wcallback(i2c::I2CWOperation *op, int status);
    void i2c_rcallback(i2c::I2CROperation *op, int status);
  }
  namespace i2c
  {
    extern util::Resource lock;

    class I2CFlag
    {
    public:
      constexpr I2CFlag operator| (I2CFlag const& rhs)
      {
        return I2CFlag{val+rhs.val};
      }
      uint32_t val;
    };
    class I2CWOperation
    {
    public:
      I2CWOperation(buf_t payload, std::function<void(int, buf_t)> callback)
        :payload(move(payload)), callback(callback)
      {
      }
      void invoke(int status)
      {
        callback(status, move(payload));
        self.reset();
      }
      buf_t payload;
      std::function<void(int, buf_t)> callback;
      std::shared_ptr<I2CWOperation> self;
    };
    class I2CROperation
    {
    public:
      I2CROperation(buf_t payload, size_t length, std::function<void(int, buf_t)> callback)
        : length(length), payload(move(payload)), callback(callback)
      {
      }
      void invoke(int status)
      {
        callback(status, move(payload));
        self.reset();
      }
      size_t length;
      buf_t payload;
      std::function<void(int, buf_t)> callback;
      std::shared_ptr<I2CROperation> self;
    };
    template <typename T> std::shared_ptr<I2CWOperation> write(uint16_t address, I2CFlag const &flags, buf_t payload, uint16_t length, T callback)
    {
      auto rv = std::make_shared<I2CWOperation>(move(payload), std::function<void(int, buf_t)>(callback));
      rv->self = rv; //circular reference to prevent dealloc.
      int sysrv = _priv::syscall_ex(0x502, address, flags.val, &((*rv->payload)[0]), length, _priv::i2c_wcallback, rv.get());
      return sysrv ? nullptr : rv;
    }
    template <typename T> std::shared_ptr<I2CROperation> read(uint16_t address, I2CFlag const &flags, buf_t target, uint16_t length, T callback)
    {
      auto rv = std::make_shared<I2CROperation>(move(target), length, std::function<void(int, buf_t)>(callback));
      rv->self = rv; //circular reference to prevent dealloc.
      int sysrv = _priv::syscall_ex(0x501, address, flags.val, &((*rv->payload)[0]), length, _priv::i2c_rcallback, rv.get());
      return sysrv ? nullptr : rv;
    }
    constexpr uint16_t internal(uint8_t address)
    {
      return 0x200 + address;
    }
    constexpr uint16_t external(uint8_t address)
    {
      return 0x100 + address;
    }
    constexpr uint16_t TMP006 = internal(0x80);
    constexpr I2CFlag NONE = I2CFlag{0};
    constexpr I2CFlag START = I2CFlag{1};
    constexpr I2CFlag RSTART = I2CFlag{1};
    constexpr I2CFlag ACKLAST = I2CFlag{2};
    constexpr I2CFlag STOP = I2CFlag{4};
    constexpr int OK = 0;
    constexpr int DNAK = 1;
    constexpr int ANAK = 2;
    constexpr int ERR = 3;
    constexpr int ARBLST = 4;
    constexpr int SYSCALL_ERR = 5;
    const char* decode(int code);
  }
}

#endif
