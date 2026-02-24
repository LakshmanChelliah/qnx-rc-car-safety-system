# =========================================================
# QNX PROJECT BUILD LOGIC (Multiple Binaries)
# =========================================================

# 1. List every file in your project that has a main() function
MAIN_SRCS = src/drive.cpp

PLATFORM ?= aarch64le
BUILD_PROFILE ?= debug
CONFIG_NAME ?= $(PLATFORM)-$(BUILD_PROFILE)
OUTPUT_DIR = build/$(CONFIG_NAME)

# 2. Automatically generate the target binary names (e.g., build/aarch64le-debug/example1)
TARGETS = $(basename $(patsubst src/%, $(OUTPUT_DIR)/%, $(MAIN_SRCS)))

# Compiler definitions
CC = qcc -Vgcc_nto$(PLATFORM)
CXX = q++ -Vgcc_nto$(PLATFORM)_cxx
LD = $(CXX)

# User defined include/preprocessor flags and libraries
INCLUDES += -I../common/include

# Compiler flags for build profiles
CCFLAGS_release += -O2
CCFLAGS_debug += -g -O0 -fno-builtin
CCFLAGS_coverage += -g -O0 -ftest-coverage -fprofile-arcs
LDFLAGS_coverage += -ftest-coverage -fprofile-arcs
CCFLAGS_profile += -g -O0 -finstrument-functions
LIBS_profile += -lprofilingS

# Generic compiler flags (which include build type flags)
CCFLAGS_all += -Wall -fmessage-length=0
CCFLAGS_all += $(CCFLAGS_$(BUILD_PROFILE))
LDFLAGS_all += $(LDFLAGS_$(BUILD_PROFILE))
LIBS_all += $(LIBS_$(BUILD_PROFILE))
DEPS = -Wp,-MMD,$(@:%.o=%.d),-MT,$@

# Macro to expand files recursively
rwildcard = $(wildcard $(addprefix $1/*.,$2)) $(foreach d,$(wildcard $1/*),$(call rwildcard,$d,$2))

# 3. Separate Shared/Common sources from Main sources
ALL_SRCS = $(call rwildcard, src, c cpp) $(call rwildcard, ../common/src, c cpp)
COMMON_SRCS = $(filter-out $(MAIN_SRCS), $(ALL_SRCS))

# 4. Generate Object lists
COMMON_OBJS = $(addprefix $(OUTPUT_DIR)/,$(addsuffix .o, $(subst ../,shared_out/,$(basename $(COMMON_SRCS)))))
ALL_OBJS = $(addprefix $(OUTPUT_DIR)/,$(addsuffix .o, $(subst ../,shared_out/,$(basename $(ALL_SRCS)))))

# Rules section for default compilation and linking
all: $(TARGETS)

# 5. Static Pattern Rule for Linking multiple binaries
$(TARGETS): $(OUTPUT_DIR)/%: $(OUTPUT_DIR)/src/%.o $(COMMON_OBJS)
	$(LD) -o $@ $(LDFLAGS_all) $(LDFLAGS) $^ $(LIBS_all) $(LIBS)

# Custom rule for compiling the shared common files
$(OUTPUT_DIR)/shared_out/%.o: ../%.c
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPS) -o $@ $(INCLUDES) $(CCFLAGS_all) $(CCFLAGS) $<

# Standard Compiling rules for local src files
$(OUTPUT_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPS) -o $@ $(INCLUDES) $(CCFLAGS_all) $(CCFLAGS) $<
$(OUTPUT_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) -c $(DEPS) -o $@ $(INCLUDES) $(CCFLAGS_all) $(CCFLAGS) $<

clean:
	rm -fr $(OUTPUT_DIR)

# Inclusion of dependencies (object files to source and includes)
-include $(ALL_OBJS:%.o=%.d)