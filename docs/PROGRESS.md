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

## 阶段 3：内存管理（进行中）

- [x] Multiboot2 内存 map 解析（`mm/multiboot.c`，2026-07-11）
  - 提取 AVAILABLE 区域到内核所属 `g_memory_map` 数组，解析后不依赖原始 mb_info 指针
  - 验证方式：输出 2 个可用区域：[0x0, 0x9FC00) 639KB 低端 + [0x100000, 0x7FE0000) ~127MB 高端
- [x] 物理页帧分配器 位图法（`mm/pmm.c`，2026-07-11）
  - managed_base=0x100000(1MB)，位图覆盖 [1MB, 0x7FE0000)，全1初始化→释放 first_free 以上
  - 关键数字：total_pages=0x7EE0(32480), bitmap_bytes=0xFDC(4060≈4KB), bitmap_pages=1
  - _kernel_end=0x1088dc, bitmap at 0x109000, first_free=0x10A000, free_pages=0x7ED6(32470)
  - 健全性测试：pmm_alloc_page() 返回 0x10A000 ≡ first_free，内核+位图未被分配
- [x] 分页（`arch/x86/paging.c`，2026-07-11）
  - 全物理内存恒等映射（virtual == physical），覆盖 [0, 0x07FE0000) ~128MB
  - 两级 4KB 页表（10-10-12），32 个 PT + 1 个 PD = 33 页（~132KB 开销）
  - 页表页面从 PMM 分配，开启前 IF=0（中断关闭），开启后验证成功再 sti
  - ADR-003：暂不做 NULL 页 guard（物理页 0 可访问），后续可单独 unmap
  - GDB 单步验证：CR3=0x10C000, CR0.PG 位从 0→1, 取指无异常, PDE[0] Accessed 位被硬件置位
  - 全链路验证：booted→GDT→IDT→PIC→Memory map→PMM→Paging enabled→sti→tick 连续
- [x] 内核堆 kmalloc/kfree（`mm/kheap.c`，2026-07-11）
  - 显式自由链表 + first-fit，block 不跨页（单页内最多 4096 字节）
  - 页首=header 的不变量，backward coalescing 从页首 walk 合法 header 链
  - forward coalescing 通过地址算术 (header+size) 判断，仅页内
  - 初始 16 页（~64KB），按需扩展（通过 pmm_alloc_page()）
  - 6 项测试全覆盖：basic/kfree/write/forward/backward/heap expansion (200×500B, 触发 10 次扩展)
- [x] **阶段 3 完成**：内存管理子系统（Multiboot 解析 → PMM 位图 → 恒等分页 → 内核堆）
