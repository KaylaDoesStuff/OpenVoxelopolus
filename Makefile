CC = gcc
CFLAGS = -Wall
LDLIBS = -lglut -lGLU -lGL -lm

cube: cube.bin
	printf '#!/bin/sh\nexec env __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia "$$(dirname "$$0")/cube.bin" "$$@"\n' > $@
	chmod +x $@

cube.bin: cube.c chunk.c render.c noise.c shader.c blocks.h chunk.h render.h noise.h shader.h
	$(CC) $(CFLAGS) -o $@ cube.c chunk.c render.c noise.c shader.c $(LDLIBS)

clean:
	rm -f cube cube.bin
