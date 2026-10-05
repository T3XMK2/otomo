# otomo_body: the cat's body inside the game (device/psp/otomo_body/main.c). Build in WSL with pspdev: ../build.sh otomo_body
TARGET = otomo_body
OBJS = main.o hooks.o proto.o objs.o exports.o
PRX_EXPORTS = exports.exp
CFLAGS = -O2 -G0 -Wall -ffreestanding
LIBS = -lpspnet_apctl -lpspnet_inet -lpspnet -lpsputility
PSP_FW_VERSION = 660
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak
