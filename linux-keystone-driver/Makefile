DRIVER = keystone-driver.ko

ifneq ($(KERNELRELEASE),)
	keystone-driver-y := \
		keystone.o \
		keystone-page.o \
		keystone-ioctl.o \
		keystone-enclave.o \
	  keystone-sbi.o
	obj-m += keystone-driver.o

ifeq ($(KEYSTONE_SDK_DIR),)
    $(error KEYSTONE_SDK_DIR not defined)
endif

	ccflags-y := -I$(KEYSTONE_SDK_DIR)/include/shared -DKEYSTONE_PLAT_$(KEYSTONE_PLATFORM)
else

PWD := $(shell pwd)
LINUXSRC ?= $(PWD)/../linux

default:
	$(MAKE) -C $(LINUXSRC) ARCH=riscv CROSS_COMPILE=riscv64-buildroot-linux-gnu- M=$(PWD)

endif

clean:
	rm -rvf *.o *.ko *.order *.symvers *.mod.c .tmp_versions .*o.cmd .*.cmd *.mod
