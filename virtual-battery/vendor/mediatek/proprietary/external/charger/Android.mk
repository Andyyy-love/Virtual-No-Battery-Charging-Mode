LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    common.cpp \
    lights.cpp \
    bootlogo.cpp \
    main.cpp \
    key_control.cpp \
    charging_control.cpp \
    monitor_hang.cpp

LOCAL_CFLAGS += $(MTK_CDEFS)

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../libshowlogo/                   \
 $(LOCAL_PATH)/include \
 $(TOP)/external/zlib/ \
 $(TOP)/external/libdrm/include

LOCAL_MODULE:= kpoc_charger
LOCAL_SYSTEM_EXT_MODULE:=true
# Move to system partition for Android O migration
#LOCAL_PROPRIETARY_MODULE := true
#LOCAL_MODULE_OWNER := mtk
LOCAL_INIT_RC := kpoc_charger.rc

#bobule workaround pdk build error, needing review
LOCAL_MULTILIB := first

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils \
    libc \
    libstdc++ \
    libz \
    libdl \
    liblog \
    libui \
    libdrm\
    libhardware_legacy \
    libsuspend \
    android.hardware.health@2.0 \
    android.hardware.health-V1-ndk \
    libbase \
    libbinder \
    libbinder_ndk \
    libhidlbase \
    libhidltransport \

LOCAL_STATIC_LIBRARIES := \
    libhealthhalutils \
    android.hardware.light-V1-cpp

# Do not build healthd since we use android.hardware.health@2.0
LOCAL_OVERRIDES_MODULES := healthd

KPOC_ENABLE_ANIME := yes
ifeq ($(KPOC_ENABLE_ANIME),yes)
LOCAL_CFLAGS = -DKPOC_ENABLE_ANIME
LOCAL_SHARED_LIBRARIES += libshowlogo
endif

include $(MTK_EXECUTABLE)