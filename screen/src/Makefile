all: brightness

brightness: dpAux.c brightness.c tables.c main.c
	gcc -g -D_GNU_SOURCE -o $@ $^ -lm -ludev

clean:
	rm -f brightness
