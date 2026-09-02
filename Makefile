ARCHS = arm64
TARGET = iphone:clang:latest:14.0
include $(THEOS)/makefiles/common.mk

TWEAK_NAME = UnityAudioPlugin
UnityAudioPlugin_FILES = Tweak.xm SPSpeedManager.m SPFloatingBall.m SPControlPanel.m SPLogManager.m fishhook.c LuckySpeeder.c LuckySpeeder.m hwbphook.c port_clock_gettime.c mach_excServer.c
UnityAudioPlugin_CFLAGS = -fobjc-arc -Wno-error -D_GNU_SOURCE
UnityAudioPlugin_FRAMEWORKS = UIKit CoreGraphics SpriteKit CoreMotion CoreFoundation
UnityAudioPlugin_LDFLAGS = -Xlinker -dead_strip_dylibs

include $(THEOS_MAKE_PATH)/tweak.mk
