all: DFC/dfc dfs

DFC/dfc: DFC/dfc.c
	gcc DFC/dfc.c -o DFC/dfc -Wall -lcrypto

dfs: dfs.c
	gcc dfs.c -o dfs -Wall -pthread

clean:
	rm DFC/dfc dfs