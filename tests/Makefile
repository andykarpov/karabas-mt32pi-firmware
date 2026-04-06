#
# tests/Makefile
#
# Builds and runs host-side unit tests for mt32-pi.
# Uses doctest (header-only) -- no dependencies beyond g++.
#
# Usage:
#   make            # build
#   make run        # build + run
#   make clean      # clean artifacts
#

CXX       = g++
CC        = gcc
CXXFLAGS  = -std=c++17 -Wall -Wextra -Wno-unused-parameter -g -O0
CXXFLAGS += -I. -Istubs -Idoctest -I../include -I../external/inih -DUNIT_TEST
CFLAGS    = -std=c99 -I../external/inih

# All compiled objects go here, mirroring the source tree.
# This prevents host-native .o files from polluting ../src/ and
# conflicting with the AArch64 cross-build.
OBJDIR    = build

# C++ source files -- add new test_*.cpp here
SRCS      = test_main.cpp \
            test_midirouter.cpp \
            ../src/midirouter.cpp \
            test_audiomixer.cpp \
            ../src/audiomixer.cpp \
            test_audioeffects.cpp \
            ../src/audioeffects.cpp \
            test_midiparser.cpp \
            ../src/midiparser.cpp \
            test_config_parse.cpp \
            test_midirecorder.cpp \
            test_fluidsequencer.cpp \
            ../src/fluidsequencer.cpp \
            ../src/midirecorder.cpp \
            test_playlist.cpp \
            ../src/playlist.cpp \
            test_osc.cpp \
            ../src/net/oscdaemon.cpp \
            test_applemidi.cpp \
            ../src/net/applemidi.cpp \
            stubs/fluidsynth_stub.cpp \
            stubs/fatfs_stub.cpp

# C source files (inih)
CFILES    = ../external/inih/ini.c

# Map source paths → $(OBJDIR)/<mirrored_path>.o  (stripping leading ../)
OBJS   = $(addprefix $(OBJDIR)/,$(subst ../,,$(SRCS:.cpp=.o)))
COBJS  = $(addprefix $(OBJDIR)/,$(subst ../,,$(CFILES:.c=.o)))
TARGET = run_tests

.PHONY: all run clean

all: $(TARGET)
	@echo "Build OK.  Run:  ./$(TARGET)"

run: $(TARGET)
	./$(TARGET)

$(TARGET): $(OBJS) $(COBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Local sources: test_*.cpp, stubs/*.cpp
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Sources under ../  (../src/*.cpp, ../src/net/*.cpp, etc.)
$(OBJDIR)/%.o: ../%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# C sources under ../  (../external/inih/ini.c)
$(OBJDIR)/%.o: ../%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(TARGET)
