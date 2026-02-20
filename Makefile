# =========================================================
# Top-Level Master Makefile
# =========================================================

# Define all target architectures here
PLATFORMS = x86_64 aarch64le

# List your project directories here (ADD NEW PROJECTS HERE)
SUBDIRS = src/drive

.PHONY: all clean $(SUBDIRS)

# Master rule to iterate through platforms, then through subdirectories
all:
	@for p in $(PLATFORMS); do \
		echo "================================================="; \
		echo " Building Workspace for Architecture: $$p"; \
		echo "================================================="; \
		for dir in $(SUBDIRS); do \
			$(MAKE) --no-print-directory -C $$dir CURRENT_PLATFORM=$$p PLATFORM=$$p all; \
		done; \
	done

# Clean everything across all platforms and subdirectories
clean:
	@for p in $(PLATFORMS); do \
		for dir in $(SUBDIRS); do \
			$(MAKE) --no-print-directory -C $$dir CURRENT_PLATFORM=$$p PLATFORM=$$p clean; \
		done; \
	done