# NINMODS in-memory Il2Cpp dumper — ndk-build makefile.
# Target ABIs: arm64-v8a, armeabi-v7a (see Application.mk).

LOCAL_PATH := $(call my-dir)

# ---------------------------------------------------------------------------
# Prebuilt Dobby (static) — one archive per ABI.
# ---------------------------------------------------------------------------
include $(CLEAR_VARS)
LOCAL_MODULE           := libdobby
LOCAL_SRC_FILES        := Dobby/$(TARGET_ARCH_ABI)/libdobby.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/Dobby
include $(PREBUILT_STATIC_LIBRARY)

# ---------------------------------------------------------------------------
# libNINMODS.so
# ---------------------------------------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE := NINMODS

LOCAL_CFLAGS := -fvisibility=hidden -ffunction-sections -fdata-sections -w
LOCAL_CPPFLAGS := -fvisibility=hidden -ffunction-sections -fdata-sections -w \
                  -std=c++17 -fno-rtti -fno-exceptions
LOCAL_LDFLAGS += -Wl,--gc-sections -Wl,--strip-all

LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv2 -lz

LOCAL_C_INCLUDES := $(LOCAL_PATH) \
                    $(LOCAL_PATH)/include \
                    $(LOCAL_PATH)/Dobby \
                    $(LOCAL_PATH)/imgui \
                    $(LOCAL_PATH)/Unity \
                    $(LOCAL_PATH)/Struct \
                    $(LOCAL_PATH)/Core \
                    $(LOCAL_PATH)/Icon \
                    $(LOCAL_PATH)/xdl/include

# Sources. Dobby ships a prebuilt archive, so its headers stay header-only here.
LOCAL_SRC_FILES := \
    Main.cpp \
    Unity/Il2Cpp.cpp \
    KittyMemory/KittyMemory.cpp \
    xdl/xdl.c \
    xdl/xdl_iterate.c \
    xdl/xdl_linker.c \
    xdl/xdl_lzma.c \
    xdl/xdl_util.c \
    imgui/imgui.cpp \
    imgui/imgui_draw.cpp \
    imgui/imgui_tables.cpp \
    imgui/imgui_widgets.cpp \
    imgui/imgui_impl_android.cpp \
    imgui/imgui_impl_opengl3.cpp

LOCAL_STATIC_LIBRARIES := libdobby
LOCAL_CPP_FEATURES     := exceptions

include $(BUILD_SHARED_LIBRARY)
