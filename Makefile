CC = gcc
CFLAGS = -Wall -Iinclude -lm -std=c2x -D_XOPEN_SOURCE=700
GL_LIBS = -lglut -lGLU -lGL
SHM_LIBS = -lrt -lpthread

all: trek_server trek_client trek_3dview

trek_server: src/trek_server.c
	$(CC) src/trek_server.c -o trek_server $(CFLAGS) $(SHM_LIBS)

trek_client: src/trek_client.c
	$(CC) src/trek_client.c -o trek_client $(CFLAGS) $(SHM_LIBS)

trek_3dview: src/trek_3dview.c
	$(CC) src/trek_3dview.c -o trek_3dview $(CFLAGS) $(GL_LIBS) $(SHM_LIBS)

clean:
	rm -f trek_server trek_client trek_3dview