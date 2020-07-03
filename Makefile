
CC           := gcc
CFLAGS       := -Wall -Wextra -pedantic -MMD -MP -I. -Wno-unused-parameter
MKFSFAT      := $(shell which mkfs.fat)
VALGRIND     := $(shell which valgrind)

SOURCE_TOOLS := $(wildcard tools/*.c)
TARGET_TOOLS := $(basename $(SOURCE_TOOLS))

SOURCE_TEST  := $(wildcard test/*.c)
TARGET_TEST  := $(basename $(SOURCE_TEST))
IMAGES_TEST  := test/fat12.bin test/fat16.bin test/fat32.bin

SOURCES      := fat.c $(SOURCE_TOOLS) $(SOURCE_TEST)
DEPENDS      := $(patsubst %.c,%.d,$(SOURCES))

.PHONY: all clean clean_test clean_tools test tools

all: fat.o

clean: clean_test clean_tools
	rm -f fat.o fat.d
	rm -f $(IMAGES_TEST)

clean_test:
	rm -f test/*.o test/*.d
	rm -f $(TARGET_TEST)

clean_tools:
	rm -f tools/*.o tools/*.d
	rm -f $(TARGET_TOOLS)

tools: $(TARGET_TOOLS)

$(TARGET_TOOLS): fat.o
$(TARGET_TOOLS): % : %.o
	$(CC) -o $@ $^

test: $(TARGET_TEST)

$(TARGET_TEST): fat.o
$(TARGET_TEST): % : %.o
	$(CC) -o $@ $^

runtest: test $(IMAGES_TEST)
ifndef VALGRIND
	$(error valgrind not found)
endif
	@rm -f /tmp/test_fatfs_log
	$(foreach F,$(TARGET_TEST),$(call runtest,$(F),$(IMAGES_TEST)))

$(IMAGES_TEST): test/%.bin :
ifndef MKFSFAT
	$(error mkfs.fat not found)
endif
	$(call genfat,$(subst fat,,$*),/tmp/$*)
	$(call mountfat,/tmp/$*,/tmp/fat)
	$(call copyfiles,/tmp/fat)
	$(call umountfat,/tmp/fat)
	@cp /tmp/$* $@
	@rm -f /tmp/$*

define genfat
	@echo "creating fat$(1)"
	@dd if=/dev/urandom of=$(2) bs=512 count=122880 > /dev/null 2>&1
	@$(MKFSFAT) -n DISKTEST -F $(1) $(2) > /dev/null 2>&1
endef

define mountfat
	@mkdir -p $(2)
	@sudo mount $(1) $(2)
endef

define copyfiles
	@echo "This is the first file!" > /tmp/FIRST.txt
	@echo "This is the second file!" > /tmp/Second_File_Using_Long_Name.txt
	@sudo cp /tmp/FIRST.txt $(1)
	@sudo cp /tmp/Second_File_Using_Long_Name.txt $(1)
endef

define umountfat
	@sudo umount $(1)
endef

define runtest
	@echo -n $(1):
	@echo "----- $(1) -----" >> /tmp/test_fatfs_log
	@$(VALGRIND) ./$(1) $(2) >> /tmp/test_fatfs_log 2>&1 && echo "Success" || echo "Failure"

endef

-include $(DEPENDS)
