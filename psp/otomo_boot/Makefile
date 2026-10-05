# otomo_boot: the body's loader (device/psp/otomo_boot/main.c). Build in WSL with pspdev: ../build.sh otomo_boot
TARGET = otomo_boot
OBJS = main.o exports.o
PRX_EXPORTS = exports.exp
CFLAGS = -O2 -G0 -Wall -ffreestanding
LIBS =
PSP_FW_VERSION = 660
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
