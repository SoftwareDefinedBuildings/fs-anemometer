#the objective of this makefile is to compile using g++ specs
CXX       = arm-none-eabi-g++
CC       = arm-none-eabi-g++
CPPFLAGS   = -ffunction-sections -std=c++14 -fno-exceptions -fno-unwind-tables -fdata-sections -Os -g3 -fno-strict-aliasing -Wall -mfloat-abi=soft -mthumb -mcpu=cortex-m4 --specs=nano.specs --specs=nosys.specs
LDFLAGS  = -nostdlib -ffunction-sections -std=c++14 -fdata-sections -fno-strict-aliasing -Os -g3 -nostartfiles -T synergypayload.ld -mfloat-abi=soft -mcpu=cortex-m4 -mthumb --specs=nano.specs --specs=nosys.specs
# LDFLAGS += -L.
LDFLAGS += -lstdc++ -lm -lc -lgcc -Wl,--gc-sections
#we have libc.a rather than -lc above

anemometer: main.o libstorm.o interface.o
	$(CXX) -o anemometer.elf $^ $(LDFLAGS)
	arm-none-eabi-size anemometer.elf

all: clean tester

install:
	sload program anemometer.elf

.PHONY: clean

clean:
	rm -f *.o anemometer.elf
