# Target architectures
PLATFORMS = x86_64 aarch64le

# List your project directories here (ADD NEW PROJECTS HERE)
SUBDIRS = drive examples launcher joystick

.PHONY: all clean $(SUBDIRS)

all:
	@for p in $(PLATFORMS); do \
		for dir in $(SUBDIRS); do \
			$(MAKE) --no-print-directory -C $$dir CURRENT_PLATFORM=$$p PLATFORM=$$p all; \
		done; \
	done

clean:
	@for p in $(PLATFORMS); do \
		for dir in $(SUBDIRS); do \
			$(MAKE) --no-print-directory -C $$dir CURRENT_PLATFORM=$$p PLATFORM=$$p clean; \
		done; \
	done