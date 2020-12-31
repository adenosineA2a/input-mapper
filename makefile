CFLAGS=-Wall -Wextra `pkg-config --cflags --libs libevdev luajit`

all:
	gcc $(CFLAGS) -o input-mapper input-mapper.c
