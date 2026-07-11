# Kumo OS 开发进度

## 阶段 0：环境搭建 ✓

- [x] 基础依赖安装
- [x] 交叉编译工具链就绪（i686-elf-gcc 13.2.0，已有预编译版本）
- [x] 仓库骨架创建
- [x] 交叉编译器验证

## 阶段 1：启动+输出 ✓ (2026-07-10)

- [x] Multiboot2 header（`boot/multiboot_header.asm`）
- [x] 内核链接脚本（`linker.ld`，加载地址 0x100000）
- [x] 启动入口（`arch/x86/boot.asm`：设栈、清零 BSS、跳转 C）
- [x] 内核 C 入口（`kernel/main.c`）
- [x] ISO 构建脚本（`tools/build_iso.sh`）
- [x] GRUB 引导 → QEMU 验证（黑屏 hlt，无 triple fault）
- [x] 串口输出驱动（`drivers/serial.c` + `drivers/serial.h`）
- [x] 终端可见 "Kumo OS booted."

## 阶段 2：中断异常框架

- [x] GDT（平坦模型：null + kernel code + kernel data，2026-07-11）
  - 验证方式：GDB 确认 CS=0x08, DS/ES/FS/GS/SS=0x10
  - 验证方式：QEMU monitor `info registers` 确认 GDT base/layout/描述符字段 全部正确
  - 验证方式：`make run` 串口输出 `GDT initialized.` 无 crash
- [x] IDT（256-entry skeleton，所有表项 → isr_stub 占位 handler，2026-07-11）
  - 验证方式：GDB 确认 IDTR base/layout 正确（limit=0x7FF = 256*8-1）
  - 验证方式：`make run` 串口输出 `IDT initialized.` 无 crash，IF=0 中断关闭
- [x] ISR trampoline（32个异常向量，pusha+iret，2026-07-11）
  - 验证方式：`#DE` 验证 ISR_NOERR 路径（push dummy 0）→ 寄存器 dump 正确
  - 验证方式：`#GP` 验证 ISR_ERR 路径（CPU 自动压 error code = 0x0018）→ ErrCode 字段正确
  - 验证方式：`-d int` 日志 + objdump 反汇编，EIP 精确落在 faulting 指令上
  - 注：32 项 vector error code 表与 Intel SDM 逐行核对，0/32 差异
- [x] 8259 PIC 重映射（IRQ0-15 → vectors 32-47，2026-07-11）
  - 验证方式：QEMU monitor `info pic` 确认 `irq_base=20`(master) / `irq_base=28`(slave)
  - 验证方式：串口输出链路 booted→GDT→IDT→PIC remapped 完整
- [x] IRQ0 时钟中断 handler + sti + 周期性 tick（2026-07-11）
  - 验证方式：串口连续输出 tick（~16-18/s），5 秒累计 99+ tick 无中断
  - 验证方式：`-d int` 日志仅见 `v=20`（IRQ0 向量 32），无其他异常/三重 fault
  - 验证方式：EOI 正确发送（tick 持续不冻结），PIC 重映射验证（IRQ0→32 而非默认的 8）

## 踩坑记录

- 2026-07-10：QEMU `-nographic` 模式下，GRUB/SeaBIOS 的引导信息走 VGA 不显示在终端。在串口驱动就绪前，只能靠 `-d int,cpu_reset -no-reboot` 间接确认"没有 triple fault"来推断内核在运行。详见 `docs/phase-notes.md`。
