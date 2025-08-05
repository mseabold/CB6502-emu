TEST_SRCS := $(wildcard *.s)
TEST_BINS := $(patsubst %.s,bin/%.bin,$(TEST_SRCS))

.PHONY:
all: $(TEST_BINS)

obj/%.o: %.s common.inc
	@mkdir -p $(@D)
	ca65 --cpu 65c02 -o $@ $<

bin/%.bin: obj/%.o common_linker.ld
	@mkdir -p $(@D)
	ld65 -C common_linker.ld -o $@ $<
