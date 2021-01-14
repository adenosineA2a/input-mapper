#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

char* mouse_path = "/dev/input/by-id/usb-Logitech_G502_HERO_Gaming_Mouse_0C7F38783538-event-mouse";
char* kbd_path = "/dev/input/by-id/usb-SINO_WEALTH_USB_KEYBOARD-event-kbd";

struct libevdev* setup_device(char* path) {
    struct libevdev* dev = NULL;
    int err, fd;
    fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    err = libevdev_new_from_fd(fd, &dev);
    if (err != 0) return NULL;
    err = libevdev_grab(dev, LIBEVDEV_GRAB);
    if (err != 0) return NULL;
    return dev;
}

lua_State* init_lua(char* filename) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    if (luaL_dofile(L, filename) != 0) return NULL;
    return L;
}


int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("%s\n", "No devices specified!");
        exit(1);
    }
    int num_devices = argc - 1;
    struct libevdev** devices = malloc(num_devices * sizeof(struct libevdev*));
    struct libevdev_uinput *uidev = NULL;
    
    for (int i = 0; i < num_devices; i++) {
        devices[i] = setup_device(argv[i+1]);
        if (devices[i] == NULL) {
            printf("%s\n", "Problem setting up device!");
            exit(1);
        }
    }

    // TODO: This is incredibly fragile and won't work 50% of the time
    for (int i = 1; i <= 484; i++) {
        if (libevdev_enable_event_code(devices[0], EV_KEY, i, NULL) != 0) {
            printf("%s\n", "Enabling event codes failed!");
            exit(1);
        }
    }

    // TODO: This is incredibly fragile and won't work 50% of the time
    if (libevdev_uinput_create_from_device(devices[0], LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev) != 0) {
        printf("%s\n", "Virtual device creation failed!");
        exit(1);
    }

    // TODO: getenv("HOME", xdg, etc
    lua_State *L = init_lua("/home/XXXXXXX/.config/input-mapper/input-mapper.lua");
    if (L == NULL) {
        printf("%s\n", "Unable to run lua config!");
        exit(1);
    }

    auto void process_event(struct input_event *event) {
        if (event->type != EV_KEY) {
            libevdev_uinput_write_event(uidev, event->type, event->code, event->value);
            return;
        }
        else if (event->value == 2) return;
        
        int code, value;
        lua_getglobal(L, "Main");
        lua_pushnumber(L, event->code);
        lua_pushnumber(L, event->value);
        int err = lua_pcall(L, 2, 1, 0);
        if (err != 0) {
            printf("%s\n", lua_tostring(L, -1));
        }

        for (int i = 1;; i++) {
            lua_rawgeti(L, -1, i);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 2);
                break;
            }

            lua_rawgeti(L, -1, 1);
            lua_rawgeti(L, -2, 2);
            code = lua_tointeger(L, -2);
            value = lua_tointeger(L, -1);
            libevdev_uinput_write_event(uidev, EV_KEY, code, value);
            lua_pop(L, 3);
        }
    }

    auto void read_device(struct libevdev *dev) {
        struct input_event event;
        int rc;
        for (;;) {
            rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING,
                &event);
            while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &event);
            }
            if (rc == -EAGAIN) break;
            if (rc != LIBEVDEV_READ_STATUS_SUCCESS) break;
            process_event(&event);
            if (!libevdev_has_event_pending(dev)) break;
        }
    }

    for (;;) {
        for (int i = 0; i < num_devices; i++) {
            if (libevdev_has_event_pending(devices[i])) {
                read_device(devices[i]);
            }
        }
        usleep(5000);
    }

    return 0;
}
