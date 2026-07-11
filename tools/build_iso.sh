#!/bin/bash
set -e

# ── Configuration ──
ISO_DIR="build/iso"              # staging directory for ISO content
KERNEL_BIN="build/kernel.elf"    # final linked kernel
GRUB_CFG="$ISO_DIR/boot/grub/grub.cfg"
ISO_OUT="build/kumo.iso"

# Ensure cross-compiler is in PATH
export PATH="$HOME/i686-elf-tools/bin:$PATH"

# ── Step 1: Compile ──
echo "==> Assembling boot/multiboot_header.asm"
nasm -f elf32 boot/multiboot_header.asm -o build/mb_header.o

echo "==> Assembling arch/x86/boot.asm"
nasm -f elf32 arch/x86/boot.asm -o build/boot.o

CFLAGS="-m32 -ffreestanding -nostdlib -Wall -Wextra"

echo "==> Compiling drivers/serial.c"
i686-elf-gcc $CFLAGS -c drivers/serial.c -o build/serial.o

echo "==> Compiling kernel/main.c"
i686-elf-gcc $CFLAGS -c kernel/main.c -o build/kmain.o

echo "==> Linking $KERNEL_BIN"
i686-elf-ld -T linker.ld -o "$KERNEL_BIN" \
    build/mb_header.o build/boot.o build/kmain.o build/serial.o

# ── Step 2: Create ISO directory tree ──
# GRUB's mkrescue expects: <root>/boot/grub/grub.cfg
# Our kernel goes under /boot/ so grub.cfg can reference it as /boot/kernel.elf
echo "==> Creating ISO directory tree"
mkdir -p "$ISO_DIR/boot/grub"
cp "$KERNEL_BIN" "$ISO_DIR/boot/kernel.elf"

# ── Step 3: Write grub.cfg ──
# timeout=0: boot immediately without showing menu
# multiboot2: use Multiboot2 protocol to load the kernel ELF
cat > "$GRUB_CFG" << 'EOF'
set timeout=0
set default=0

menuentry "Kumo OS" {
    multiboot2 /boot/kernel.elf
    boot
}
EOF

# ── Step 4: Build ISO ──
# -o: output ISO path
# $ISO_DIR: root of the ISO filesystem
echo "==> Building ISO with grub-mkrescue"
grub-mkrescue -o "$ISO_OUT" "$ISO_DIR"

echo "==> Done: $ISO_OUT"
ls -lh "$ISO_OUT"
