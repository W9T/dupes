CXX     = g++
COPTS   = -Wall -Wextra -fmax-errors=3 -O3 -march=native -std=c++17
LOPTS   = -lstdc++ -lstdc++fs -lm

PROJ    = dupes

BINDIR	= bin
ifeq ($(OS), Windows_NT)
	BINEXT	= .exe
	INSTDIR	= \bin\utils\bin
else
	BINEXT	= 
	INSTDIR	= /usr/local/bin
endif

### #################################################################
all: build

run:
	bin/$(PROJ).bin

build: $(BINDIR)
	$(CXX) $(COPTS) $(PROJ).cc $(LOPTS) -o $(BINDIR)/$(PROJ)$(BINEXT)
clean:
	rm -v $(BINDIR)/$(PROJ).*
install: build $(INSTDIR)
	cp -v $(BINDIR)/$(PROJ)$(BINEXT) $(INSTDIR)
