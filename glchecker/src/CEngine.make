CPATH=

all:
	sudo g++ -w -m32 -shared -fPIC -Ofast -fomit-frame-pointer $(CPATH)/CEngine.cpp $(CPATH)/CWindow.cpp $(CPATH)/COpenGL.cpp $(CPATH)/CSound.cpp $(CPATH)/CSocket.cpp $(CPATH)/CGUI.cpp $(CPATH)/CVector.cpp -o ./libCEngine.so -lGL -lbass -lX11 -lXxf86vm -lXrandr
