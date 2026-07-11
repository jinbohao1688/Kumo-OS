# Kumo OS 里程碑回顾：阶段 0-8

**日期**：2026-07-11  
**范围**：从零开始到 ELF 外部程序加载执行

---

## 总览

Kumo OS 是一个从零构建的 x86 32 位教学操作系统内核。截至阶段 8，已实现一个功能完整的操作系统内核基础链路：

```
引导 → 中断/异常 → 内存管理(PMM+分页+堆) → 多任务调度
→ Ring3 用户态 → syscall(11个) → VFS+RamFS → Shell → ELF 加载器
```

## 各阶段成果

| 阶段 | 主题 | 核心成果 | 验证方式 |
|------|------|---------|---------|
| 0 | 环境搭建 | 交叉编译工具链、仓库骨架 | GCC 编译 hello world |
| 1 | 启动+输出 | Multiboot2 引导、串口驱动 | "Kumo OS booted." |
| 2 | 中断异常框架 | GDT/IDT/PIC/IRQ0 时钟 | 5秒 99+ tick 无崩溃 |
| 3 | 内存管理 | PMM 位图、恒等分页、内核堆 | 6 项堆测试全通过 |
| 4 | 多任务协作调度 | TCB 环形链表、switch_to | 4 任务 80+ 轮转 |
| 5 | Ring0→Ring3 | TSS.esp0 切换、iret 特权级 | 2 用户任务交替 80+ 轮 |
| 6 | 文件系统 | VFS 抽象层、RamFS | open→write→read→close |
| 7 | Shell | 5 个内建命令、行编辑 | help/ls/cat/echo/run |
| 8a | 技术债清偿 | NULL 页 unmap、用户指针校验 | 4 组测试全通过 |
| 8b | ELF 加载器 | 解析→映射→栈构造→执行 | exec /hello.elf → "Hello from ELF!" |

## 内核架构

### 内存布局（128MB QEMU VM）

```
0x00000000 ─── 低 1MB（恒等映射，PMM 不管）
0x00100000 ─── 内核加载点（.text/.rodata/.data/.bss ~75KB）
              PMM bitmap (1 page)
              Page Directory (1 page) + Page Tables (~32 pages)
              KHeap 初始 64KB (16 pages, 连续分配)
              ... 自由物理页 (~126MB) ...
0x07FE0000 ─── 物理内存顶部
```

### 关键架构决策

| ADR | 决策 | 理由 |
|-----|------|------|
| ADR-001 | 32 位保护模式起步 | 降低初始复杂度，64 位长模式进入过程本身是不小的复杂度 |
| ADR-003 | NULL 页 unmap | 用户态上线后清除 PTE[0].P，空指针解引用触发 #PF |
| ADR-004 | copy_from/to_user | 所有 6 个指针接受型 syscall 接入用户态指针校验 |
| 阶段 8b | PIE-only ELF 加载 | 共享 PD 架构下，非 PIE 程序 vaddr 冲突不可避免 |
| 阶段 8b | 手工 NASM PIC | 零重定位，ELF 格式仅作容器，EIP discovery 用 call/pop |

### syscall 表（11 个）

| # | 名称 | 功能 |
|---|------|------|
| 1 | PRINT | 调试打印（废弃，被 WRITECONSOLE 取代） |
| 2 | YIELD | 协作式让出 CPU |
| 3 | OPEN | 打开文件（路径 + 标志） |
| 4 | READ | 读取文件到用户缓冲区 |
| 5 | WRITE | 从用户缓冲区写入文件 |
| 6 | CLOSE | 关闭文件描述符 |
| 7 | READCHAR | 非阻塞串口读取（-1 = 无数据） |
| 8 | READDIR | 目录遍历 |
| 9 | RUN | 启动注册的内置测试程序 |
| 10 | WRITECONSOLE | 原始串口输出（无 LF 转换） |
| 11 | EXEC | 从 VFS 加载 ELF 并执行 |

### 文件系统层次

```
VFS (vfs_node_t + vfs_file_t + fd 表)
 └── RamFS (树状结构，kmalloc 缓冲区)
      └── 预加载: /hello.elf
```

### 任务模型

- **调度**：协作式轮转（环形链表，task_yield）
- **TCB**：esp(offset 0) / id / state / next / kernel_stack_top / fd_table[16]
- **用户任务**：独立内核栈(2页) + 用户栈(1页)，组合栈帧 switch_to → return_to_ring3 → iret
- **idle 任务**：复用 boot_stack，无 Ring3 栈切换

## 关键数字

| 指标 | 数值 |
|------|------|
| 内核 ELF 大小 | ~75KB |
| PMM 管理物理页 | 32,480 页 (~127MB) |
| 页表开销 | 33 页 (~132KB) |
| KHeap 初始大小 | 64KB (16 页连续) |
| syscall 数量 | 11 |
| Shell 大小 | ~1.9KB (汇编) |
| ELF 加载器大小 | ~386 行 C |
| 源代码文件数 | ~35 |
| 最大并发用户任务 | 3 (验证过) |

## 已知限制与技术债

1. **kheap 跨页 coalescing**：coalesce_forward/backward 限在单页边界内，大块分配可能无法合并碎片。当前 exec 路径绕过 heap 直接用 PMM
2. **共享页目录**：所有任务共享一个 PD，无进程隔离。后续需要 per-process 页表
3. **协作式调度**：无抢占、无优先级、无睡眠/等待队列
4. **RamFS 仅单层目录**：不支持嵌套路径（`/a/b/c`）
5. **无磁盘驱动**：文件系统完全在内存中，重启丢失
6. **无用户程序编译工具链**：用户程序需手写 NASM 汇编，不支持 C 用户程序
7. **32 位限制**：不支持 >4GB 物理内存、不支持 64 位用户程序

## 下一阶段方向

用户可选的继续方向：

1. **驱动扩展**（阶段 7 原路线图）：
   - 真实磁盘驱动（ATA PIO → AHCI）
   - FAT32 文件系统
   - 键盘 IRQ 驱动（IRQ1 键盘中断 + 扫描码解析）
   - 从磁盘加载并执行程序

2. **进程模型完善**：
   - Per-process 页表（CR3 切换）
   - 抢占式调度（PIT 定时器中断触发）
   - 进程睡眠/等待/退出

3. **用户态扩展**：
   - C 用户程序支持（交叉编译 + C runtime）
   - 更多 syscall（fork/execve/wait/exit）

4. **64 位迁移**：
   - PAE → 长模式
   - x86_64 交叉编译工具链
   - SMEP/SMAP 硬件安全特性

## 文档体系

```
docs/
  decisions.md                    — 架构决策记录（ADR-001~004 + 经验记录）
  phase-notes.md                  — 各阶段踩坑记录与可迁移经验
  PROGRESS.md                     — 开发进度（按阶段 checklist）
  milestone-review-phase0-8.md   — 本文档
```

## 统计

- **开发周期**：2 天（2026-07-10 ~ 2026-07-11）
- **阶段数**：9 个（0~8，其中 8a 和 8b 为一个阶段的子部分）
- **commit 数**：~20
- **发现并修复的 bug**：4 个（strcmp_word 寄存器复用、dispatch 编辑残留、strcmp_word 比较顺序、kheap 页面碎片化）
- **环境踩坑**：3 个（QEMU -d int 调试、gdb-multiarch 替代 i686-elf-gdb、QEMU serial stdio 管道首字节丢失）
