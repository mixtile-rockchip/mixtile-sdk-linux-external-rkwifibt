ifeq ($(CONFIG_RK960),)
CONFIG_RK960=m
endif

# Only RK962 support
ifneq ($(findstring $(RK_ENABLE_WIFI_CHIP),"RK962"),)
CONFIG_RK96X_SUPPORT_POWERSAVE=y
EXTRA_CFLAGS += -DCONFIG_RK962
endif
# Firmware loading (fwio.c):
#   y -> filp_open + kernel_read (-DFW_LOADER_FROM_FOPEN)
#   n -> request_firmware
#
# Kernel CONFIG_ANDROID=y does not indicate userland OS; this tree defaults to file-path loading.
# For request_firmware, set explicitly: export RK_FW_LOAD_FROM_FOPEN=n or RK_FW_LOAD_FROM_FOPEN=n make ...
RK_FW_LOAD_FROM_FOPEN ?= y
$(info RK_FW_LOAD_FROM_FOPEN=$(RK_FW_LOAD_FROM_FOPEN))

ifeq ($(RK_FW_LOAD_FROM_FOPEN),y)
EXTRA_CFLAGS += -DFW_LOADER_FROM_FOPEN
endif

NAME = rk96x

OBJS := \
	fwio.o \
	txrx.o \
	main.o \
	queue.o \
	hwio.o \
	bh.o \
	wsm.o \
	sta.o \
	ap.o \
	scan.o \
	debug.o \
	pm.o \
	sdio.o \
	vendor.o \
	config.o

ifeq ($(CONFIG_RK960_ITP),y)
OBJS += itp.o
endif

ifneq ($(findstring $(RK_ENABLE_WIFI_CHIP),"RK962"),)
obj-m = rk96x_wake_aov_irq.o
endif

EXTRA_CFLAGS += -Wno-unused-variable

#EXTRA_CFLAGS += -DROAM_OFFLOAD
EXTRA_CFLAGS += -DHW_RESET
#EXTRA_CFLAGS += -DP2P_MULTIVIF
#EXTRA_CFLAGS += -DCUSTOM_FEATURE
#EXTRA_CFLAGS += -DMCAST_FWDING
EXTRA_CFLAGS += -DRK960_SUSPEND_RESUME_FILTER_ENABLE
EXTRA_CFLAGS += -DAP_AGGREGATE_FW_FIX
EXTRA_CFLAGS += -DAP_HT_CAP_UPDATE
EXTRA_CFLAGS += -DROC_DEBUG
# Extra IE for probe response from upper layer is needed in P2P GO
# For offloading probe response to FW, the extra IE must be included
# in the probe response template
EXTRA_CFLAGS += -DPROBE_RESP_EXTRA_IE
#EXTRA_CFLAGS += -DIPV6_FILTERING

#EXTRA_CFLAGS += -DCONFIG_RK960_5GHZ_SUPPORT
#EXTRA_CFLAGS += -DCONFIG_RK960_WAPI_SUPPORT
#EXTRA_CFLAGS += -DCONFIG_RK960_USE_STE_EXTENSIONS
#EXTRA_CFLAGS += -DCONFIG_RK960_DISABLE_BEACON_HINTS
EXTRA_CFLAGS += -DCONFIG_RK960_DEBUGFS
#EXTRA_CFLAGS += -DCONFIG_RK960_WSM_DUMPS
#EXTRA_CFLAGS += -DCONFIG_RK960_WSM_DUMPS_SHORT
#EXTRA_CFLAGS += -DCONFIG_RK960_ITP
#EXTRA_CFLAGS += -DIBSS_SUPPORT
#EXTRA_CFLAGS += -DCUSTOM_REG_SET
EXTRA_CFLAGS += -DENABLE_DBGFS_WRITE
#EXTRA_CFLAGS += -DSUPPORT_FWCR
#EXTRA_CFLAGS += -DDISABLE_TX_POLICY
EXTRA_CFLAGS += -DRK960_CSYNC_ADJUST
ifeq ($(CONFIG_RK96X_SUPPORT_POWERSAVE),y)
EXTRA_CFLAGS += -DENABLE_DBGFS_WRITE
EXTRA_CFLAGS += -DSUPPORT_FWCR
EXTRA_CFLAGS += -DSUPPORT_RK962_POWERSAVE
#EXTRA_CFLAGS += -DRK96X_WIFI_ONLY
endif
EXTRA_CFLAGS += -DCONFIG_OFFLINE_LOG_SUPPORT
#EXTRA_CFLAGS += -DCONFIG_OFFLINE_LOG_TEST
#EXTRA_CFLAGS += -DRk960_WIFI_MAC_ADDR_FROM_VENDOR_STORAGE

ifneq ($(shell grep -o "CONFIG_RFKILL=y" $(WIFI_BUILD_KERNEL_OBJ_DIR)/.config),)
EXTRA_CFLAGS += -DRFKILL_RK
endif

GCC_VER_49 := $(shell echo `$(CC) -dumpversion | cut -f1-2 -d.` \>= 4.9 | bc )
ifeq ($(GCC_VER_49),1)
EXTRA_CFLAGS += -Wno-date-time  # Fix compile error && warning on gcc 4.9 and later
#EXTRA_CFLAGS += -Wno-error=date-time
endif

obj-$(CONFIG_RK960) += $(NAME).o
$(NAME)-objs= $(OBJS)

#EXTRA_CFLAGS += -g
EXTRA_CFLAGS += -Wno-incompatible-pointer-types-discards-qualifiers

ifneq ($(KERNELRELEASE),)

#EXTRA_LDFLAGS += --strip-debug

else

### @make -C $(KROOT) M=$(PWD) modules
### @make -C $(KROOT) M=$(PWD) clean
all:
	$(MAKE) -C $(KERNEL_DIR) M=$(shell pwd) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
	$(CROSS_COMPILE)strip --strip-debug $(shell pwd)/rk96x.ko
ifneq ($(findstring $(RK_ENABLE_WIFI_CHIP),"RK962"),)
	$(CROSS_COMPILE)strip --strip-debug $(shell pwd)/rk96x_wake_aov_irq.ko
endif

	echo "WIFI M_OUT_DIR = "$(M_OUT_DIR)

	mkdir -p $(M_OUT_DIR)/rk96x/fw

	@cp -f $(shell pwd)/rk96x.ko  $(M_OUT_DIR)
ifneq ($(findstring $(RK_ENABLE_WIFI_CHIP),"RK962"),)
	@cp -f $(shell pwd)/rk96x_wake_aov_irq.ko  $(M_OUT_DIR)
endif
	@cp -f $(shell pwd)/wifi/rk96x_wifi_loader.bin  $(M_OUT_DIR)/rk96x/fw
	@cp -f $(shell pwd)/wifi/rk96x_wifi.conf  $(M_OUT_DIR)/rk96x/fw
	@chmod 644 $(M_OUT_DIR)/rk96x/fw/rk96x_wifi.conf
ifeq ($(RK_ENABLE_BLUETOOTH),y)
	@cp -f $(shell pwd)/bt/rk96x_bt_loader.bin  $(M_OUT_DIR)/rk96x/fw
endif

ifneq ($(findstring $(RK_ENABLE_WIFI_CHIP),"RK960"),)
	echo "cp rk960 fw"
	@cp -f $(shell pwd)/wifi/rk960*  $(M_OUT_DIR)/rk96x/fw
ifeq ($(RK_ENABLE_BLUETOOTH),y)
	@cp -f $(shell pwd)/bt/rk960*  $(M_OUT_DIR)/rk96x/fw
endif
else
	@cp -f $(shell pwd)/wifi/rk962*  $(M_OUT_DIR)/rk96x/fw
ifeq ($(RK_ENABLE_BLUETOOTH),y)
	echo "cp rk962 fw, btmcu fw is $(RK_WIFI_BTMCU_FW)"
	@cp -f $(shell pwd)/bt/rk962_bt.bin  $(M_OUT_DIR)/rk96x/fw
	@cp -f $(shell pwd)/bt/rk962_bt_rf.bin  $(M_OUT_DIR)/rk96x/fw
	@cp -f $(shell pwd)/bt/rk962_bt_v2.bin  $(M_OUT_DIR)/rk96x/fw
	@cp -f $(shell pwd)/bt/rk962_bt_rf_v2.bin  $(M_OUT_DIR)/rk96x/fw
	@cp -f $(shell pwd)/bt/$(RK_WIFI_BTMCU_FW) $(M_OUT_DIR)/rk96x/fw/rk962_bt_mcu.bin
endif
endif

clean:
	rm -rf *.o *.ko *.mod .*.cmd *.order *.symvers out rk96x.mod.c rk96x_wake_aov_irq.mod.c

endif

