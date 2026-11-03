CC = gcc
CFLAGS = -Wall -Wextra -O2 -g $(shell pkg-config --cflags libpipewire-0.3 lilv-0 json-c) -fPIC -Isrc
LDFLAGS = $(shell pkg-config --libs libpipewire-0.3 lilv-0 json-c) -ldl -lm

TARGET = easyeffects_cli
SRC_DIR = src
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/pipewire_manager.c \
       $(SRC_DIR)/lv2_loader.c \
       $(SRC_DIR)/preset_manager.c

OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

install: $(TARGET)
	mkdir -p ~/.config/easyeffects-cli/presets
	cp $(TARGET) /usr/local/bin/

test: $(TARGET)
	./$(TARGET) list-plugins

.PHONY: all clean install test