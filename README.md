# Kumo OS

**Kumo OS**（Kumo = 雲 / くも）是一个从零开发的 x86 32 位教学级操作系统内核，完全使用 C 语言和 NASM 汇编编写。项目覆盖了从 GRUB 引导到图形用户界面的完整技术栈——内存管理、中断异常处理、多任务调度、文件系统、ELF 加载器、framebuffer 图形驱动、窗口合成器、按钮控件，最终交付一个能进行四则运算的图形计算器应用。

约 7,000 行源码，29 个 commit，全部通过与 [Claude Code](https://claude.ai/code) 结对编程完成，有效开发时间约 12 小时。

> This is a **teaching OS kernel** for x86 32-bit protected mode, built from scratch in C + NASM assembly. It covers the full stack from boot to GUI — memory management, interrupt handling, multitasking, VFS, ELF loader, framebuffer graphics, window compositor, button widgets, and a working calculator app. ~7,000 SLOC, developed entirely in pair-programming with Claude Code.

---

## 功能特性 (Features)

### 核心内核（阶段 0–8 / Core Kernel）

| 子系统 | 实现内容 |
|--------|---------|
| **引导 (Boot)** | Multiboot2 GRUB 引导，32 位保护模式入口 |
| **中断/异常 (IDT/ISR)** | GDT + IDT 初始化，32 个异常向量的 ISR trampoline（`ISR_NOERR` / `ISR_ERR` 双路径），#DE / #GP / #PF 真实异常触发验证 |
| **中断控制器 (PIC)** | 8259A PIC 重映射（IRQ0–15 → 向量 32–47），IRQ0 时钟中断、IRQ12 鼠标中断 |
| **物理内存管理 (PMM)** | 位图分配器 (bitmap allocator)，从 Multiboot2 内存图自动计算可用物理页 |
| **分页 (Paging)** | 两级 4KB 页表，全物理内存恒等映射 (identity mapping)，NULL 页解映射 (#PF 验证) |
| **内核堆 (KHeap)** | 基于双向链表的显式自由列表分配器，支持 forward/backward coalescing，6 项覆盖性测试 |
| **多任务 (Multitasking)** | 协作式调度 + 抢占式调度 (IRQ0 时钟)，`switch_to` 栈自描述性设计，每任务独立内核栈 |
| **特权级切换 (Ring0/Ring3)** | TSS 设置、`return_to_ring3` trampoline、syscall 门 (IDT[0x80], DPL=3) |
| **系统调用 (Syscall)** | 7 个 syscall (open/read/write/close/yield/readdir/run)，`copy_from_user` 指针校验覆盖全部用户态指针 |
| **VFS + RamFS** | 虚拟文件系统层 + 内存文件系统实现，支持文件创建/读写/目录操作 |
| **Shell** | 用户态 shell 程序，通过 syscall 与内核交互 |
| **ELF32 加载器** | ET_EXEC 解析 + 程序头 LOAD 段映射 + `elf_setup_user_stack` (argc/argv)，PIE-only 方案 |

### GUI 扩展（阶段 9–15 / GUI Extension）

| 子系统 | 实现内容 |
|--------|---------|
| **Framebuffer 驱动** | Multiboot2 framebuffer tag 解析，物理内存跨范围映射 (`paging_map_phys_range`)，color_info 字段精确验证 |
| **2D 绘图原语 (Primitives)** | `put_pixel` / `get_pixel`、Bresenham `draw_line`、`draw_rect` / `fill_rect`，像素采样验证 |
| **位图字体 (Bitmap Font)** | 自制 8×16 位图字体，覆盖 95 个 ASCII 可打印字符，无第三方字体授权依赖 |
| **抢占式调度 (Preemptive)** | IRQ0 接入 `schedule()`，250+ tick 寄存器完整性验证 (regtest_a/b)，功能回归测试全部通过 |
| **PS/2 鼠标驱动** | IRQ12 处理 → 3 字节包解析，光标 save/restore 机制（备份像素复原），移动边界裁剪 |
| **进程隔离 (Isolation)** | 每任务独立页目录 (Page Directory)，`test_probe_a` 跨任务读取触发 #PF ErrCode=0x05 精确验证隔离生效 |
| **窗口系统 (WM)** | `window_t` 结构体、标题栏 + 主体渲染、Z 序多窗口合成 (Z-order compositing)、点击置顶、单窗口→多窗口渐进 |
| **输入路由 (Input)** | `mouse_click_callback` 回调机制：驱动层只上报原始事件 (x, y, buttons)，上层做命中检测——`drivers/` 不依赖 `wm/` |
| **按钮控件 (Button)** | press/release/cancel 状态机，`g_pressed_button` 指针追踪，两阶段点击路由（按钮优先于窗口 Z 序） |
| **计算器应用 (Calculator)** | 16 按钮数组、accumulator/current/pending_op 四则运算状态机、显示屏右对齐更新、5+3=8 验证通过 |

---

## 技术亮点 (Technical Highlights)

### 每一层都有实测证据，不是"看起来对"

项目贯穿始终的工程原则：**每个新机制上线时，必须用可验证的证据证明它确实在工作**——不是"编译通过"，不是"没崩溃"，而是具体到寄存器位、错误码、像素值。

- **进程隔离的精确验证**：阶段 12 的 `test_probe_a` 尝试读取另一个进程的物理页面。验证不满足于"触发了一个 #PF"——而是检查 **CR2=0x0080E200**（目标地址）和 **ErrCode=0x05**（bit 0=present 为保护性错误非缺页，bit 2=user 表明确实是用户态访问触发的）。这三个数字一起锁死了"隔离在硬件层面生效"的结论。

- **ELF 加载器的边角情况覆盖**：阶段 8a 的 `test_boundary` 程序把路径字符串放在页面偏移 0xFFD 处（距 4KB 页尾仅 3 字节），验证 `copy_from_user_string` 在逐字节扫描时不会越界访问下一个页面。这个用例源自 ADR-004 讨论中识别出的"字符串跨页时如果按页粒度预验证会读到字符串范围外的内存"的隐患。

完整的技术决策和踩坑记录见 [`docs/decisions.md`](docs/decisions.md)（8 篇 ADR）和 [`docs/phase-notes.md`](docs/phase-notes.md)。

### 架构分层干净

```
app/calc.c          ← 应用层（计算器）
wm/wm.c button.c    ← 窗口管理层（合成器、控件）
gfx/primitives.c    ← 绘图原语层（像素、线条、矩形、文字）
drivers/mouse.c     ← 硬件驱动层（PS/2、串口）
```

**依赖方向严格单向**：上层可以包含下层头文件，下层不知道上层存在。阶段 13b 曾因 `mouse.c` 可能反向依赖 `wm/window.h` 的架构失误被纠正——改为回调函数指针 (`mouse_click_callback_t`)，驱动只上报 `(x, y, buttons)`，由上层自行做窗口命中检测。

---

## 如何构建运行 (Build & Run)

### 环境要求

- **Ubuntu 22.04**（或其他支持 APT 的 Linux 发行版）
- **NASM** (汇编器)
- **GRUB** (`grub-mkrescue`, `xorriso`)
- **QEMU** (`qemu-system-i386`)
- **i686-elf 交叉工具链** (GCC 13.2.0 + Binutils 2.41)

```bash
# 安装依赖
sudo apt install nasm grub-pc-bin xorriso qemu-system-x86

# 交叉编译器可以从此处获取预编译版本：
# https://github.com/nativeos/i686-elf-tools/releases
# 解压到 ~/i686-elf-tools/ 目录
```

### 构建 ISO 并运行

```bash
# 构建 ISO（输出 build/kumo.iso）
make

# 纯串口模式运行（无 GUI 输出，仅串口日志）
make run

# VNC 模式运行（图形界面可通过 VNC 查看）
make run-vnc
# 然后用 VNC 客户端连接 localhost:5900 (display :0)

# VNC + monitor 模式（可同时使用 QEMU monitor 和 VNC）
make run-vnc-mon
```

### VNC 连接

```bash
# 使用 vncdotool（推荐，支持编程控制）
pip install vncdotool
vncdotool -s :0 capture screenshot.png

# 或使用任意 VNC viewer 连接
vncviewer localhost:0
```

`make run` 使用 `-nographic` 模式，所有内核日志通过串口（COM1, 0x3F8）输出到终端——无需 GUI 即可看到完整的启动、测试、调度日志。

---

## 项目结构 (Project Structure)

```
KumoOS/
├── boot/                   # Multiboot2 引导头
│   └── multiboot_header.asm
├── arch/x86/               # x86 架构相关
│   ├── boot.asm            # 32 位保护模式入口
│   ├── gdt.c/h             # 全局描述符表
│   ├── idt.c/h             # 中断描述符表
│   ├── isr.asm             # ISR/IRQ 汇编 trampoline
│   ├── isr_stub.asm        # 异常 stub（ISR_NOERR / ISR_ERR）
│   ├── irq.asm/h           # IRQ 入口
│   ├── irq_handler.c       # IRQ 分发（IRQ0 调度 + IRQ12 鼠标）
│   ├── pic.c/h             # 8259A PIC 驱动
│   ├── paging.c/h          # 分页 + 用户指针校验
│   ├── tss.c/h             # TSS (Task State Segment)
│   ├── syscall.asm/h       # syscall 门 (int 0x80)
│   ├── syscall_dispatch.c  # syscall 分发
│   ├── ring3.asm           # Ring3 返回 trampoline
│   └── exception.c         # 异常处理器
├── mm/                     # 内存管理
│   ├── multiboot.c/h       # Multiboot2 信息解析
│   ├── pmm.c/h             # 物理内存管理 (位图分配器)
│   └── kheap.c/h           # 内核堆 (链表分配器)
├── sched/                  # 调度器
│   ├── task.c/h            # 任务管理 (TCB, 创建/调度)
│   └── switch.asm          # 上下文切换 (switch_to)
├── fs/                     # 文件系统
│   ├── vfs.c/h             # 虚拟文件系统层
│   ├── ramfs.c/h           # 内存文件系统
│   └── elf.c/h             # ELF32 加载器
├── drivers/                # 硬件驱动
│   ├── serial.c/h          # 串口驱动 (COM1, 调试输出)
│   └── mouse.c/h           # PS/2 鼠标驱动 + 光标管理
├── gfx/                    # 图形子系统
│   ├── primitives.c/h      # 2D 绘图原语 (像素/线/矩形)
│   └── font.c/h            # 8×16 位图字体 (95 ASCII 字符)
├── wm/                     # 窗口管理器
│   ├── window.c/h          # 窗口结构体 + 绘制
│   ├── wm.c/h              # 窗口管理器 (Z 序合成)
│   └── button.c/h          # 按钮控件 (状态机)
├── app/                    # 应用程序
│   └── calc.c/h            # 图形计算器 (16 按钮, 四则运算)
├── kernel/
│   └── main.c              # 内核入口 kmain() + 初始化流程
├── user/                   # 用户态程序 (NASM 汇编)
│   ├── shell.asm           # Shell (syscall 交互)
│   ├── test_ramfs.asm      # VFS/RamFS 功能测试
│   ├── test_bad_ptr.asm    # 指针校验测试
│   ├── test_boundary.asm   # 页面边界字符串测试
│   ├── test_null.asm       # NULL 解引用测试
│   ├── hello_elf.asm       # ELF32 Hello World
│   ├── regtest_a/b.asm     # 寄存器完整性验证 (抢占)
│   ├── test_probe_a/b.asm  # 进程隔离验证
│   └── elf_i386.ld         # ELF 链接脚本
├── docs/                   # 项目文档
│   ├── PROGRESS.md         # 各阶段实现记录
│   ├── decisions.md        # 架构决策记录 (ADR)
│   ├── phase-notes.md      # 阶段踩坑笔记
│   └── milestone-review-*.md  # 里程碑回顾
├── Makefile                # 构建系统
├── linker.ld               # 内核链接脚本
├── .gitignore
└── README.md
```

---

## 开发过程 (Development Process)

本项目完全通过与 **Claude Code** 结对编程完成，采用"先方案讨论、后写代码、每步验证"的工作方式：

1. **方案讨论**：每个阶段开始前，先在对话中讨论技术方案——数据结构设计、状态机语义、架构分层边界，确认后再动手
2. **增量实现**：一个阶段只做一件事，每个 commit 对应一个完整的子功能
3. **自检验证**：所有功能通过 `sti` 前直接调用函数进行串口自检——不依赖用户交互来验证正确性，每个测试点都有精确的期望值和实际值对照
4. **架构决策记录**：重要的设计取舍（为什么做某个简化、为什么不现在做某件事）记录在 `docs/decisions.md`，附带了具体的原因、触发条件和后续方案

29 个 commit 保留了完整的开发轨迹——从"第一个像素点亮"到"图形计算器做 5+3=8"，每一步的交付物和验证证据都可追溯。

对具体的工程决策过程感兴趣，推荐阅读：
- [`docs/decisions.md`](docs/decisions.md) — 8 篇 ADR + 1 篇经验记录，覆盖分页方案、NULL 页 guard、syscall 指针校验、ELF PIE-only、光标/窗口交互简化、IRQ12 重绘延迟分析、按钮视觉反馈简化
- [`docs/phase-notes.md`](docs/phase-notes.md) — 各阶段的踩坑记录和设计笔记

---

## 未来方向 (Roadmap)

### GUI 子系统（已规划，待实现 / Planned）
- **Damage tracking（脏矩形优化）**：非全屏重绘，只更新变化区域，同时自然缓解 Decision-007 记录的 IRQ12 重绘延迟问题
- **窗口交互增强**：窗口拖动、调整大小、关闭按钮
- **更多控件**：文本框、复选框（按钮状态机验证通过后，其他控件是同样模式的重复）
- **用户态 GUI 迁移**：`app/calc.c` 的内核接口模式（`init → 接收事件 → 调用绘图原语`）就是未来用户态 GUI 程序的雏形

### 核心内核（待探索 / To Explore）
- **64 位长模式 (x86-64 Long Mode)**：ADR-001 已记录"先 32 位再 64 位"的策略，当前所有 32 位机制在 64 位下对应物清晰（PML4、寄存器扩展、SMEP/SMAP）
- **更多文件系统**：FAT32 或 ext2 读写支持
- **网络栈**：NE2000 或 e1000 网卡驱动 + 简易 TCP/IP
- **更多用户态应用**：文本编辑器、简单游戏等

---

## 许可 (License)

本项目为个人学习项目，当前未指定开源许可证。源码仅供学习参考。

---

*Kumo OS — built with Claude Code, one phase at a time.*
