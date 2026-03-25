# =========================================================
# QNX PROJECT BUILD LOGIC FOR WEB BRIDGE
# =========================================================

# 1. Single source containing main()
MAIN_SRCS = src/web_bridge.cpp

# Default platform variables are inherited from the parent make call
PLATFORM ?= aarch64le
BUILD_PROFILE ?= debug
CONFIG_NAME ?= $(PLATFORM)-$(BUILD_PROFILE)
OUTPUT_DIR = build/$(CONFIG_NAME)

# Compiler definitions
CC = qcc -Vgcc_nto$(PLATFORM)
CXX = q++ -Vgcc_nto$(PLATFORM)_cxx
LD = $(CXX)

# Include paths
INCLUDES += -I../common/include

# Compiler flags for build profiles
CCFLAGS_release += -O2
CCFLAGS_debug += -g -O0 -fno-builtin
CCFLAGS_coverage += -g -O0 -ftest-coverage -fprofile-arcs
LDFLAGS_coverage += -ftest-coverage -fprofile-arcs
CCFLAGS_profile += -g -O0 -finstrument-functions
LIBS_profile += -lprofilingS

# Generic flags
CCFLAGS_all += -Wall -fmessage-length=0
CCFLAGS_all += $(CCFLAGS_$(BUILD_PROFILE))
LDFLAGS_all += $(LDFLAGS_$(BUILD_PROFILE))
LIBS_all += $(LIBS_$(BUILD_PROFILE))
DEPS = -Wp,-MMD,$(@:%.o=%.d),-MT,$@

# Target binary name (e.g. build/aarch64le-debug/web_bridge.aarch64le.bin)
TARGET = $(OUTPUT_DIR)/web_bridge.$(PLATFORM).bin

# Gather common sources from ../common/src
rwildcard = $(wildcard $(addprefix $1/*.,$2)) $(foreach d,$(wildcard $1/*),$(call rwildcard,$d,$2))
ALL_SRCS = $(MAIN_SRCS) $(call rwildcard, ../common/src, c cpp)
COMMON_SRCS = $(filter-out $(MAIN_SRCS), $(ALL_SRCS))
COMMON_OBJS = $(addprefix $(OUTPUT_DIR)/,$(addsuffix .o, $(subst ../,shared_out/,$(basename $(COMMON_SRCS)))))

all: $(TARGET)

$(TARGET): $(OUTPUT_DIR)/src/web_bridge.o $(COMMON_OBJS)
	@mkdir -p $(dir $@)
	$(LD) -o $@ $(LDFLAGS_all) $(LDFLAGS) $^ $(LIBS_all) $(LIBS) -lsocket

# Build common objects
$(OUTPUT_DIR)/shared_out/%.o: ../%.c
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPS) -o $@ $(INCLUDES) $(CCFLAGS_all) $(CCFLAGS) $<

# Compile the bridge source
$(OUTPUT_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) -c $(DEPS) -o $@ $(INCLUDES) $(CCFLAGS_all) $(CCFLAGS) $<

clean:
	rm -fr $(OUTPUT_DIR)

-include $(TARGET:%.bin=%.d)