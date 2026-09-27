CC = gcc
CFLAGS = -Wall -O2 $(shell pkg-config --cflags libpipewire-0.3 lilv-0 json-c) -fPIC
LDFLAGS = $(shell pkg-config --libs libpipewire-0.3 lilv-0 json-c) -ldl

TARGET = easyeffects_cli_full
SRC = easyeffects_cli_full.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

install:
	mkdir -p ~/.config/easyeffects-cli/presets
	cp $(TARGET) /usr/local/bin/

test:
	./$(TARGET) list-plugins
