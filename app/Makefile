# fm1-midi-bridge: SDK application Makefile, derived from the AC79 SDK demo_ble
# Makefile (Apache-2.0). Builds against an SDK checkout outside this tree.
#
#   make            build/fm1bridge.elf and build/app.bin
#   make clean
#
# Variables: SDK (default ~/fw-AC79_AIoT_SDK), TOOL_DIR (default ~/.jieli/toolchain/pi32v2/bin)
SDK      ?= $(HOME)/fw-AC79_AIoT_SDK
TOOL_DIR ?= $(HOME)/.jieli/toolchain/pi32v2/bin
PROJ     := $(abspath .)
CC    := $(TOOL_DIR)/clang
CXX   := $(TOOL_DIR)/clang
LD    := $(TOOL_DIR)/lto-wrapper
AR    := $(TOOL_DIR)/lto-ar
MKDIR := mkdir -p
RM    := rm -rf
export OBJDUMP := $(TOOL_DIR)/objdump
export OBJCOPY := $(TOOL_DIR)/objcopy
export OBJSIZEDUMP := $(TOOL_DIR)/objsizedump
SYS_LIB_DIR := $(TOOL_DIR)/../lib/r3
SYS_INC_DIR := $(TOOL_DIR)/../include
EXT_CFLAGS  := -D__SHELL__
export PATH:=$(TOOL_DIR):$(PATH)
OUT_DIR   := $(abspath $(PROJ)/..)/build
OUT_ELF   := $(OUT_DIR)/fm1bridge.elf
OBJ_FILE  := $(OUT_ELF).objs.txt
BUILD_DIR := $(OUT_DIR)/objs
# 编译参数设置
CFLAGS := \
	-target pi32v2 \
	-integrated-as \
	-mcpu=r3 \
	-mfprev1 \
	-Wuninitialized \
	-Wno-invalid-noreturn \
	-fno-common \
	-integrated-as \
	-Oz \
	-g \
	-flto \
	-fallow-pointer-null \
	-fprefer-gnu-section \
	-femulated-tls \
	-Wno-shift-negative-value \
	-Wframe-larger-than=2560 \
	-mllvm -pi32v2-large-program=true \
	-fms-extensions \
	-w \


# C++额外的编译参数
CXXFLAGS :=


# 宏定义
DEFINES := \
	-DSUPPORT_MS_EXTENSIONS \
	-DCONFIG_RELEASE_ENABLE \
	-DCONFIG_CPU_WL82 \
	-DCONFIG_FREE_RTOS_ENABLE \
	-DCONFIG_THREAD_ENABLE \
	-DCONFIG_AUDIO_ONCHIP \
	-D__GCC_PI32V2__ \
	-DCONFIG_NEW_ECC_ENABLE \
	-DCONFIG_EQ_SUPPORT_ASYNC \
	-DEQ_CORE_V1 \
	-DCONFIG_SFC_ENABLE \
	-DCONFIG_NO_SDRAM_ENABLE \
	-DCONFIG_BT_ENABLE=1 \
	-DEVENT_HANDLER_NUM_CONFIG=2 \
	-DEVENT_TOUCH_ENABLE_CONFIG=0 \
	-DEVENT_POOL_SIZE_CONFIG=256 \
	-DCONFIG_EVENT_KEY_MAP_ENABLE=0 \
	-DTIMER_POOL_NUM_CONFIG=15 \
	-DAPP_ASYNC_POOL_NUM_CONFIG=0 \
	-DVFS_ENABLE=1 \
	-DUSE_SDFILE_NEW=1 \
	-DSDFILE_STORAGE=1 \
	-DVFS_FILE_POOL_NUM_CONFIG=8 \
	-DVM_MAX_PAGE_ALIGN_SIZE_CONFIG=64*1024 \
	-DVM_MAX_SECTOR_ALIGN_SIZE_CONFIG=64*1024 \
	-DVM_ITEM_MAX_NUM=256 \
	-DCONFIG_TWS_ENABLE \
	-DCONFIG_BTCTRLER_TASK_DEL_ENABLE \
	-DCONFIG_LMP_CONN_SUSPEND_ENABLE \
	-DCONFIG_LMP_REFRESH_ENCRYPTION_KEY_ENABLE \
	-DCONFIG_ITEM_FORMAT_VM \


DEFINES += $(EXT_CFLAGS) # 额外的一些定义

# 头文件搜索路径
INCLUDES := \
	-I$(SDK)/include_lib/c++ \
	-I$(SDK)/include_lib/c++/include \
	-I$(SDK)/include_lib/newlib/include \
	-I$(SDK)/include_lib/c++/simple_pthread \
	-I$(SDK)/include_lib \
	-I$(SDK)/include_lib/driver \
	-I$(SDK)/include_lib/driver/device \
	-I$(SDK)/include_lib/driver/cpu/wl82 \
	-I$(SDK)/include_lib/system \
	-I$(SDK)/include_lib/system/generic \
	-I$(SDK)/include_lib/btctrler \
	-I$(SDK)/include_lib/btctrler/port/wl82 \
	-I$(SDK)/include_lib/update \
	-I$(SDK)/include_lib/btstack/third_party/common \
	-I$(SDK)/include_lib/btstack/third_party/rcsp \
	-I$(SDK)/include_lib/utils \
	-I$(SDK)/include_lib/utils/syscfg \
	-I$(SDK)/include_lib/utils/event \
	-I$(SDK)/include_lib/media \
	-I$(SDK)/include_lib/media/cpu/wl82 \
	-I$(SDK)/apps \
	-I$(PROJ)/include \
	-I$(SDK)/apps/common \
	-I$(SDK)/apps/common/include \
	-I$(SDK)/apps/common/config/include \
	-I$(SDK)/include_lib/btstack \
	-I$(SDK)/include_lib/net \
	-I$(SDK)/include_lib/net/lwip_2_1_3/lwip/src/include \
	-I$(SDK)/include_lib/net/lwip_2_1_3/lwip/src/include/lwip \
	-I$(SDK)/include_lib/net/lwip_2_1_3/lwip/port \
	-I$(SDK)/include_lib/net/lwip_2_1_3/lwip/app \
	-I$(SDK)/apps/common/ble/include \
	-I$(SDK)/apps/common/usb \
	-I$(SDK)/apps/common/usb/device \
	-I$(SDK)/apps/common/usb/include \
	-I$(SDK)/apps/common/usb/include/device \
	-I$(SDK)/include_lib/driver/device/usb \
	-I$(SDK)/include_lib/driver/device/usb/device \
	-I$(PROJ)/usb \
	-I$(PROJ) \
	-I$(PROJ)/update \
	-I$(PROJ)/ui \
	-I$(PROJ)/input \
	-I$(PROJ)/midi \
	-I$(SDK)/include_lib/utils/btmesh \
	-I$(SDK)/include_lib/utils/btmesh/adaptation \


# 需要编译的 .c 文件
c_SRC_FILES := \
	$(SDK)/apps/common/ble/le_24g_deal.c \
	$(SDK)/apps/common/ble/le_hogp.c \
	$(SDK)/apps/common/ble/le_net_central.c \
	$(SDK)/apps/common/ble/le_net_cfg.c \
	$(SDK)/apps/common/ble/le_net_cfg_dui.c \
	$(SDK)/apps/common/ble/le_net_cfg_tencent.c \
	$(SDK)/apps/common/ble/le_net_cfg_turing.c \
	$(PROJ)/bt_ble/ble_midi.c \
	$(PROJ)/bt_ble/ble_host.c \
	$(PROJ)/bt_ble/ble_port.c \
	$(PROJ)/bt_ble/adv_parse.c \
	$(SDK)/apps/common/ble/mesh/examples/AliGenie_fan.c \
	$(SDK)/apps/common/ble/mesh/examples/AliGenie_light.c \
	$(SDK)/apps/common/ble/mesh/examples/AliGenie_socket.c \
	$(SDK)/apps/common/ble/mesh/examples/TUYA_light.c \
	$(SDK)/apps/common/ble/mesh/examples/generic_onoff_client.c \
	$(SDK)/apps/common/ble/mesh/examples/generic_onoff_server.c \
	$(SDK)/apps/common/ble/mesh/examples/onoff_tobe_provision.c \
	$(SDK)/apps/common/ble/mesh/examples/provisioner.c \
	$(SDK)/apps/common/ble/mesh/examples/vendor_client.c \
	$(SDK)/apps/common/ble/mesh/examples/vendor_server.c \
	$(SDK)/apps/common/ble/mesh/mesh_config_common.c \
	$(SDK)/apps/common/ble/mesh/model_api.c \
	$(SDK)/apps/common/ble/mesh/unix_timestamp.c \
	$(SDK)/apps/common/ble/multi_demo/le_multi_client.c \
	$(SDK)/apps/common/ble/multi_demo/le_multi_common.c \
	$(SDK)/apps/common/ble/multi_demo/le_multi_trans.c \
	$(SDK)/apps/common/config/bt_profile_config.c \
	$(SDK)/apps/common/config/log_config/app_config.c \
	$(SDK)/apps/common/config/log_config/lib_btctrler_config.c \
	$(SDK)/apps/common/config/log_config/lib_btstack_config.c \
	$(SDK)/apps/common/config/log_config/lib_driver_config.c \
	$(SDK)/apps/common/config/log_config/lib_system_config.c \
	$(SDK)/apps/common/config/log_config/lib_update_config.c \
	$(SDK)/apps/common/config/user_cfg.c \
	$(SDK)/apps/common/debug/debug.c \
	$(SDK)/apps/common/debug/debug_user.c \
	$(SDK)/apps/common/key/adkey.c \
	$(SDK)/apps/common/key/key_driver.c \
	$(SDK)/apps/common/net/wifi_conf.c \
	$(SDK)/apps/common/system/init.c \
	$(SDK)/apps/common/system/system_reset_reason.c \
	$(SDK)/apps/common/system/version.c \
	$(SDK)/apps/common/update/update.c \
	$(PROJ)/app_main.c \
	$(PROJ)/bridge.c \
	$(PROJ)/update/fm1_update.c \
	$(PROJ)/update/fm1_update_port.c \
	$(PROJ)/ui/lcd.c \
	$(PROJ)/ui/midilog.c \
	$(PROJ)/ui/ui.c \
	$(PROJ)/ui/ui_menu.c \
	$(PROJ)/ui/ui_pages.c \
	$(PROJ)/usb/usb_port.c \
	$(PROJ)/board/board.c \
	$(PROJ)/bt_ble/ble.c \
	$(PROJ)/usb/usb_midi.c \
	$(PROJ)/usb/usb_device.c \
	$(PROJ)/midi/midi_msg.c \
	$(PROJ)/midi/blemidi.c \
	$(PROJ)/midi/usbmidi.c \
	$(PROJ)/midi/router.c \
	$(PROJ)/midi/panic.c \
	$(PROJ)/settings.c \
	$(PROJ)/util/crc16.c \
	$(PROJ)/input/matrix.c \
	$(PROJ)/input/matrix_port.c \
	$(PROJ)/settings_port.c \
	$(SDK)/apps/common/usb/usb_config.c \
	$(SDK)/apps/common/usb/usb_epbuf_manager.c \
	$(SDK)/apps/common/usb/device/descriptor.c \
	$(SDK)/apps/common/usb/device/task_pc.c \
	$(SDK)/apps/common/usb/device/user_setup.c \
	$(SDK)/apps/common/usb/device/msd_upgrade.c \
	$(SDK)/cpu/wl82/debug.c \
	$(SDK)/cpu/wl82/iic.c \
	$(SDK)/cpu/wl82/key/adc_api.c \
	$(SDK)/cpu/wl82/key/ctmu.c \
	$(SDK)/cpu/wl82/key/irflt.c \
	$(SDK)/cpu/wl82/key/plcnt.c \
	$(SDK)/cpu/wl82/key/rdec.c \
	$(SDK)/cpu/wl82/port_waked_up.c \
	$(SDK)/cpu/wl82/setup.c \


# 需要编译的 .S 文件
S_SRC_FILES := \
	$(SDK)/include_lib/btctrler/version.z.S \
	$(SDK)/include_lib/btstack/version.z.S \
	$(SDK)/include_lib/driver/version.z.S \
	$(SDK)/include_lib/media/version.z.S \
	$(SDK)/include_lib/net/version.z.S \
	$(SDK)/include_lib/server/version.z.S \
	$(SDK)/include_lib/system/version.z.S \
	$(SDK)/include_lib/update/version.z.S \
	$(SDK)/include_lib/utils/version.z.S \
	$(SDK)/lib/net/version.z.S \
	$(SDK)/lib/server/version.z.S \
	$(SDK)/lib/utils/version.z.S \


# 需要编译的 .s 文件
s_SRC_FILES :=


# 需要编译的 .cpp 文件
cpp_SRC_FILES :=


# 需要编译的 .cc 文件
cc_SRC_FILES :=


# 需要编译的 .cxx 文件
cxx_SRC_FILES :=


# 链接参数
LFLAGS := \
	--plugin-opt=-pi32v2-always-use-itblock=false \
	--plugin-opt=-enable-ipra=true \
	--plugin-opt=-pi32v2-merge-max-offset=4096 \
	--plugin-opt=-pi32v2-enable-simd=true \
	--plugin-opt=mcpu=r3 \
	--plugin-opt=-global-merge-on-const \
	--plugin-opt=-inline-threshold=5 \
	--plugin-opt=-inline-max-allocated-size=32 \
	--plugin-opt=-inline-normal-into-special-section=true \
	--plugin-opt=-dont-used-symbol-list=malloc,free,sprintf,printf,puts,putchar \
	--plugin-opt=save-temps \
	--plugin-opt=-pi32v2-enable-rep-memop \
	--plugin-opt=-emulated-tls \
	--sort-common \
	--plugin-opt=-used-symbol-file=$(SDK)/cpu/wl82/sdk_used_list.used \
	--plugin-opt=-enable-movable-region=true \
	--plugin-opt=-movable-region-section-prefix=.movable.slot. \
	--plugin-opt=-movable-region-stub-section-prefix=.movable.stub. \
	--plugin-opt=-movable-region-prefix=.movable.region. \
	--plugin-opt=-movable-region-stub-prefix=__movable_stub_ \
	--plugin-opt=-movable-region-stub-swi-number=-2 \
	--plugin-opt=-movable-region-map-file-list=$(SDK)/apps/common/movable/funcname.txt \
	--plugin-opt=-movable-region-section-map-file-list=$(SDK)/apps/common/movable/section.txt \
	--plugin-opt=-movable-region-exclude-func-file-list=$(SDK)/apps/common/movable/exclude.txt \
	--plugin-opt=-pi32v2-large-program=true \
	--start-group \
	$(SDK)/include_lib/newlib/pi32v2-lib/libm.a \
	$(SDK)/include_lib/newlib/pi32v2-lib/libc.a \
	$(SDK)/include_lib/newlib/pi32v2-lib/libcompiler_rt.a \
	--start-group  \
	$(SDK)/cpu/wl82/liba/fs.a \
	$(SDK)/cpu/wl82/liba/event.a \
	$(SDK)/cpu/wl82/liba/system.a \
	$(SDK)/cpu/wl82/liba/cfg_tool.a \
	$(SDK)/cpu/wl82/liba/cpu.a \
	$(SDK)/cpu/wl82/liba/common_lib.a \
	$(SDK)/cpu/wl82/liba/wl_rf_common.a \
	$(SDK)/cpu/wl82/liba/btctrler.a \
	$(SDK)/cpu/wl82/liba/btstack.a \
	$(SDK)/cpu/wl82/liba/crypto_toolbox_Osize.a \
	$(SDK)/cpu/wl82/liba/lib_ccm_aes.a \
	$(SDK)/cpu/wl82/liba/lib_sig_mesh.a \
	$(SDK)/cpu/wl82/liba/update.a \
	--end-group \
	-T$(SDK)/cpu/wl82/sdk.ld \
	-M=$(OUT_DIR)/fm1bridge.map \
	--end-group  \
	--plugin-opt=mcpu=r3 \
	--plugin-opt=-mattr=+fprev1 \



c_OBJS    := $(c_SRC_FILES:%.c=%.c.o)
S_OBJS    := $(S_SRC_FILES:%.S=%.S.o)
s_OBJS    := $(s_SRC_FILES:%.s=%.s.o)
cpp_OBJS  := $(cpp_SRC_FILES:%.cpp=%.cpp.o)
cxx_OBJS  := $(cxx_SRC_FILES:%.cxx=%.cxx.o)
cc_OBJS   := $(cc_SRC_FILES:%.cc=%.cc.o)

OBJS      := $(c_OBJS) $(S_OBJS) $(s_OBJS) $(cpp_OBJS) $(cxx_OBJS) $(cc_OBJS)
DEP_FILES := $(OBJS:%.o=%.d)


OBJS      := $(addprefix $(BUILD_DIR)/, $(patsubst $(PROJ)/%,app/%,$(patsubst $(SDK)/%,sdk/%,$(OBJS))))
DEP_FILES := $(addprefix $(BUILD_DIR)/, $(patsubst $(PROJ)/%,app/%,$(patsubst $(SDK)/%,sdk/%,$(DEP_FILES))))


VERBOSE ?= 0
ifeq ($(VERBOSE), 1)
QUITE :=
else
QUITE := @
endif

# 一些旧的 make 不支持 file 函数，需要 make 的时候指定 LINK_AT=0 make
LINK_AT ?= 1

# 表示下面的不是一个文件的名字，无论是否存在 all, clean, pre_build 这样的文件
# 还是要执行命令
# see: https://www.gnu.org/software/make/manual/html_node/Phony-Targets.html
.PHONY: all clean pre_build

# 不要使用 make 预设置的规则
# see: https://www.gnu.org/software/make/manual/html_node/Suffix-Rules.html
.SUFFIXES:

all: pre_build $(OUT_ELF)
	$(info +POST-BUILD)
	$(QUITE) sh $(PROJ)/../tools/mkapp.sh $(OUT_ELF) $(OUT_DIR)/app.bin

pre_build:
	$(info +PRE-BUILD)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P $(SDK)/cpu/wl82/sdk_used_list.c -o $(SDK)/cpu/wl82/sdk_used_list.used
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P $(SDK)/apps/common/movable/section.c -o $(SDK)/apps/common/movable/section.txt
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P $(SDK)/cpu/wl82/sdk_ld.c -o $(SDK)/cpu/wl82/sdk.ld
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -D__LD__ -E -P $(SDK)/cpu/wl82/tools/isd_config_rule_loader.c -o $(SDK)/cpu/wl82/tools/isd_config_loader.ini

clean:
	$(QUITE) $(RM) $(OUT_ELF)
	$(QUITE) $(RM) $(BUILD_DIR)



ifeq ($(LINK_AT), 1)
$(OUT_ELF): $(OBJS)
	$(info +LINK $@)
	$(shell $(MKDIR) $(@D))
	$(file >$(OBJ_FILE), $(OBJS))
	$(QUITE) $(LD) -o $(OUT_ELF) @$(OBJ_FILE) $(LFLAGS) $(LIBPATHS) $(LIBS)
else
$(OUT_ELF): $(OBJS)
	$(info +LINK $@)
	$(shell $(MKDIR) $(@D))
	$(QUITE) $(LD) -o $(OUT_ELF) $(OBJS) $(LFLAGS) $(LIBPATHS) $(LIBS)
endif


define COMPILE_C
	$(info +CC $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MM -MT $@ $< -o $(@:.o=.d)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@
endef
define COMPILE_S
	$(info +AS $<)
	$(QUITE) $(MKDIR) $(@D)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -MM -MT $@ $< -o $(@:.o=.d)
	$(QUITE) $(CC) $(CFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@
endef

$(BUILD_DIR)/sdk/%.c.o : $(SDK)/%.c
	$(COMPILE_C)
$(BUILD_DIR)/app/%.c.o : $(PROJ)/%.c
	$(COMPILE_C)
$(BUILD_DIR)/sdk/%.S.o : $(SDK)/%.S
	$(COMPILE_S)
$(BUILD_DIR)/app/%.S.o : $(PROJ)/%.S
	$(COMPILE_S)

-include $(DEP_FILES)
