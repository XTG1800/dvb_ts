CC = gcc
RM = rm -rf 
TARGET = main
        
.PHONY: all clean
        
all: 
	@#$(CC) -E -o main.i main.c
	@#$(CC) -S -o main.s main.i
	@#$(CC) -c -o main.o main.s 
	@#$(CC) -o $(TARGET) main.o
	@$(CC) -o $(TARGET) main.c
        
clean:
	@$(RM) main.i main.s main.o $(TARGET)
