CFLAGS = -g -O3 -Wall -Wextra -Wno-sign-compare
LDLIBS = -lcairo -lpng -lz -lm
TARGET = sgd2png

$(TARGET): sgd.c
	$(CC) -o $@ $(CFLAGS) $< $(LDFLAGS) $(LDLIBS)

.PHONY: clean

clean:
	rm -f $(TARGET)
