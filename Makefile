BACKEND ?= xlib
LIBS := -lGL
CFLAGS := -Wall -fPIC -g

ifeq ($(BACKEND),xlib)
LIBS += -lX11
endif
ifeq ($(BACKEND),drm)
LIBS += -lEGL
zdl_drm.o: CFLAGS += -I/usr/include/libdrm
endif

CXXFLAGS := $(CFLAGS)
SO_LDFLAGS := -shared $(LIBS)
T_LDFLAGS := -L. -lzdl
objs := zdl_$(BACKEND).o
tgt := libzdl.so
tst := zdltest

all: $(tgt)

$(tst): $(objs) test.o | $(tgt)
	$(CXX) -o $@ $^ $(T_LDFLAGS)
	#LD_LIBRARY_PATH=. ./$@

$(tgt): $(objs)
	$(CC) -o $@ $^ $(SO_LDFLAGS)

clean:
	$(RM) $(tgt) $(objs) test.o
