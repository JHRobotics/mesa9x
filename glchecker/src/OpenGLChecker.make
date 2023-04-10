CPATH=

all:
	sudo g++ -w -m32 -Ofast -fomit-frame-pointer $(CPATH)/main.cpp $(CPATH)/parser.cpp $(CPATH)/benchmark.cpp -o ./OpenGLChecker.out -lGL -lbass -lX11 -lXxf86vm -lXrandr -lCEngine