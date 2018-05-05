TARGET = jpegDecoder.out
OBJS = main.o \
	   jpegDecoder.o jpegStream.o jpegHeader.o jpegMCU.o \
	   bmpStream.o

STDC++ = -std=c++11

$(TARGET) : $(OBJS)
	g++ $(STDC++) $^ -o $@

main.o: main.cpp
	g++ $(STDC++) -c $^
bmpStream.o: bmpStream.cpp bmpStream.h
	g++ $(STDC++) -c $^

%.o: %.cpp jpegDecoder.h
	g++ $(STDC++) -c $^

clean:
	rm *.o *.h.gch *.out
