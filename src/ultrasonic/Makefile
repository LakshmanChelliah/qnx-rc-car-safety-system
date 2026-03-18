# =========================================================
# QNX PROJECT BUILD LOGIC (Multiple Binaries)
# =========================================================

MAIN_SRCS = src/ultrasonic.cpp

PLATFORM ?= aarch64le
BUILD_PROFILE ?= debug
CONFIG_NAME ?= $(PLATFORM)-$(BUILD_PROFILE)
OUTPUT_DIR = build/$(CONFIG_NAME)

TARGETS = $(addsuffix .$(PLATFORM).bin, $(basename $(patsubst src/%, $(OUTPUT_DIR)/%, $(MAIN_SRCS))))

CC = qcc -Vgcc_nto$(PLATFORM)
CXX = q++ -Vgcc_nto$(PLATFORM)_cxx
LD = $(CXX)

INCLUDES += -I../common/include -Iinclude

CCFLAGS_release += -O2
CCFLAGS_debug += -g -O0 -fno-builtin
CCFLAGS_coverage += -g -O0 -ftest-coverage -fprofile-arcs
LDFLAGS_coverage += -ftest-coverage -fprofile-arcs
CCFLAGS_profile += -g -O0 -finstrument-functions
LIBS_profile += -lprofilingS

CCFLAGS_all += -Wall -fmessage-length=0
CCFLAGS_all += $(CCFLAGS_$(BUILD_PROFILE))
LDFLAGS_all += $(LDFLAGS_$(BUILD_PROFILE))
LIBS_all += $(LIBS_$(BUILD_PROFILE))
DEPS = -Wp,-MMD,$(@:%.o=%.d),-MT,$@

rwildcard = $(wildcard $(addprefix $1/*.,$2)) $(foreach d,$(wildcard $1/*),$(call rwildcard,$d,$2))

ALL_SRCS = $(call rwildcard, src, c cpp) $(call rwildcard, ../common/src, c cpp)
COMMON_SRCS = $(filter-out $(MAIN_SRCS), $(ALL_SRCS))

COMMON_OBJS = $(addprefix $(OUTPUT_DIR)/,$(addsuffix .o, $(subst ../,shared_out/,$(basename $(COMMON_SRCS)))))
ALL_OBJS = $(addprefix $(OUTPUT_DIR)/,$(addsuffix .o, $(subst ../,shared_out/,$(basename $(ALL_SRCS)))))

all: $(TARGETS)

$(TARGETS): $(OUTPUT_DIR)/%.$(PLATFORM).bin: $(OUTPUT_DIR)/src/%.o $(COMMON_OBJS)
	$(LD) -o $@ $(LDFLAGS_all) $(LDFLAGS) $^ $(LIBS_all) $(LIBS)

$(OUTPUT_DIR)/shared_out/%.o: ../%.c
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPS) -o $@ $(INCLUDES) $(CCFLAGS_all) $(CCFLAGS) $<

$(OUTPUT_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPS) -o $@ $(INCLUDES) $(CCFLAGS_all) $(CCFLAGS) $<
$(OUTPUT_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) -c $(DEPS) -o $@ $(INCLUDES) $(CCFLAGS_all) $(CCFLAGS) $<

clean:
	rm -fr $(OUTPUT_DIR)

-include $(ALL_OBJS:%.o=%.d)

