#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

typedef struct _GList {
    void *data;
    struct _GList *next;
    struct _GList *prev;
} GList;

static void *g_fake_bl_dev = NULL;
static void *g_backlight_enum = NULL;

static GList *(*real_query_by_subsystem)(void *client, const char *subsys) = NULL;
static const char *(*real_get_name)(void *dev) = NULL;
static const char *(*real_get_subsystem)(void *dev) = NULL;
static const char *(*real_get_sysfs_attr)(void *dev, const char *name) = NULL;
static int (*real_get_sysfs_attr_as_int)(void *dev, const char *name) = NULL;
static int (*real_get_sysfs_attr_as_int_uncached)(void *dev, const char *name) = NULL;
static void (*real_add_match_subsystem)(void *en, const char *subsys) = NULL;
static GList *(*real_enumerator_execute)(void *en) = NULL;
static void *(*real_g_object_ref)(void *obj) = NULL;
static void (*real_g_object_unref)(void *obj) = NULL;
static GList *(*real_g_list_prepend)(GList *list, void *data) = NULL;
static void (*real_g_list_free)(GList *list) = NULL;

static void init_syms(void) {
    if (real_get_name) return;
    real_query_by_subsystem = dlsym(RTLD_NEXT, "g_udev_client_query_by_subsystem");
    real_get_name = dlsym(RTLD_NEXT, "g_udev_device_get_name");
    real_get_subsystem = dlsym(RTLD_NEXT, "g_udev_device_get_subsystem");
    real_get_sysfs_attr = dlsym(RTLD_NEXT, "g_udev_device_get_sysfs_attr");
    real_get_sysfs_attr_as_int = dlsym(RTLD_NEXT, "g_udev_device_get_sysfs_attr_as_int");
    real_get_sysfs_attr_as_int_uncached = dlsym(RTLD_NEXT, "g_udev_device_get_sysfs_attr_as_int_uncached");
    real_add_match_subsystem = dlsym(RTLD_NEXT, "g_udev_enumerator_add_match_subsystem");
    real_enumerator_execute = dlsym(RTLD_NEXT, "g_udev_enumerator_execute");
    real_g_object_ref = dlsym(RTLD_NEXT, "g_object_ref");
    real_g_object_unref = dlsym(RTLD_NEXT, "g_object_unref");
    real_g_list_prepend = dlsym(RTLD_NEXT, "g_list_prepend");
    real_g_list_free = dlsym(RTLD_NEXT, "g_list_free");
}

static GList *filter_lcd_backlight(GList *leds) {
    GList *out = NULL;
    for (GList *l = leds; l != NULL; l = l->next) {
        void *dev = l->data;
        const char *name = (dev && real_get_name) ? real_get_name(dev) : NULL;
        if (name && strcmp(name, "lcd-backlight") == 0) {
            g_fake_bl_dev = dev;
            if (real_g_object_ref) real_g_object_ref(dev);
            if (real_g_list_prepend) out = real_g_list_prepend(out, dev);
        } else {
            if (dev && real_g_object_unref) real_g_object_unref(dev);
        }
    }
    if (leds && real_g_list_free) real_g_list_free(leds);
    return out;
}

__attribute__((visibility("default")))
GList *g_udev_client_query_by_subsystem(void *client, const char *subsys) {
    init_syms();
    GList *res = real_query_by_subsystem ? real_query_by_subsystem(client, subsys) : NULL;
    if (!res && subsys && strcmp(subsys, "backlight") == 0 && real_query_by_subsystem) {
        GList *leds = real_query_by_subsystem(client, "leds");
        return filter_lcd_backlight(leds);
    }
    return res;
}

__attribute__((visibility("default")))
void g_udev_enumerator_add_match_subsystem(void *en, const char *subsys) {
    init_syms();
    if (subsys && strcmp(subsys, "backlight") == 0) {
        g_backlight_enum = en;
        if (real_add_match_subsystem) real_add_match_subsystem(en, "leds");
        return;
    }
    if (real_add_match_subsystem) real_add_match_subsystem(en, subsys);
}

__attribute__((visibility("default")))
GList *g_udev_enumerator_execute(void *en) {
    init_syms();
    GList *res = real_enumerator_execute ? real_enumerator_execute(en) : NULL;
    if (en && en == g_backlight_enum) {
        g_backlight_enum = NULL;
        return filter_lcd_backlight(res);
    }
    return res;
}

__attribute__((visibility("default")))
const char *g_udev_device_get_subsystem(void *dev) {
    init_syms();
    if (dev && dev == g_fake_bl_dev) return "backlight";
    return real_get_subsystem ? real_get_subsystem(dev) : NULL;
}

__attribute__((visibility("default")))
const char *g_udev_device_get_sysfs_attr(void *dev, const char *name) {
    init_syms();
    if (dev && dev == g_fake_bl_dev && name && strcmp(name, "type") == 0) {
        return "raw";
    }
    if (dev && dev == g_fake_bl_dev && name && strcmp(name, "actual_brightness") == 0) {
        name = "brightness";
    }
    return real_get_sysfs_attr ? real_get_sysfs_attr(dev, name) : NULL;
}

__attribute__((visibility("default")))
int g_udev_device_get_sysfs_attr_as_int(void *dev, const char *name) {
    init_syms();
    if (dev && dev == g_fake_bl_dev && name && strcmp(name, "actual_brightness") == 0) {
        name = "brightness";
    }
    return real_get_sysfs_attr_as_int ? real_get_sysfs_attr_as_int(dev, name) : 0;
}

__attribute__((visibility("default")))
int g_udev_device_get_sysfs_attr_as_int_uncached(void *dev, const char *name) {
    init_syms();
    if (dev && dev == g_fake_bl_dev && name && strcmp(name, "actual_brightness") == 0) {
        name = "brightness";
    }
    return real_get_sysfs_attr_as_int_uncached ? real_get_sysfs_attr_as_int_uncached(dev, name) : 0;
}
