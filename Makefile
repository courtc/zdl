CFLAGS := -Wall -fPIC -g
CXXFLAGS := $(CFLAGS)
LDFLAGS := -lGL -lX11
SO_LDFLAGS := -shared $(LDFLAGS)
T_LDFLAGS := -L. -lzdl $(LDFLAGS)
objs := zdl_xlib.o
tgt := libzdl.so
tst := zdltest

all: $(tgt)

$(tst): test.o | $(tgt)
	$(CXX) -o $@ $^ $(T_LDFLAGS)

test: $(tst)
	LD_LIBRARY_PATH=. ./$(tst)

$(tgt): $(objs)
	$(CC) -o $@ $^ $(SO_LDFLAGS)

clean:
	$(RM) $(tgt) $(tst) $(objs) test.o

.PHONY: test clean
