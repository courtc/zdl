CFLAGS := -Wall -fPIC -g
CXXFLAGS := $(CFLAGS)
SO_LDFLAGS := -shared -lGL -lX11
T_LDFLAGS := -L. -lzdl
objs := zdl_xlib.o
tgt := libzdl.so
tst := zdltest

all: $(tgt)

$(tst): $(objs) test.o | $(tgt)
	$(CXX) -o $@ $^ $(T_LDFLAGS)
	#LD_LIBRARY_PATH=. ./$@

$(tgt): $(objs)
	$(CXX) -o $@ $^ $(SO_LDFLAGS)

clean:
	$(RM) $(tgt) $(objs) test.o
