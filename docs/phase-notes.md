# Phase 1 笔记

## 踩坑记录

### 用 QEMU 调试参数区分"正常黑屏"和"三重 fault 重启"

**日期**：2026-07-10

**场景**：在串口驱动就绪之前，内核唯一的输出手段是 VGA 文本模式，
但我们选择了 `-nographic` 模式测试，导致完全看不到屏幕内容。
此时内核如果 boot 成功（卡在 `hlt` 循环）和启动失败（三重 fault
导致 QEMU 静默重启）在外部观察中完全一样——都是"终端没有任何输出"。

**解决方案**：使用 QEMU 的调试参数：

```bash
qemu-system-i386 -cdrom build/kumo.iso \
  -d int,cpu_reset \
  -no-reboot \
  -no-shutdown \
  -nographic
```

各参数的作用：

| 参数 | 作用 |
|---|---|
| `-d int,cpu_reset` | 将所有异常/中断和 CPU 重置事件打印到 stderr |
| `-no-reboot` | 发生三重 fault 时 QEMU 不自动重启，而是停止并打印错误信息 |
| `-no-shutdown` | 发生 shutdown 事件时不退出 |

**判定方法**：
- 日志里**没有** `check_exception` / `triple fault` / 异常的 `CPU Reset` → 内核引导成功，卡在 `hlt`
- 日志里**出现**异常中断 dump 或 `CPU Reset`（非初始上电那次）→ 内核崩溃，发生了三重 fault

**后续价值**：这个技巧在阶段 2/3 调试中断异常和分页时会反复用到——
当 CPU 因缺页异常跳到了一个错误的处理函数、导致二重/三重 fault 时，
这些日志是唯一的线索。没有 `-d int -no-reboot`，你只会看到 QEMU 窗口
闪了一下然后无限重启，完全不知道发生了什么。

## `i686-elf-gdb` 连接 QEMU 6.2 GDB stub 时寄存器包不匹配

**日期**：2026-07-11

**场景**：用 `i686-elf-gdb`（交叉工具链自带）连接 `qemu-system-i386 -s -S`
时，报错 `Remote 'g' packet reply is too long (expected 308 bytes, got 344 bytes)`，
导致 GDB 在 `target remote` 这一步直接失败，无法断点调试。

**原因**：`i686-elf-gdb` 编译时目标为纯 32 位，期望 308 字节的通用寄存器包；
QEMU 6.2 的 GDB stub 即使运行时是 32 位虚拟机，仍发送 x86_64 格式的 344 字节
寄存器包（包含 64 位寄存器 + XMM 扩展）。尝试 `-cpu qemu32` 无效，这是 QEMU
GDB stub 的实现层面的限制。

**解决方案**：换用系统自带的 `gdb-multiarch`（Ubuntu 22.04 自带 12.1），
配合 `set architecture i386`，即可正常连接、断点、查看段寄存器和 GDTR 等。
`gdb-multiarch` 的寄存器包解析足够灵活，能兼容 QEMU 6.2 的格式差异。

**后续价值**：阶段 2/3 调试中断和分页时需要频繁用 GDB 检查寄存器，遇到 GDB
连接失败先检查是不是这个包大小问题——换 `gdb-multiarch` 是第一反应。

## 串口 vs VGA 作为主调试通道

**决策**：串口（COM1 0x3F8）作为首要调试输出通道。

**理由**：
1. QEMU `-nographic` 可以直接把串口数据重定向到当前终端，命令行里直接看到
2. QEMU `-serial file:log.txt` 可以把串口输出保存到文件，适合自动化冒烟测试（`grep "Kumo OS booted." log.txt`）
3. 串口驱动的复杂度（几十行 C）远低于 VGA 文本模式驱动（需要处理光标、颜色、滚动）
4. 后续自动化测试可以直接写脚本断言串口输出，不需要截图比对

**VGA 的价值**：作为辅助的视觉验证手段，在 GUI 模式下可以快速瞟一眼确认内核在跑。但不应作为主要的自动化验证通道。

## pmm_alloc_contiguous_pages() — 连续物理页分配接口

**日期**：2026-07-11（阶段 4）

**背景**：阶段 4 多任务调度中，任务内核栈需要 2 页（8KB）连续物理内存。
`pmm_alloc_page()` 每次只分配单页，两次调用不保证物理连续。

**新增接口**：`uint32_t pmm_alloc_contiguous_pages(uint32_t count)` — 扫描位图
找到 `count` 个连续空闲位，标记为已分配，返回起始物理地址。

**后续复用场景**：
- 阶段 5（用户态）：用户态任务可能需要更大的连续物理内存（如 TSS 的 I/O
  位图、LDT 等）
- 阶段 7（磁盘/块设备）：DMA 缓冲区通常需要物理连续的多页内存（BIOS ATA
  PIO 模式的最低要求是单页 512 字节，但 DMA 模式需要连续物理缓冲区）
- 任何需要跨越页边界的、物理地址连续的数据结构（以后的内存映射文件、
  大块网络缓冲区等）

不要为每个阶段重新发明一个连续分配变体——在需要更高级语义（如 buddy
allocator 的按阶分配）之前，这个简单扫描接口足够覆盖当前需求。
