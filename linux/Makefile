# =========================================================================
#                     vscpl2drv-automation build file
# =========================================================================

# Package version
MAJOR_VERSION=1
MINOR_VERSION=1
RELEASE_VERSION=0
BUILD_VERSION=0
PACKAGE_VERSION=1.1.0

# STATIC = yes if a static build is requested.
STATIC=no
VSCP_PROJ_BASE_DIR=/var/lib/vscp/
DESTDIR=/var/lib/vscp/

prefix = /usr/local
exec_prefix = ${prefix}
top_srcdir = ..
top_builddir = ..

CC = gcc
CXX = g++

WARNINGS_HARD = -Werror -Wall -Wextra -pedantic -Wcast-align -Wcast-qual \
  -Wctor-dtor-privacy -Wdisabled-optimization -Wformat=2 -Winit-self \
  -Wlogical-op -Wmissing-include-dirs -Wnoexcept -Wold-style-cast \
  -Woverloaded-virtual -Wredundant-decls -Wshadow -Wsign-promo \
  -Wstrict-null-sentinel -Wstrict-overflow=5 -Wundef -Wno-unused \
  -Wno-variadic-macros -Wno-parentheses -fdiagnostics-show-option
WARNINGS_MEDIUM = -Wformat-security -Wformat=2 -Wmissing-include-dirs \
  -Wno-parentheses -Wformat=2 -Wlogical-op
WARNINGS = -Wall

VERSION_DEFS = -DMAJOR_VERSION=$(MAJOR_VERSION) -DMINOR_VERSION=$(MINOR_VERSION) \
	-DRELEASE_VERSION=$(RELEASE_VERSION) -DBUILD_VERSION=$(BUILD_VERSION)
CFLAGS = -std=c99  -g -O0 -DOPENSSL_API_1_1 ${WARNINGS} -fPIC -DCBC -I.. -I../common \
	-I../../vscp/src/common \
	-I../../vscp/src/vscp/common \
	-I../../vscp/src/common/third_party/ \
	$(VERSION_DEFS)
CXXFLAGS = -std=c++11 ${WARNINGS} -D__LINUX__ -fPIC -D_REENTRANT  -g -O0
CPPFLAGS = -D__LINUX__ -fPIC  ${WARNINGS} -fno-var-tracking-assignments \
    -I.. -I@include@ -I../common \
	-I../../vscp/src/common \
	-I../../vscp/src/vscp/common \
	-I../../vscp/src/common/third_party/ \
	-I../../vscp/src/common/third_party/nlohmann \
	$(VERSION_DEFS)

LDFLAGS = 
LIBS = -lexpat -lssl -lwrap -lz -lrt -lm -lcrypto -lpthread 
EXTRALIBS = 

DLFLAGS = -g -shared -fPIC
STRIP = strip
INSTALL_PROGRAM = ${INSTALL}
INSTALL_DATA = ${INSTALL} -m 644
INSTALL_DIR = /usr/bin/install -c -d
INSTALL = /usr/bin/install -c
srcdir = .
top_srcdir = .
top_builddir =
bindir = ${exec_prefix}/bin
libdir = ${exec_prefix}/lib
datadir = ${prefix}/share
includedir = ${prefix}/include
DLLPREFIX = lib

#LIB_PLUS_VER = "vscpl2drv-automation.so.$(MAJOR_VERSION).$(MINOR_VERSION).$(RELEASE_VERSION)"
LIB_PLUS_VER = "vscpl2drv-automation.so"
LIB_SONAME = "vscpl2drv-automation.so.$(MAJOR_VERSION)"

### Variables: ###

AUTOMATION_OBJECTS = vscpl2drv-automation.o\
	automation.o\
	vscphelper.o\
	vscpdatetime.o\
	hlo.o\
	guid.o\
	crc.o\
	crc8.o\
	vscp_aes.o\
	vscpmd5.o\
	fastpbkdf2.o\
	vscpbase64.o

### Targets: ###

all: $(LIB_PLUS_VER) test

test:  test.o $(AUTOMATION_OBJECTS)
	$(CXX) -o $@ test.o $(AUTOMATION_OBJECTS) $(LDFLAGS) -lexpat -lssl -lwrap -lz -lrt -lm -lcrypto -lpthread  $(EXTRALIBS)

$(LIB_PLUS_VER): $(AUTOMATION_OBJECTS)
	$(CXX) -Wl,-soname,$(LIB_SONAME) -o $@ $(AUTOMATION_OBJECTS) $(DLFLAGS) -lexpat -lssl -lwrap -lz -lrt -lm -lcrypto -lpthread  $(EXTRALIBS)
	ar rcs libvscpl2drv-automation.a $(AUTOMATION_OBJECTS)

automation.o: ../common/automation.cpp ../common/automation.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c ../common/automation.cpp -o $@

vscphelperlib.o: ../vscp/src/vscp/common/vscphelperlib.cpp ../vscp/src/vscp/common/vscphelperlib.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c ../vscp/src/vscp/common/vscphelperlib.cpp -o $@

vscpdatetime.o: ../vscp/src/vscp/common/vscpdatetime.cpp ../vscp/src/vscp/common/vscpdatetime.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c ../vscp/src/vscp/common/vscpdatetime.cpp -o $@

vscphelper.o: ../vscp/src/vscp/common/vscphelper.cpp ../vscp/src/vscp/common/vscphelper.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c ../vscp/src/vscp/common/vscphelper.cpp -o $@

hlo.o: ../vscp/src/vscp/common/hlo.cpp ../vscp/src/vscp/common/hlo.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c ../vscp/src/vscp/common/hlo.cpp -o $@

guid.o: ../vscp/src/vscp/common/guid.cpp ../vscp/src/vscp/common/guid.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c ../vscp/src/vscp/common/guid.cpp -o $@

vscp_aes.o: ../vscp/src/common/vscp_aes.c ../vscp/src/common/vscp_aes.h
	$(CC) $(CFLAGS) -c ../vscp/src/common/vscp_aes.c -o $@

crc.o: ../vscp/src/common/crc.c ../vscp/src/common/crc.h
	$(CC) $(CFLAGS) -c ../vscp/src/common/crc.c -o $@

crc8.o: ../vscp/src/common/crc8.c ../vscp/src/common/crc8.h
	$(CC) $(CFLAGS) -c ../vscp/src/common/crc8.c -o $@

vscpbase64.o: ../vscp/src/common/vscpbase64.c ../vscp/src/common/vscpbase64.h
	$(CC) $(CFLAGS) -c ../vscp/src/common/vscpbase64.c -o $@

fastpbkdf2.o: ../vscp/src/common/fastpbkdf2.c ../vscp/src/common/fastpbkdf2.h
	$(CC) $(CFLAGS) -c ../vscp/src/common/fastpbkdf2.c -o $@

vscpmd5.o: ../vscp/src/common/vscpmd5.c ../vscp/src/common/vscpmd5.h
	$(CC) $(CFLAGS) -c ../vscp/src/common/vscpmd5.c -o $@

install: all	
	$(INSTALL_DIR) $(DESTDIR)/drivers/level2/
	$(STRIP) $(LIB_PLUS_VER)
	$(INSTALL_PROGRAM) $(LIB_PLUS_VER) $(DESTDIR)/drivers/level2/
#	ln -fs $(DESTDIR)/drivers/level2//$(LIB_PLUS_VER) $(DESTDIR)/drivers/level2//vscpl2drv-automation.so
#	ln -fs $(DESTDIR)/drivers/level2//$(LIB_PLUS_VER) $(DESTDIR)/drivers/level2//vscpl2drv-automation.so.$(MAJOR_VERSION)
#	ln -fs $(DESTDIR)/drivers/level2//$(LIB_PLUS_VER) $(DESTDIR)/drivers/level2//vscpl2drv-automation.so.$(MAJOR_VERSION).$(MINOR_VERSION)
#	$(INSTALL_PROGRAM) libvscpl2drv-automation.a $(DESTDIR)/drivers/level2/

uninstall:
	rm -f  $(DESTDIR)$(libdir)/$(LIB_PLUS_VER)
#	rm -f  $(DESTDIR)$(libdir)/vscpl2drv-automation.so
#	rm -f  $(DESTDIR)$(libdir)/vscpl2drv-automation.so.$(MAJOR_VERSION)
#	rm -f  $(DESTDIR)$(libdir)/vscpl2drv-automation.so.$(MAJOR_VERSION).$(MINOR_VERSION)
#	rm -f  $(DESTDIR)$(libdir)/vscpl2drv-automation.a

install-strip: install

man: ../man/vscpl2drv-automation.1.md
	pandoc -s -t man ../man/vscpl2drv-automation.1.md -o ../man/vscpl2drv-automation.1

deb:    all
	echo "\n" | ./builddist.sh
	@mkdir -p  ../dist
	@mv *.deb ../dist

clean:
	rm -rf ./.deps ./.pch
	rm -f ./*.o
	rm -f ../common/*.o
	rm -f ../vscp/src/common/*.o
	rm -f ../vscp/src/vscp/common/*.o
	rm -f $(LIB_PLUS_VER)
	rm -f *.a
	rm -f test
	rm -f *.deb
	rm -f *.gz

distclean: clean
	rm -f config.cache config.log config.status bk-deps bk-make-pch shared-ld-sh Makefile

$(ALWAYS_BUILD):  .FORCE

.FORCE:

# Include dependency info, if present:
-include .deps/*.d

.PHONY: all install uninstall clean distclean data .FORCE
