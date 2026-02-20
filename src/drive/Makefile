# =========================================================
# 1. MULTI-ARCHITECTURE WRAPPER
# =========================================================
# Define the platforms we want to build every time
PLATFORMS = x86_64 aarch64le

# If CURRENT_PLATFORM is not set, we are in the "parent" make run.
# We loop through PLATFORMS and call this Makefile again for each one.
ifndef CURRENT_PLATFORM

all:
	@for p in $(PLATFORMS); do \
		echo "=== Building Drive Node for $$p ==="; \
		$(MAKE) --no-print-directory CURRENT_PLATFORM=$$p PLATFORM=$$p all; \
	done

clean:
	@for p in $(PLATFORMS); do \
		$(MAKE) --no-print-directory CURRENT_PLATFORM=$$p PLATFORM=$$p clean; \
	done

rebuild: clean all

else
# =========================================================
# 2. QNX PROJECT BUILD LOGIC (The "Child" Run)
# =========================================================

ARTIFACT = drive_binary
PLATFORM = $(CURRENT_PLATFORM)
BUILD_PROFILE ?= debug
CONFIG_NAME ?= $(PLATFORM)-$(BUILD_PROFILE)
OUTPUT_DIR = build/$(CONFIG_NAME)
TARGET = $(OUTPUT_DIR)/$(ARTIFACT)

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

# Source list (local AND common shared files)
SRCS = $(call rwildcard, src, c cpp) $(call rwildcard, ../common/src, c cpp)

# Object files list
# VERY IMPORTANT: Replaces '../' with 'shared_out/' so they stay trapped inside the architecture output folder
OBJS = $(addprefix $(OUTPUT_DIR)/,$(addsuffix .o, $(subst ../,shared_out/,$(basename $(SRCS)))))

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

# Linking rule
$(TARGET): $(OBJS)
	$(LD) -o $(TARGET) $(LDFLAGS_all) $(LDFLAGS) $(OBJS) $(LIBS_all) $(LIBS)

# Rules section for default compilation and linking
all: $(TARGET)

clean:
	rm -fr $(OUTPUT_DIR)

# Inclusion of dependencies (object files to source and includes)
-include $(OBJS:%.o=%.d)

endif