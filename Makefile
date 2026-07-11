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
ASM_SRCS    := boot/multiboot_header.asm arch/x86/boot.asm arch/x86/isr_stub.asm arch/x86/isr.asm arch/x86/irq.asm sched/switch.asm arch/x86/syscall.asm arch/x86/ring3.asm
C_SRCS      := kernel/main.c drivers/serial.c arch/x86/gdt.c arch/x86/idt.c arch/x86/exception.c arch/x86/pic.c arch/x86/irq_handler.c mm/multiboot.c mm/pmm.c arch/x86/paging.c mm/kheap.c sched/task.c arch/x86/tss.c arch/x86/syscall_dispatch.c fs/vfs.c fs/ramfs.c fs/elf.c gfx/primitives.c gfx/font.c

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

$(KERNEL_ELF): $(OBJS) linker.ld $(BUILD_DIR)/test_ramfs.h $(BUILD_DIR)/shell.h $(BUILD_DIR)/test_bad_ptr.h $(BUILD_DIR)/test_boundary.h $(BUILD_DIR)/test_null.h $(BUILD_DIR)/hello_elf.h $(BUILD_DIR)/regtest_a.h $(BUILD_DIR)/regtest_b.h
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

$(BUILD_DIR)/main.o: kernel/main.c drivers/serial.h arch/x86/gdt.h arch/x86/idt.h arch/x86/pic.h arch/x86/irq.h mm/multiboot.h arch/x86/paging.h arch/x86/tss.h arch/x86/syscall.h fs/vfs.h fs/ramfs.h fs/elf.h gfx/primitives.h gfx/font.h $(BUILD_DIR)/test_ramfs.h $(BUILD_DIR)/shell.h $(BUILD_DIR)/test_bad_ptr.h $(BUILD_DIR)/test_boundary.h $(BUILD_DIR)/test_null.h $(BUILD_DIR)/hello_elf.h $(BUILD_DIR)/regtest_a.h $(BUILD_DIR)/regtest_b.h | $(BUILD_DIR)
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

$(BUILD_DIR)/multiboot.o: mm/multiboot.c mm/multiboot.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pmm.o: mm/pmm.c mm/pmm.h mm/multiboot.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/paging.o: arch/x86/paging.c arch/x86/paging.h mm/pmm.h mm/multiboot.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kheap.o: mm/kheap.c mm/kheap.h mm/pmm.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/switch.o: sched/switch.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/syscall.o: arch/x86/syscall.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/ring3.o: arch/x86/ring3.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/tss.o: arch/x86/tss.c arch/x86/tss.h arch/x86/gdt.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/syscall_dispatch.o: arch/x86/syscall_dispatch.c arch/x86/syscall.h arch/x86/paging.h sched/task.h fs/vfs.h mm/kheap.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs.o: fs/vfs.c fs/vfs.h sched/task.h mm/kheap.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/ramfs.o: fs/ramfs.c fs/ramfs.h fs/vfs.h mm/kheap.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/task.o: sched/task.c sched/task.h mm/kheap.h mm/pmm.h arch/x86/paging.h arch/x86/tss.h fs/vfs.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/elf.o: fs/elf.c fs/elf.h mm/pmm.h arch/x86/paging.h drivers/serial.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/primitives.o: gfx/primitives.c gfx/primitives.h mm/multiboot.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/font.o: gfx/font.c gfx/font.h gfx/primitives.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ── Ensure build dir exists ──

$(BUILD_DIR):
	mkdir -p $@

# ── User test program: assemble flat binary → C header ──

$(BUILD_DIR)/test_ramfs.h: user/test_ramfs.asm | $(BUILD_DIR)
	nasm -f bin $< -o $(BUILD_DIR)/test_ramfs.bin
	xxd -i $(BUILD_DIR)/test_ramfs.bin > $@

$(BUILD_DIR)/shell.h: user/shell.asm | $(BUILD_DIR)
	nasm -f bin $< -o $(BUILD_DIR)/shell.bin
	xxd -i $(BUILD_DIR)/shell.bin > $@

$(BUILD_DIR)/test_bad_ptr.h: user/test_bad_ptr.asm | $(BUILD_DIR)
	nasm -f bin $< -o $(BUILD_DIR)/test_bad_ptr.bin
	xxd -i $(BUILD_DIR)/test_bad_ptr.bin > $@

$(BUILD_DIR)/test_boundary.h: user/test_boundary.asm | $(BUILD_DIR)
	nasm -f bin $< -o $(BUILD_DIR)/test_boundary.bin
	xxd -i $(BUILD_DIR)/test_boundary.bin > $@

$(BUILD_DIR)/test_null.h: user/test_null.asm | $(BUILD_DIR)
	nasm -f bin $< -o $(BUILD_DIR)/test_null.bin
	xxd -i $(BUILD_DIR)/test_null.bin > $@

# ── ELF test program: assemble + link → ELF32 → C header ──

$(BUILD_DIR)/hello_elf.o: user/hello_elf.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/hello_elf.elf: $(BUILD_DIR)/hello_elf.o user/elf_i386.ld
	$(LD) -m elf_i386 -T user/elf_i386.ld $< -o $@

$(BUILD_DIR)/hello_elf.h: $(BUILD_DIR)/hello_elf.elf | $(BUILD_DIR)
	xxd -i $< > $@

$(BUILD_DIR)/regtest_a.h: user/regtest_a.asm | $(BUILD_DIR)
	nasm -f bin $< -o $(BUILD_DIR)/regtest_a.bin
	xxd -i $(BUILD_DIR)/regtest_a.bin > $@

$(BUILD_DIR)/regtest_b.h: user/regtest_b.asm | $(BUILD_DIR)
	nasm -f bin $< -o $(BUILD_DIR)/regtest_b.bin
	xxd -i $(BUILD_DIR)/regtest_b.bin > $@
