CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
DRM_INC = -I/home/danilo/Documentos/TABLET/drm_include -I/home/danilo/Documentos/TABLET/drm_include/libdrm
CFLAGS ?= -O3 -march=armv8.2-a+crypto -fPIC -Wall -std=gnu11 -fno-builtin -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 $(DRM_INC)
LDFLAGS ?= -shared -lpthread -ldl

TARGETS = libgpu_interface_driver.so libmali_shim.so libhybris_gbm_bridge.so

all: $(TARGETS)

libgpu_interface_driver.so: gpu_interface_driver.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

libmali_shim.so: gpu_interface_driver.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

libhybris_gbm_bridge.so: hybris_gbm_bridge.c
	$(CC) -O2 -fPIC -Wall $(DRM_INC) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)

.PHONY: all clean
