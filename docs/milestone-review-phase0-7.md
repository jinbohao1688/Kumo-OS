# Kumo OS 阶段 0-7 里程碑回顾

**日期**：2026-07-11
**范围**：阶段 0（环境搭建）至阶段 7（Shell），主线路图全部完成
**系统能力**：从 Multiboot2 引导 → 用户态交互式命令行 Shell

---

## 一、从 0 到能跑 Shell 的技术栈清单

按依赖顺序排列（上层依赖下层）：

| # | 子系统 | 解决的问题 | 当前状态 |
|---|--------|-----------|---------|
| 1 | **GDT** | CPU 保护模式下段内存访问的基础——定义内核/用户代码段、数据段的基址和权限（平坦模型，base=0，4GB limit） | 稳定。6 个描述符：null + kcode/kdata + ucode/udata + TSS |
| 2 | **IDT** | 中断/异常的路由表——CPU 根据中断向量号查表找到 handler 入口。256 项全填充，前 32 项是异常 ISR，第 0x80 项是 syscall 门（DPL=3） | 稳定 |
| 3 | **PIC (8259A)** | 硬件中断控制器重映射——将 IRQ0-15 从默认的向量 8-15（与 CPU 保留向量冲突）重映射到 32-47 | 稳定 |
| 4 | **ISR trampoline** | 汇编层中断入口/出口——pusha 保存寄存器 → 调 C handler → popa → iret。ISR_NOERR 和 ISR_ERR 两条路径（区分 CPU 是否自动压 error code） | 稳定。双路径分别用 #DE 和 #GP 验证过 |
| 5 | **IRQ0 时钟中断** | 系统心跳——PIT 周期触发 IRQ0，handler 发 EOI、输出 tick 计数。当前 ~18Hz | 稳定 |
| 6 | **Multiboot2 解析** | 从 GRUB 传递的 multiboot_info 结构体中提取物理内存 map（AVAILABLE 区域），为 PMM 提供"哪些内存可用"的输入 | 稳定 |
| 7 | **PMM（物理内存管理）** | 位图法物理页分配器——managed_base=1MB，覆盖 ~128MB。提供 pmm_alloc_page / pmm_free_page / pmm_alloc_contiguous_pages | 稳定。6 项 heap 测试中包含 200×500B 大规模分配 |
| 8 | **Paging（分页）** | 两级 4KB 页表，全物理内存恒等映射 [0, 0x07FE0000)。开启 CR0.PG 后 CPU 即进入分页模式。paging_set_user_accessible 设置 PDE.U/S + PTE.U/S | 稳定。PDE.U/S=0 导致 Ring3 #PF 的坑已记录 |
| 9 | **KHeap（内核堆）** | kmalloc/kfree——显式自由链表 + first-fit。block 不跨页（每页独立管理），forward/backward coalescing | 稳定。6 项测试全覆盖（含 heap expansion 10 次） |
| 10 | **Scheduler（调度器）** | 协作式轮转调度——task_t 环形链表，task_create 插在 current 之后，task_yield 调用 switch_to 切到 next。idle 任务复用 boot_stack | 稳定。3 任务 + idle 轮转验证通过 |
| 11 | **TSS** | 硬件任务状态段——ss0/esp0 定义 Ring3→Ring0 中断时的内核栈。LTR 加载 TSS 段选择子。task_yield 动态更新 TSS.esp0 为新任务内核栈顶 | 稳定。GDB 验证了 TSS.esp0 在任务切换时正确更新 |
| 12 | **Syscall** | int 0x80（DPL=3 门）→ pusha → 调 syscall_dispatch（C 函数，按 cdecl 传 6 个参数）→ popa → iret。返回值覆盖 saved eax | 稳定。10 个 syscall 全部可用 |
| 13 | **Ring3 切换** | task_create_user 构建组合栈帧（callee-saved regs → return_to_ring3 → iret 帧）。TSS.esp0 指向新任务内核栈顶。trampoline 设 DS→0x23 后 iret | 稳定。2 用户任务交替 yield 80+ 轮无崩溃 |
| 14 | **VFS（虚拟文件系统）** | vfs_node_t（节点 + ops 函数指针表）+ vfs_file_t（打开的文件，含 node + offset + flags）。vfs_open/read/write/close 路径解析 + fd 表管理。每任务 fd_table[16] | 稳定。单层路径 `/filename` |
| 15 | **RamFS** | 基于 VFS 的内存文件系统——ramfs_node_t 树状结构（parent → children → next 链表），文件内容 kmalloc 缓冲区 + 动态扩展 | 稳定。create/lookup/read/write/readdir 均可用 |
| 16 | **Shell** | 用户态交互式命令行——串口轮询 READCHAR（非阻塞），line editing（回显 + backspace），strcmp_word 命令匹配，5 个内建命令（help/ls/cat/echo/run） | 稳定。最小闭环验证通过 |

**依赖关系链**：

```
GDT → IDT → PIC → ISR → IRQ0
                           ↓
    Multiboot2 → PMM → Paging → KHeap → Scheduler → TSS → Syscall → Ring3
                                                                         ↓
                                                                   VFS → RamFS → Shell
```

其中关键交叉依赖：
- Paging 依赖 PMM（页表页从 PMM 分配）
- KHeap 依赖 PMM+Paging（初始页从 PMM 分配，页已恒等映射）
- Scheduler 依赖 KHeap（task_create 需要 kmalloc TCB）
- TSS+Ring3+Syscall 三者互相依赖（Ring3 需要 TSS.esp0 + syscall gate；syscall 需要 TSS 切换栈）
- VFS 依赖 KHeap（节点/文件结构 kmalloc）
- Shell 依赖全部上述组件

---

## 二、当前遗留技术债清单

| # | 编号 | 债务项 | 严重程度 | 触发条件 | 说明 |
|---|------|--------|---------|---------|------|
| 1 | ADR-003 | **NULL 页 guard** | 中 | 加载首个外部用户程序前 | 虚拟地址 0x0 仍映射到物理页 0，Ring3 空指针解引用不会触发 #PF。当前所有用户代码为内核内置 blob，不会主动解引用 NULL。修复成本极低：清除 PTE[0].P 位 + invlpg |
| 2 | ADR-004 | **copy_from/to_user** | 高 | 加载首个外部用户程序前 | 所有接受用户指针的 syscall（OPEN/READ/WRITE/READDIR/RUN/WRITECONSOLE）直接解引用用户传入的指针，无权限校验。攻击面：用户可传内核地址读写内核数据/代码。需实现逐页检查 PDE.U/S + PTE.U/S 的 copy_from/to_user |
| 3 | — | **Shell TTY 共享问题** | 低 | 多个用户任务同时运行 shell 时 | READCHAR 直接读 COM1 端口，多个任务同时调用会竞争串口输入字节。当前仅一个 shell 任务运行，风险不触发。需要引入 TTY 层（将输入路由到前台任务）或 wait-for-input 阻塞语义 |
| 4 | — | **RamFS 单层路径** | 低 | 需要嵌套目录时 | vfs_lookup 只解析 `/name` 格式，不支持 `/dir/sub/file`。需要 path_next 支持多级遍历 |
| 5 | — | **32 位无 SMEP/SMAP** | 中 | 长期不切换 64 位 | x86 32 位模式不提供硬件页权限隔离（SMEP/SMAP 是 64 位 CR4 功能）。即使实现了 copy_from_user，Ring0 代码仍然可以无意中解引用用户地址。必须靠软件纪律（code review 确保所有用户指针都走 copy_from_user） |
| 6 | — | **调度器无抢占无优先级** | 低 | 需要实时性或多用户公平调度时 | 纯协作式——任务不调用 YIELD 就能永久占用 CPU。当前所有任务都是内核编写的良性代码，不会恶意不 yield |
| 7 | — | **文件系统无持久化** | 低 | 需要重启后保留数据时 | RamFS 全部在内存中，关机即丢失。后续需要块设备驱动 + 磁盘文件系统（FAT32 或 ext2） |
| 8 | — | **无用户程序加载机制** | 中 | 阶段 8 | 所有用户代码通过 `xxd -i` 嵌入内核二进制。阶段 8 需实现 ELF32 加载器，支持从 RamFS 文件加载程序 |

---

## 三、阶段 8 ELF 加载器复杂度预估

**结论：中等复杂度（6/10），风险中等偏高（7/10）。不是继分页和 Ring3 之后的第三个高风险阶段，但比 VFS/RamFS 困难一个量级。**

### 低风险部分（约 60% 工作量）

- ELF32 格式解析本身不复杂——读取 ELF header → 遍历 program headers → 找到 PT_LOAD 段 → 分配物理页 + 映射用户页 + 拷贝段数据 + 清零 BSS。标准流程，Intel/OSDev 文档充分覆盖。大约 200-300 行 C。
- 已经有一个能正确工作的 task_create_user——它负责构建 Ring3 栈帧、分配内核栈/用户栈。ELF loader 只需要在调 task_create_user 之前把代码和数据放到正确的内存位置即可，Ring3 进入机制不需要改动。

### 高风险部分（约 40% 工作量）

1. **ADR-004（copy_from_user）必须优先实现**——这是 ELF loader 的前置条件。ELF loader 本身不需要 copy_from_user（它从内核读取文件），但一旦系统能加载外部用户程序，用户就可以向内核传任意指针。这不再是"可以推迟"的技术债。约 100-150 行 C（页表遍历 + 边界检查）。
2. **ADR-003（NULL 页 unmap）**——同样应该在首个外部程序加载前完成。成本极低（清一个 PTE 位）。
3. **页级内存布局**——ELF 的 PT_LOAD 段有自己的虚拟地址（p_vaddr），可能与当前"代码固定加载到约定地址"的方案冲突。需要决定：是尊重 ELF 的 p_vaddr 还是重定位。最简方案：只支持位置无关的 ELF（编译时 -fPIE），加载到任意页然后跳转。这避免了地址空间布局的复杂性。
4. **用户栈传参**——需要将 argc、argv 放到用户栈上，让 crt0 或 _start 能读取。这是纯寄存器→栈内存的布局工作，容易出错但容易 GDB 验证。

### 与历史阶段的难度对比

| 阶段 | 复杂度 | 风险 | 不可调试性 | 说明 |
|------|--------|------|-----------|------|
| Phase 3（分页） | 8/10 | 9/10 | **极高** — 一个 PTE 位错了就 triple fault，无串口输出 | 启用了 CPU 的一个全新模式 |
| Phase 5（Ring3） | 7/10 | 8/10 | **高** — TSS.esp0 时机错误导致栈踩踏，很难回溯 | 跨越了特权级边界 |
| **Phase 8（ELF）** | **6/10** | **7/10** | **中等** — 可以在加载前打印所有段信息，跳到用户代码前做最终检查 | 在上层做数据搬运，不改 CPU 模式 |
| Phase 4（调度器） | 5/10 | 5/10 | 中 — 栈布局错误可通过 GDB x/5xw 验证 | |
| Phase 6（VFS+RamFS） | 4/10 | 3/10 | 低 — 内存数据结构，可打印状态 | |

Phase 8 的风险主要不在 ELF 格式本身，而在于 (a) 它是"第一个外部代码跑在系统里"的节点，必须在此之前清偿 ADR-003 和 ADR-004 两笔安全债；(b) 内存布局错误可能导致神秘的用户态 crash，虽不像 triple fault 那样完全黑盒，但错误信息有限。好在所有加载逻辑都在内核中，可以用串口打印每一步的状态（ELF header 信息、段加载地址、entry point），验证闭环比 Phase 3/5 容易得多。

### 建议分步策略

**阶段 8a：清偿技术债（防御性改动，不改变用户可见行为）**
1. ADR-003：unmap NULL 页（清除 PTE[0].P + invlpg）
2. ADR-004：实现 copy_from_user / copy_to_user（页表 U/S 位遍历 + 逐页校验 + memcpy）

**阶段 8b：ELF 加载器（新增功能）**
1. ELF32 header 解析 + program header 遍历
2. PT_LOAD 段加载（物理页分配 → 用户页映射 → 段内容拷贝 → BSS 清零）
3. 用户栈构建（argc/argv 压栈）
4. task_create_user 创建新任务，entry = ELF entry point

分步的好处：如果 8b 引入 bug，至少能确定不是安全校验层的问题。

---

## 四、关键踩坑经验总结

以下是从阶段 0 到 7 反复出现的 bug 模式，具有跨阶段的迁移价值：

1. **栈布局偏移量错位**（阶段 2/4/5）：汇编压栈 + C 结构体解析时，push/pop 顺序、字段偏移量、结构体大小三者必须完全一致。不要相信"看起来顺序一样"——分别找一条触发路径来实测验证（经验-001）。

2. **寄存器复用导致自我覆盖**（阶段 7）：x86 汇编中一个寄存器不能同时承担"地址指针"和"临时数据"两种角色。`mov dl, [edx + ecx]` 之后 edx 就不再是原来的指针。写汇编代码时先在注释里标注每个寄存器的角色，确认没有角色冲突。

3. **页表层级权限**（阶段 5）：PDE 和 PTE 的 U/S 位必须同时设置。CPU 在 page walk 时先检查 PDE，PDE.U/S=0 会拒绝整个 4MB 区域的所有 Ring3 访问，无视 PTE.U/S 的值。

4. **CPU 模式切换点的验证闭环**（阶段 3/5）：每次启用新的 CPU 模式（分页的 CR0.PG、Ring3 的 DPL=3 iret），必须在切换前后各设一个 GDB 断点，单步跨过切换指令，确认关键寄存器（CR0/CR3/CS/SS:ESP/TSS.esp0）的值符合预期。不要依赖"系统没 crash"来推断正确性。

5. **QEMU 环境差异**（阶段 1/7）：`-nographic` vs `-serial stdio`、终端 vs 管道——这些环境差异可能在特定条件下暴露非内核 bug。自动化测试脚本应考虑 pipe vs terminal、`-nographic` vs `-serial stdio` 的组合。

---

## 五、当前系统量化数据

| 指标 | 数值 |
|------|------|
| 源文件总数 | ~25（不含 build 产物和 docs） |
| 汇编文件 | 8（boot + isr_stub + isr + irq + switch + syscall + ring3 + user/shell + user/test_ramfs） |
| C 文件 | 14（main + serial + gdt + idt + exception + pic + irq_handler + multiboot + pmm + paging + kheap + task + tss + syscall_dispatch + vfs + ramfs） |
| 内核 ELF 大小 | ~50KB |
| Shell 二进制 | ~1.6KB |
| Syscall 数量 | 10（PRINT/YIELD/OPEN/READ/WRITE/CLOSE/READCHAR/READDIR/RUN/WRITECONSOLE） |
| 物理内存 | 128MB（QEMU 默认） |
| 可用物理页 | ~32,000 页（~127MB） |
| 内核堆初始大小 | 16 页（64KB），按需扩展 |
| ADR 数量 | 4（003 和 004 待清偿） |
| Bug 记录 | 8 条（phase-notes.md） |
