# Kumo OS — top-level Makefile

CROSS_PREFIX := $(HOME)/i686-elf-tools/bin/i686-elf-
CC          := $(CROSS_PREFIX)gcc
LD          := $(CROSS_PREFIX)ld
NASM        := nasm

CFLAGS      := -m32 -ffreestanding -nostdlib -Wall -Wextra -g
LDFLAGS     := -T linker.ld

BUILD_DIR   := build
ISO_DIR     := $(BUILD_DIR)/iso
KERNEL_ELF  := $(BUILD_DIR)/kernel.elf
ISO_OUT     := $(BUILD_DIR)/kumo.iso

# Sources
ASM_SRCS    := boot/multiboot_header.asm arch/x86/boot.asm arch/x86/isr_stub.asm arch/x86/isr.asm arch/x86/irq.asm
C_SRCS      := kernel/main.c drivers/serial.c arch/x86/gdt.c arch/x86/idt.c arch/x86/exception.c arch/x86/pic.c arch/x86/irq_handler.c

# Objects (under build/)
ASM_OBJS    := $(patsubst %.asm,$(BUILD_DIR)/%.o,$(notdir $(ASM_SRCS)))
C_OBJS      := $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(C_SRCS)))
OBJS        := $(ASM_OBJS) $(C_OBJS)

# ── Top-level targets ──

.PHONY: all run clean

all: $(ISO_OUT)

run: all
	qemu-system-i386 -cdrom $(ISO_OUT) -nographic

clean:
	rm -rf $(BUILD_DIR)

# ── ISO ──

$(ISO_OUT): $(KERNEL_ELF)
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernel.elf
	@echo 'set timeout=0'              >  $(ISO_DIR)/boot/grub/grub.cfg
	@echo 'set default=0'              >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo ''                           >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo 'menuentry "Kumo OS" {'      >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo '    multiboot2 /boot/kernel.elf' >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo '    boot'                   >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo '}'                          >> $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_DIR)

# ── Link ──

$(KERNEL_ELF): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# ── Assemble (each .asm → build/<name>.o) ──

$(BUILD_DIR)/multiboot_header.o: boot/multiboot_header.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/boot.o: arch/x86/boot.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/isr_stub.o: arch/x86/isr_stub.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/isr.o: arch/x86/isr.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/irq.o: arch/x86/irq.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

# ── Compile C (each .c → build/<name>.o) ──

$(BUILD_DIR)/main.o: kernel/main.c drivers/serial.h arch/x86/gdt.h arch/x86/idt.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial.o: drivers/serial.c drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt.o: arch/x86/gdt.c arch/x86/gdt.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: arch/x86/idt.c arch/x86/idt.h arch/x86/isr.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/exception.o: arch/x86/exception.c arch/x86/isr.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pic.o: arch/x86/pic.c arch/x86/pic.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/irq_handler.o: arch/x86/irq_handler.c arch/x86/irq.h arch/x86/isr.h arch/x86/idt.h arch/x86/pic.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ── Ensure build dir exists ──

$(BUILD_DIR):
	mkdir -p $@
