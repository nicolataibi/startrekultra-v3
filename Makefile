CC = gcc
CFLAGS = -Wall -Iinclude -lm -std=c23 -D_XOPEN_SOURCE=700
GL_LIBS = -lglut -lGLU -lGL
SHM_LIBS = -lrt -lpthread

all: startrekultra ultra_view trek_server trek_client trek_3dview



startrekultra: src/main.c

	$(CC) src/main.c -o startrekultra $(CFLAGS) $(SHM_LIBS)



ultra_view: src/ultra_view.c

	$(CC) src/ultra_view.c -o ultra_view $(CFLAGS) $(GL_LIBS) $(SHM_LIBS)



trek_server: src/trek_server.c

	$(CC) src/trek_server.c -o trek_server $(CFLAGS) $(SHM_LIBS)



trek_client: src/trek_client.c



	$(CC) src/trek_client.c -o trek_client $(CFLAGS) $(SHM_LIBS)







trek_3dview: src/trek_3dview.c



	$(CC) src/trek_3dview.c -o trek_3dview $(CFLAGS) $(GL_LIBS) $(SHM_LIBS)







clean:



	rm -f startrekultra ultra_view trek_server trek_client trek_3dview




