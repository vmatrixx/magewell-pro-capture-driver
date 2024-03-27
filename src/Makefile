
ProCapture-objs :=	  \
	sources/dma/mw-dma-mem.o					\
	sources/dma/mw-dma-user.o					\
	sources/dma/mw-dma-kernel.o					\
	sources/dma/mw-dma-phy.o					\
	sources/ospi/ospi-linux.o					\
	sources/ospi/linux-file.o  					\
	sources/avstream/v4l2.o				        \
	sources/avstream/v4l2-sg-buf.o				\
	sources/avstream/mw-event-dev.o				\
	sources/avstream/mw-event-ioctl.o			\
	sources/avstream/alsa.o				        \
	sources/avstream/capture.o

ProCapture-objs += ProCaptureLib.o

obj-m = ProCapture.o

EXTRA_CFLAGS += -I$(src) -I$(src)/public -I$(src)/sources
# strip debug symbols
EXTRA_LDFLAGS += -S

ifneq ($(KERNELRELEASE),)
KERNELDIR ?= /lib/modules/$(KERNELRELEASE)/build
else
KERNELDIR ?= /lib/modules/$(shell uname -r)/build
endif

PWD := $(shell pwd)

all: ProCaptureLib.o
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

OS_ARCH := $(shell uname -m | sed -e 's/i.86/i386/')
ifneq ($(OS_ARCH), i386)
	ProCaptureLibFile = ProCaptureLib_64.lib
else
	ProCaptureLibFile = ProCaptureLib_32.lib
endif
ProCaptureLib.o:
	cp $(ProCaptureLibFile) ProCaptureLib.o
	touch .ProCaptureLib.o.cmd


