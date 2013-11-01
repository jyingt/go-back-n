all: gbn

gbn: gbn.c
	gcc -o gbn gbn.c 
clean:
	rm -rf gbn OutputFile
