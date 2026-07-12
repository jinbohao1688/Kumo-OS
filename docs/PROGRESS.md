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

## 阶段 3：内存管理 ✓ (2026-07-11)

### 物理地址内存布局（128MB QEMU VM，当前实测值）

```
0x00000000 ─────────────────────── 低 1MB（PMM 不管，恒等映射了）
0x00100000 ┌────────────────────── managed_base（内核加载点）
           │ 内核 ELF ~50KB
           │   .multiboot / .text / .rodata / .data / .bss (含 16KB 启动栈)
0x00109000 ├── _kernel_end
           │  [padding < 4KB]
0x0010A000 ├── PMM bitmap  1 page  ~4KB   (管理 32,480 页)
0x0010B000 ├── first_free  (PMM sanity alloc → 从此开始)
0x0010C000 ├── Page Directory  1 page
0x0010D000 ├── Page Table [0..31]  32 pages  覆盖 [0, 0x08000000)
0x0012D000 ├── KHeap 初始 16 pages  ~64KB...
           │   (按需扩展，实测 200×500B 触发 10 次扩展至 0x147000)
           │  ... 大量自由物理页 (~126MB) ...
0x07FE0000 └────────────────────── top_of_memory (0x07FE0000)
```

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

## 阶段 4：多任务协作调度 ✓ (2026-07-11)

- [x] TCB 结构体（`sched/task.h`，esp 在偏移 0，id/state/next 共 16 字节）
- [x] 上下文切换（`sched/switch.asm`，纯 NASM，push ebp/edi/esi/ebx → pop 对称）
- [x] 调度器（`sched/task.c`：task_init/task_create/task_yield，环形链表轮转）
  - idle 任务复用 boot_stack（16KB .bss），esp=0 哨兵在首次 yield 时填充
  - 新任务内核栈：2 页 8KB 连续物理内存（pmm_alloc_contiguous_pages）
  - task_create 插入环形链表（insert-after-current），g_next_id 顺序编号
- [x] 初始栈布局 GDB 核对（5 字段 x/5xw，与偏移表 100% 一致，无 order-reversal bug）
- [x] 3 测试任务 + idle 循环验证（80+ tick 无崩溃，C→B→A→idle 轮转稳定）
- [x] `pmm_alloc_contiguous_pages(count)` 新增（连续物理页分配，扫描位图找连续空闲位）
  - 后续阶段 5（用户态栈）、阶段 7（DMA 缓冲区）等需要连续物理内存的场景可复用
- [x] **阶段 4 完成**：协作式轮转调度（环形链表 + switch_to + 独立内核栈）

## 阶段 5：Ring0→Ring3 切换 + syscall ✓ (2026-07-11)

### 5a — 核心机制 ✓ (2026-07-11)

- [x] GDT 扩展至 6 项（null + kcode/kdata + ucode/udata + TSS）
  - User code: DPL=3, access=0xFA; User data: DPL=3, access=0xF2
  - TSS: 系统段描述符 (access=0x89), selector=0x28
- [x] TSS 初始化（`arch/x86/tss.c` — ss0/esp0 + LTR）
- [x] 页面用户态权限（`paging_set_user_accessible()` — PDE.U/S + PTE.U/S + invlpg）
- [x] Ring3 入口（`arch/x86/ring3.asm` — DS/ES/FS/GS→0x23 → iret 帧 → iret）
- [x] int 0x80 syscall handler（`arch/x86/syscall.asm` + `syscall_dispatch.c`）
  - 入口 DS/ES/FS/GS→0x10，出口→0x23，对称切换
- [x] **坑：PDE.U/S=0 导致 Ring3 指令取指 #PF**
- [x] 6 步 GDB 验证全通过（最小闭环，单个用户任务，全局共享内核栈）
- [x] 20+ 轮 syscall 循环无崩溃

### 5b — 多任务 + TSS.esp0 动态切换 ✓ (2026-07-11)

- [x] TCB 增加 kernel_stack_top 字段（offset 0x10，`sched/task.h`）
- [x] task_yield 中更新 TSS.esp0 → next->kernel_stack_top（仅非零值，跳过 idle）
- [x] task_create_user() 创建独立内核栈的 Ring3 任务
  - 2-page 内核栈（pmm_alloc_contiguous_pages(2)）+ 1-page 用户栈（pmm_alloc_page + mark user-accessible）
  - 组合栈帧：callee-saved regs → return_to_ring3 → iret 帧（EIP/CS/EFLAGS/ESP/SS）
- [x] return_to_ring3 trampoline（`arch/x86/ring3.asm` — DS/ES/FS/GS→0x23 → iret）
- [x] SYSCALL_YIELD=2：用户态任务通过 syscall 主动让出 CPU
  - syscall_dispatch 调用 task_yield()，task_yield 更新 TSS.esp0 后 switch_to
- [x] 2 用户任务 + idle 三轮转验证（A(0x41)/B(0x42) 交替打印，8 秒 80+ 轮无崩溃）
- [x] GDB 验证：4 次 task_yield 断点，eax 与 g_tss.esp0 交替 0x14C000↔0x14F000，确认每次 switch_to 前 TSS.esp0 正确指向新任务内核栈顶
- [x] **阶段 5 完成**：多任务 Ring0↔Ring3 完整闭环

## 阶段 6：文件系统 + 系统调用（ramfs）✓ (2026-07-11)

- [x] VFS 抽象层（`fs/vfs.h` + `fs/vfs.c`）
  - vfs_node_t（文件/目录节点，含 ops 函数指针表）
  - vfs_file_t（打开文件描述，含 node + offset + flags）
  - vfs_open/read/write/close — 路径解析 + fd 管理
  - vfs_mount — 挂载文件系统根节点
- [x] RamFS 实现（`fs/ramfs.h` + `fs/ramfs.c`）
  - 树状结构（parent/children/next 链表）
  - 文件内容存储在 kmalloc 缓冲区，write 时动态扩展
  - 单层目录支持（`/filename`，尚不支持嵌套路径）
- [x] 文件描述符集成到 TCB
  - TCB 新增 `fd_table[16]`，task_init/task_create/task_create_user 全部清零
  - vfs_open 扫描空闲槽位，vfs_close 释放
- [x] 新增 4 个 syscall：SYSCALL_OPEN(3)/READ(4)/WRITE(5)/CLOSE(6)
  - 参数传递：open(path,flags), read/write(fd,buf,size), close(fd)
  - 所有接受用户指针的 syscall 已标记 `FIXME: validate user buffer`
- [x] ADR-004：用户态指针校验记作已知技术债，待加载外部程序前实现 copy_from/to_user
- [x] 最小闭环验证：open → write("hello") → close → open → read(5) → 输出 "hello"+bytes_read=5

### 阶段 6 文件清单

```
fs/
  vfs.h         — VFS 接口定义（vfs_node_t, vfs_file_t, vfs_node_ops_t）
  vfs.c         — VFS 实现（路径解析, fd 表管理, open/read/write/close）
  ramfs.h       — RamFS 内部结构（ramfs_node_t）
  ramfs.c       — RamFS 实现（create, lookup, read, write — 内嵌 ops 回调）
user/
  test_ramfs.asm — 用户态测试程序（call/pop 位置无关，271 字节）
```

## 阶段 7：Shell + 用户态命令 ✓ (2026-07-11)

- [x] Shell 用户态程序（`user/shell.asm`，1604 字节，位置无关）
  - 串口轮询输入（SYSCALL_READCHAR，非阻塞，无数据时 YIELD）
  - 行编辑：回显 + backspace 处理（"\b \b" 序列）
  - 行缓冲（128 字节），Enter 执行，跳过前导空格
  - strcmp_word 命令匹配（word = 空格/NUL 分隔，前缀匹配拒绝——"helpx" 不匹配 "help"）
- [x] 5 个内建命令
  - `help` — 显示可用命令列表
  - `ls` — READDIR 遍历根目录子节点
  - `cat <file>` — open→read(128B chunks)→print→close
  - `echo <text>` — 打印参数文本
  - `run <name>` — 通过 SYSCALL_RUN 在 run_registry 中查找并启动注册的测试程序
- [x] 新增 4 个 syscall
  - SYSCALL_READCHAR(7) — serial_poll_char 封装，非阻塞，-1=无数据
  - SYSCALL_READDIR(8) — vfs_readdir 封装
  - SYSCALL_RUN(9) — run_exec 封装，查找 run_registry
  - SYSCALL_WRITECONSOLE(10) — 逐字节 serial_putchar，无 LF→CR+LF 转换
- [x] Run Registry 基础设施（`kernel/main.c`）
  - 注册表 g_run_registry[8]，run_register(name, code_page)
  - run_exec 按 name 查找并调用 task_create_user 创建新任务
  - ramfs_test 已注册但未自动运行（需手动 `run ramfs_test`）
- [x] 新增内核函数
  - `serial_putchar(char c)` — 原始字节输出（无 CR+LF 转换）
  - `serial_poll_char(void)` — 非阻塞轮询 COM1 输入
  - `vfs_readdir(path, index, name_buf)` — VFS 层 readdir 封装
- [x] **Bug 修复：strcmp_word 寄存器复用导致指针自我覆盖**
  - `mov dl, [edx + ecx]` 覆盖 edx 低字节，后续迭代地址计算错误
  - 修复：命令字符串指针改用 esi，dl 仅做临时数据寄存器
  - 记入 phase-notes.md 作为"寄存器不能同时承担指针和临时数据两种角色"的可迁移经验
- [x] **Bug 修复：dispatch 段编辑残留**
  - 调试代码恢复时漏掉 `call cmd_help_handler; jmp main_loop`，导致 help 命令 fall-through 到 .try_ls
  - 修复：补回 handler 调用，恢复完整控制流
- [x] **环境踩坑：QEMU `-serial stdio` 管道输入首字节丢失**
  - stdin 非终端时 QEMU 内部初始化消耗首字节，非内核 bug
  - 规避：sleep 3 等待 QEMU 就绪后再发送数据
- [x] 最小闭环验证通过：
  - 提示符 `# ` 出现
  - 输入回显（h→e→l→p 逐字输出）
  - `help` → 显示全部 5 个命令和描述
  - `ls` → 显示 `(empty)`（根目录无文件）
  - tick 持续运行，多任务调度正常
- [x] **阶段 7 完成**：用户态 Shell 完整可用，syscall 数量达到 10 个

**阶段 0-7 主线路图全部完成** 🎉

### 阶段 7 文件清单

```
user/
  shell.asm          — Shell 用户态程序（1604 字节，位置无关）
新增/修改的内核文件：
  drivers/serial.c   — +serial_putchar(), +serial_poll_char()
  drivers/serial.h   — 声明上述两个函数
  fs/vfs.c           — +vfs_readdir()
  arch/x86/syscall.h — +SYSCALL_READCHAR(7)/READDIR(8)/RUN(9)/WRITECONSOLE(10)
  arch/x86/syscall_dispatch.c — +4 个新 syscall case
  kernel/main.c      — +run_registry, run_register(), run_exec()
build/
  shell.h            — shell.asm 编译产物（xxd -i 生成 C 数组）

## 阶段 8a：清偿技术债（ADR-003 + ADR-004）✓ (2026-07-11)

### ADR-003 — NULL 页 unmap

- [x] `paging_unmap_null_page()` — 清除 PTE[0].P + invlpg on 0x0
- [x] 接入 kmain：paging_init() → paging_unmap_null_page() → kheap_init()
- [x] 验证：`test_null` 用户程序解引用 NULL 触发 #PF（Vector 0x0E, ErrCode 0x04）

### ADR-004 — copy_from/to_user 用户态指针校验

- [x] `page_is_user_accessible(vaddr)` — 内部函数，检查 PDE.U/S + PTE.U/S + PTE.P
- [x] `is_user_accessible_range(vaddr, size)` — 遍历区间内所有页
- [x] `copy_from_user(kernel_dst, user_src, size)` — 预验证 + memcpy
- [x] `copy_to_user(user_dst, kernel_src, size)` — 预验证 + memcpy
- [x] `copy_from_user_string(kbuf, user_ptr, max_len)` — 逐字节扫描，仅在跨越
  4KB 页边界时验证新页，遇到 NUL 即停止

### 6 个指针接受型 syscall 全部接入

- [x] SYSCALL_OPEN — copy_from_user_string(path)
- [x] SYSCALL_READ — kmalloc 内核缓冲区 → vfs_read → copy_to_user(user_buf)
- [x] SYSCALL_WRITE — copy_from_user(user_buf) → vfs_write
- [x] SYSCALL_READDIR — copy_from_user_string(path) + copy_to_user(name_buf)
- [x] SYSCALL_RUN — copy_from_user_string(name)
- [x] SYSCALL_WRITECONSOLE — copy_from_user(buf) → serial_putchar 逐字节输出

### 新增测试程序（3 个）

- [x] `user/test_null.asm` — NULL 解引用 → #PF
- [x] `user/test_bad_ptr.asm` — 传入内核地址 0x100000 给 OPEN，应被拦截返回 -1
- [x] `user/test_boundary.asm` — 路径 "/x" 放在页面偏移 0xFFD，逐字节扫描应成功
- [x] 四组测试全部通过（回归/恶意指针拦截/NULL #PF/页边界字符串）

### Bug 修复

- [x] **shell.asm: strcmp_word 比较顺序** — `cmp dl, 0; je .check_end` 移到
  `cmp al, dl; jne .no_match` 之前。旧代码在命令字符串结束时（dl=NUL），
  若用户输入后跟空格，会直接跳到 .no_match 而不会检查空格是否合法。
  阶段 7 只测过 help/ls 两个无参数命令，此 bug 直到阶段 8a 才暴露。

### 阶段 8a 文件清单

```
arch/x86/
  paging.h           — +5 个新函数声明
  paging.c           — +6 个新函数实现
  syscall_dispatch.c — 6 个 syscall case 全部改写为校验模式
user/
  test_null.asm      — NULL 解引用测试
  test_bad_ptr.asm   — 恶意指针拦截测试
  test_boundary.asm  — 页边界字符串测试
kernel/
  main.c             — +paging_unmap_null_page() 调用, +3 个测试程序注册
docs/
  decisions.md       — ADR-003/ADR-004 标记已实现
  phase-notes.md     — +Phase 8a 踩坑记录（strcmp_word 比较顺序）

## 阶段 8b：ELF32 加载器 ✓ (2026-07-11)

### 架构决策

- **PIE-only 方案**（非简化选择，而是共享页目录架构下的必然）：
  - 所有任务共享同一 Page Directory（TCB 无 CR3 字段）
  - 两个非 PIE 程序若 vaddr 重叠，内存映射必然冲突
  - 因此程序必须用 position-independent 方式加载
- **选项 2：手工 NASM PIC，零重定位**：
  - 不使用 .rela.dyn 重定位表（省略 ELF 中最复杂的部分）
  - 用户程序通过 `call/pop ebp; sub ebp, get_eip` 做 EIP discovery
  - 一切数据寻址通过 `[ebp + offset]` PC-relative
  - ELF 格式仅作为容器（封装代码段），不依赖重定位机制

### Step 1: ELF header 解析

- [x] `elf_parse_and_print()` — 解析 ELF header + program headers
  - 验证 ELF magic (0x7F E L F)、32-bit、LE
  - 打印全部 19 个 ELF header 字段 + 每个 program header 的 8 个字段
  - 与 `i686-elf-readelf -h -l` 输出逐字段比对，100% 一致

### Step 2: ELF 内存映射

- [x] `elf_load()` — 将 ELF 加载到内存
  - Pass 1: 扫描 PT_LOAD segments，找到最小 vaddr 和最大 end
  - `pmm_alloc_contiguous_pages()` 分配连续物理页
  - `load_base = base_phys - min_vaddr`（恒等映射下的加载基址）
  - `paging_set_user_accessible()` 标记所有页为 Ring3 可访问
  - Pass 2: 从文件拷贝 segment 数据，zero BSS（memsz > filesz）
  - 返回 `load_base + e_entry`（通用公式，不特殊处理 e_entry=0）
  - 验证：hexdump 与 `xxd` 输出逐字节比对，0x37 字节全部一致

### Step 3: 用户栈构造（System V ABI argc/argv）

- [x] `elf_setup_user_stack(prog_name)` — 构造符合 System V ABI 的用户栈
  - 分配用户栈页面 → `paging_set_user_accessible()`
  - 自顶向下构造：字符串数据 → 对齐 → 指针数组 → argc
  - 布局：`[argc=1][argv[0]=ptr_to_name][argv NULL][envp NULL]...[name_string]`
  - 验证：GDB dump 6 dwords，确认所有 5 个字段正确

### Step 4: task_create_user 集成 + Ring3 执行

- [x] `task_create_user()` 增加 `user_esp` 参数
  - `user_esp == 0`：内部分配新用户栈（兼容 shell/test 程序）
  - `user_esp != 0`：使用预构造的 ELF 栈（Step 3 返回值）
- [x] 内核入口串口 dump 组合栈帧 10 dwords，确认 EIP/CS/EFLAGS/ESP/SS 全部正确
- [x] 验证：`Hello from ELF!` 出现在串口输出（argv[0]="hello_elf"）

### Step 5: Shell exec 命令接入

- [x] `exec_elf_from_path(path)` — 从 VFS 加载 ELF 的完整流程
  - vfs_open → vfs_read 到 PMM 缓冲区 → elf_load → elf_setup_user_stack → task_create_user
- [x] `/hello.elf` 预加载到 ramfs（vfs_open + vfs_write，在 task_init 之后以使用 fd 表）
- [x] SYSCALL_EXEC(11) — 用户态 exec 调用入口
- [x] exec 参数解析支持绝对路径和相对路径（自动补齐 "/"）
- [x] 验证：shell 敲入 `exec /hello.elf` → `started` → `Hello from ELF!`
  - argv[0]="/hello.elf"（exec 路径）vs argv[0]="hello_elf"（Step 4 硬编码）— 区分两条执行路径
  - tick 时序：Step 4 输出在 tick 1 之前，exec 输出在 tick 0x2D 之后

### kheap 修复

- [x] **kheap_init 改用 `pmm_alloc_contiguous_pages`**：初始 16 页作为一个 64KB 大块
  - 旧方案：每页单独插入 free list（单页最大 4KB block）
  - 新方案：连续页作为单个大块（可服务 >4KB 的分配）
- [x] **heap_expand 改为 4 页一扩**：每次扩展 16KB 连续页
  - 旧方案：每次扩 1 页 = 4KB（对大块分配无用）
  - 新方案：每次扩 4 页 = 16KB（可服务中等大小的分配）
- [x] **exec 路径 ELF 缓冲区改用 PMM**：`pmm_alloc_contiguous_pages(4)` 代替 `kmalloc(16KB)`
  - 原因：heap 的 coalesce_forward/backward 限在单页边界内，跨页碎片化后大块分配可能不可达
  - 这是临时方案，长期应修复 heap 跨页 coalescing 或切换到 buddy allocator

### 阶段 8b 文件清单

```
fs/
  elf.h             — ELF32 结构体定义 + 3 个函数声明（386 行）
  elf.c             — ELF 解析器 + 加载器 + 用户栈构造器
user/
  hello_elf.asm     — 最小 ELF 测试程序（位置无关，"Hello from ELF!"）
  elf_i386.ld       — ELF32 链接脚本（vaddr 从 0x0 开始，作为 PIE 的相对基址）
arch/x86/
  syscall.h         — +SYSCALL_EXEC(11)
  syscall_dispatch.c — +SYSCALL_EXEC case
sched/
  task.h            — task_create_user 增加 user_esp 参数
  task.c            — task_create_user 支持预构造用户栈
kernel/
  main.c            — +exec_elf_from_path(), +pre-load /hello.elf, +ELF task create
mm/
  kheap.c           — kheap_init 连续页分配 + heap_expand 4 页/次
user/
  shell.asm         — +exec 命令（参数解析 + SYSCALL_EXEC）+ 更新 help 文本
```

**阶段 8a+8b 全部完成** 🎉

**阶段 0-8 完整功能链**：引导 → 中断/异常 → 内存管理(PMM+分页+堆) → 多任务调度 → Ring3 用户态 → syscall → VFS+RamFS → Shell → ELF 加载器执行外部程序

## 阶段 9：显卡驱动基础（Framebuffer）第一步 ✓ (2026-07-11)

### Multiboot2 Framebuffer tag 解析

- [x] 请求标签（`boot/multiboot_header.asm`）：type=5, width/height/depth=0（GRUB 自选最佳模式）
- [x] 响应标签（`mm/multiboot.c`）：type=8 解析，含 framebuffer_addr/pitch/width/height/bpp/type
- [x] color_info 字段完整解析：red/green/blue 的 field_position + mask_size
  - QEMU `-vga std` 32bpp 下确认 BGRA 布局：red pos=16, green pos=8, blue pos=0, 各 8 bit mask
- [x] **Bug 修复：framebuffer_bpp 字段类型错误**
  - 初版将 `framebuffer_bpp` 写成 `uint32_t`（4 字节），实际 GRUB 源码中为 `uint8_t`（1 字节）
  - 导致后续所有 color_info 字段整体偏移 3 字节（red_field_position 读到 green_mask_size，依此类推）
  - 修正为 `uint8_t` + `__attribute__((packed))`，六项 color_info 值与 QEMU 预期完全吻合

### 跨范围物理内存映射

- [x] `paging_map_phys_range(phys_addr, size)` — 扩展恒等映射覆盖 top_of_memory 之外的 MMIO 区域
  - framebuffer 物理地址 0xFD000000 远在 128MB RAM 之上，不在初始 paging_init 覆盖范围
  - 按需分配新页表（PDE on demand），PTE 逐页恒等映射（phys | P | RW，仅内核访问）
  - 1024×768×32bpp = 3MB 精确映射，边界外 PTE 确认未映射
- [x] GDB 验证：
  - PDE[0x3F4] = 0x00150003 → PT phys=0x00150000, P=1, RW=1
  - PTE[0] = 0xFD000003, PTE[1] = 0xFD001003 → 连续恒等映射
  - PTE[768] = 0x00000000 → 帧缓冲范围外未映射，边界精确

### 最小视觉验证

- [x] `framebuffer_fill_solid(color)` — 逐行填充（尊重 pitch），32bpp 下工作
- [x] 纯色 0x00335588 填充 1024×768，QEMU GUI 窗口显示深蓝灰色背景
- [x] QEMU 命令切换：`-nographic` → `-serial stdio -vga std`（串口调试 + GUI 图形双通道）

### 阶段 9 第一步文件清单

```
boot/
  multiboot_header.asm  — +framebuffer 请求标签 (type=5, 20 bytes)
mm/
  multiboot.h           — +MULTIBOOT_TAG_FRAMEBUFFER(8), +multiboot_tag_framebuffer_t packed struct, +framebuffer_t + extern
  multiboot.c           — +g_framebuffer 全局变量, +type=8 tag 解析 + color_info 串口打印
arch/x86/
  paging.h              — +paging_map_phys_range() 声明
  paging.c              — +paging_map_phys_range() 实现（按需分配 PT + 恒等映射）
kernel/
  main.c                — +framebuffer_fill_solid(), 在 paging_init 后映射 FB + 填充纯色

## 阶段 10：2D 绘图原语 + 字体渲染 ✓ (2026-07-11)

### 绘图原语

- [x] `put_pixel(x, y, packed_color)` — 所有绘图的唯一出口，边界检查（越界静默丢弃）
  - 根据 bpp 选择写入宽度（32/24/16 bit），使用 pitch 计算行偏移
- [x] `make_color(r, g, b)` — 通用位域打包，复用阶段 9 color_info 字段
  - 对 QEMU stdvga BGRA 布局（red pos=16, green pos=8, blue pos=0, 各 8-bit mask），
    产生 `0x00RRGGBB` 像素值；对其他布局同样正确
- [x] `draw_line(x0, y0, x1, y1, color)` — Bresenham 整数直线算法
  - 缓坡/陡坡自动分支，每步仅一次加法+比较
- [x] `draw_rect(x, y, w, h, color)` — 空心矩形（四条线段，复用 draw_line）
- [x] `fill_rect(x, y, w, h, color)` — 实心矩形（逐行写入，带 framebuffer 边界裁剪）

### 自制 8×16 位图字体

- [x] 95 个 ASCII 可打印字符（0x20 空格至 0x7E ~），每字符 16 bytes = 1520 bytes
- [x] 设计规范：大写/数字 5px 宽 7px 高（rows 3-9），小写 x-height 5px（rows 6-10），
  升笔至 row 3，降笔至 row 13，基线 row 12
- [x] **无第三方数据依赖**——全部字符手工设计，零授权风险
- [x] `draw_string(x, y, str, fg_color)` — 单色字符串渲染，支持换行符 \n
- [x] 不可打印字符静默跳过，不作 crash

### 验证

- [x] **程序化像素采样**：9 个采样点（背景/标题/黄填充/红描边/青斜线/灰ASCII/绿小写/
  蓝大写/橙数字）全部精确命中预期颜色
- [x] **全字符集人工目视**：字体数据渲染为 ASCII 点阵图，逐字符核对基线对齐（底线
  全部一致）、笔画连续性（无断裂）和可读性（无变形/粘连），95 字符全部通过
- [x] QEMU 图形窗口一切正常，现有功能（Shell、ELF 加载器）回归无影响

### 阶段 10 文件清单

```
gfx/
  primitives.h     — put_pixel / draw_line / draw_rect / fill_rect / make_color 声明
  primitives.c     — 上述函数实现（168 行）
  font.h           — draw_string 声明 + 字体参数宏
  font.c           — 95 字符 8×16 位图字体点阵 + draw_string 实现（232 行）
kernel/
  main.c           — 替换 Phase 9 纯色填充，改为 Phase 10 综合绘图测试
Makefile           — +gfx/primitives.o / gfx/font.o 编译规则
```

## 阶段 11：抢占式调度 ✓ (2026-07-11)

### 设计分析

- [x] IDT 门类型查证：全部 256 个向量使用中断门 (0x8E)，CPU 进入时自动清 IF
  - syscall `int 0x80` 同样使用中断门 (0xEE)，DPL=3 允许 Ring3 调用但 IF 同样清 0
  - 结论：**所有内核代码路径（syscall/IRQ/异常）天然在 IF=0 下执行**
- [x] 抢占安全性分析：唯一 IF=1 进入调度器的路径是 idle 循环（kmain `for(;;) task_yield()`）
  - 其余路径（syscall YIELD、未来 IRQ0 调度的 task_yield）均已 IF=0
  - 修复：`task_yield()` 开头加 `cli`，保护 switch_to 的 ESP 交换临界区
- [x] IRQ0 汇编入口确认：`irq.asm` 已在 `irq_common_stub` 中使用 `pushad/popad` 完整保存/恢复
  全部 8 个通用寄存器（EAX..EDI），与 ISR stub 规格一致，无需改造
- [x] switch_to 栈深度无关性确认：`ret` 指令仅无条件跳到栈顶返回地址，不关心调用链深度
  - yield 路径（4 层调用）和抢占路径（8 层调用）的恢复由 C 调用约定自然 unwind

### 代码改动

- [x] `sched/task.c:task_yield()` — 首行加 `__asm__ volatile("cli")`
  - 保护 g_current 操作、TSS.esp0 更新、switch_to ESP 交换
  - IF 恢复路径：用户任务通过 iret 恢复 IF=1，idle 循环 cli 无操作
- [x] `arch/x86/irq_handler.c` — IRQ0 在 EOI 之后调用 `task_yield()`
  - tick 计数保留，打印改为每 50 tick (~2.75s) 一次
  - EOI 在 schedule() 之前发送，PIC 可锁存下一 tick
- [x] `kernel/main.c` — `sti` 从中间位置移到 idle 循环之前
  - 原因：sti 过早执行时 `task_init()` 尚未运行，g_current==NULL，irq_handler 调用
    task_yield() 会解引用 NULL → #PF

### 寄存器保存验证

- [x] 创建 2 个专用测试程序（`user/regtest_a.asm`, `user/regtest_b.asm`）
  - Task A 标记：EBX=0xBBBB ECX=0xCCCC EDX=0xDDDD ESI=0xEEEE EDI=0xFFFF EBP=0x1111
  - Task B 标记：EBX=0x2222 ECX=0x3333 EDX=0x4444 ESI=0x5555 EDI=0x6666 EBP=0x7777
  - 每轮循环验证 6 个寄存器 + SYSCALL_YIELD 让出 CPU
- [x] 验证结果：250+ tick 运行，0 次 FAIL_A 或 FAIL_B
  - pushad/popad 在抢占路径上正确保存/恢复所有通用寄存器
  - 无跨任务寄存器值混叠

### 功能回归测试

- [x] Shell（help/ls/cat/run/exec）— 正常
- [x] ELF 加载执行（hello_elf）— "Hello from ELF!" 正常
- [x] ramfs_test（VFS open/write/read 闭环）— 正常
- [x] 串口输入（`echo help | ...`）- 正常回显和命令执行

### 阶段 11 文件清单

```
sched/
  task.c              — task_yield() 加 cli 保护
arch/x86/
  irq_handler.c       — +task.h include, +IRQ0 调用 task_yield(), +tick 降频打印
user/
  regtest_a.asm       — Task A 寄存器验证程序（6 寄存器标记 + yield 循环）
  regtest_b.asm       — Task B 寄存器验证程序（不同标记值）
kernel/
  main.c              — sti 移到 idle 循环之前, +Phase 11 regtest 任务注册和启动
Makefile              — +regtest_a/regtest_b 编译规则
```

## 阶段 11b：PS/2 鼠标驱动 ✓ (2026-07-12)

### PS/2 控制器初始化流程

- [x] Step 1: Enable auxiliary port（`outb(0x64, 0xA8)`）
- [x] Step 2: Read-modify-write controller configuration byte
  - 读 CFG（cmd 0x20）→ 置 bit1 (enable IRQ12) → 清 bit5 (enable mouse clock) → 写回（cmd 0x60）
- [x] Step 3: Wire IDT[44] (IRQ12) → `irq12_entry`
- [x] Step 4: Set Defaults（cmd 0xD4 → data 0xF6），验证 ACK=0xFA
- [x] Step 5: Enable Data Reporting（cmd 0xD4 → data 0xF4），验证 ACK=0xFA
- [x] Step 6: Set Stream Mode（cmd 0xD4 → data 0xEA），验证 ACK=0xFA
- [x] Step 7: Unmask IRQ12（slave PIC bit 4）和 IRQ2（master bit 2）
- [x] Step 8: 光标初始位置 = 屏幕中心，首次 cursor_draw()

### IRQ12 中断处理

- [x] `irq12_entry`（`arch/x86/irq.asm`）：pushad 规格，与 ISR/IRQ0 stub 完全一致
  - push dummy error code 0 + vector 44 → jmp irq_common_stub
  - irq_common_stub: pushad → call irq_handler → popad → add esp,8 → iret
- [x] `irq_handler` 在 EOI 之前分发 vector 44 到 `mouse_handle_interrupt()`
  - 注意：mouse_handle_interrupt 在 EOI **之前**调用（保证 ISR 状态一致）
  - 抢占式调度仅在 IRQ0 路径触发，IRQ12 不触发 task_yield()
- [x] IRQ1（键盘）屏蔽 + PIC ISR 清理
  - QEMU 偶尔遗留 IRQ1 ISR 置位，阻塞低优先级中断（包括 IRQ2 slave cascade）
  - 解决：irq_init 中发送 non-specific EOI 清零两个 PIC + 屏蔽 IRQ1

### 3 字节包状态机

- [x] PS/2 鼠标标准 3 字节包：byte0(flags) + byte1(dx) + byte2(dy)
  - 3 次 IRQ12 中断拼出一个完整包，byte0 bit3 必须为 1（验证同步）
- [x] `mouse_process_packet()`：
  - 丢弃首个包（init 后 PS/2 缓冲区可能有残留 0xFA ACK，误食会永久偏移包边界）
  - 前 5 个真实包串口 dump（flags/dx/dy hex，用于调试）
  - 按钮状态变化上报（L/R/M 或 none）
  - 移动事件 → cursor_restore() → 更新坐标（边界钳制）→ cursor_draw()

### 光标 save/restore 显示

- [x] `cursor_save()` — 读取光标区域 12×20 像素的背景，存入 `cursor_backup[]`
  - 使用新增的 `get_pixel(x, y)` 读取帧缓冲（32/24/16bpp 三路径，与 put_pixel 对称）
- [x] `cursor_draw()` — save → fill_rect 白色填充(11×19 内部) → draw_rect 黑色边框(12×20 外框) → 更新 cursor_old
- [x] `cursor_restore()` — 将 `cursor_backup[]` 写回旧光标位置（12×20），恢复原始背景
- [x] `mouse_drain_buf()` — sti 前清空 PS/2 输出缓冲区残留字节（最多 16 字节）

### 颜色污染 bug

- [x] **症状**：光标移动经过阶段 10 已绘制内容时，背景颜色被污染（对角线从青色变成绿色）
  - 该 bug 在早期开发迭代中发现（截图证据确认），具体修复的代码差异因会话中断丢失了上下文
  - 当前代码的 save/restore 机制逻辑上完整且对称（get_pixel ↔ put_pixel 对所有 bpp 路径一致），
    未能精确定位到是当初哪一处具体改动解决了该问题
- [x] **验证**：VNC 视觉验证通过——光标划过文字/矩形/青色对角线，背景完好无损，无颜色污染

### 初始化顺序约束（踩坑）

- [x] PS/2 初始化在 framebuffer 就绪之后（依赖 `g_framebuffer` 读取屏幕尺寸）
- [x] `mouse_drain_buf()` 必须在 `sti` 之前调用
  - 原因：Enable Data Reporting 后鼠标即开始发送数据，但 sti 前 IRQ12 无法触发 ISR，
    数据堆积在 PS/2 输出缓冲区。如果不清空，sti 后第一条 IRQ12 会读到这些旧字节，
    导致 3 字节包边界永久偏移

### 阶段 11b 文件清单

```
drivers/
  mouse.h           — mouse_init / mouse_handle_interrupt / mouse_drain_buf 声明
  mouse.c           — PS/2 初始化 8 步流程 + 3 字节包状态机 + 光标 save/restore（300 行）
arch/x86/
  irq.asm           — +irq12_entry（pushad 规格）
  irq_handler.c     — +IRQ12 分发 + IRQ1 屏蔽 + PIC ISR 清理 + inb/outb 辅助函数
gfx/
  primitives.h      — +get_pixel() 声明
  primitives.c      — +get_pixel() 实现（32/24/16bpp 三路径，与 put_pixel 对称）
kernel/
  main.c            — +mouse_init() 调用（FB 就绪后）+ mouse_drain_buf() 调用（sti 前）
Makefile            — +drivers/mouse.c 编译规则 + run-vnc/run-vnc-mon 目标
```

**阶段 11 完整收尾**：抢占式调度（11a）+ 鼠标驱动（11b）全部完成并 commit ✓

## 阶段 12：每任务独立页目录（进程隔离）✓ (2026-07-12)

### 架构设计

- **物理内存布局分离**：内核区域 [0x100000, 0x800000) vs 用户区域 [0x800000+)
  - 分界线 0x800000（8MB）为当前 128MB VM 的合理选择，预留内核扩展空间
  - PMM 双区域分配器：`pmm_alloc_page()` 从低端分配（内核），`pmm_alloc_user_page()` 从高端分配（用户）
  - `pmm_alloc_contiguous_pages()` 和 `pmm_alloc_contiguous_user_pages()` 同理
  - **为什么需要布局分离**：Phase 8b 所有任务共享同一 PD，用户页在内核 PD 中标记 U/S。
    Phase 12 改为每任务独立 PD 后，如果两个任务 A/B 的用户页物理地址相同（如都分配在
    0x10A000），A 在其私有 PD 中将该 PTE 标记为 U/S=1，B 切换到自己的 PD 时该地址
    指向完全不同的物理页——不存在冲突，因为物理页本身已通过布局分离保证了不重叠

- **每任务独立 Page Directory**：
  - `task_create_user_with_pages()` — 新 API，接收页面数组，分配私有 PD 并浅拷贝内核 PDE[0-1]
  - TCB 增加 `cr3` 字段（offset 0x14）：0 = 使用内核 PD（idle/内核任务），非零 = 私有 PD
  - 用户 PDE[2+] 初始为空，通过 `paging_set_user_accessible_for_task()` 按需标记

- **CR3 切换**（`sched/switch.asm`）：
  - 插入点：`mov esp, [eax]` 之后、`pop ebp` 之前
  - 此时新任务的 esp 已加载，但寄存器未恢复，CR3 切换不影响 pop 目标
  - 旧任务的 CR3 在 eax 中仍然可用（eax = &old_task->esp）
  - 仅当 cr3 != 0 时才执行 `mov cr3`（idle 任务仍使用内核 PD）

- **按需 PT 克隆**（`paging_set_user_accessible_for_task`）：
  - 调用时比较任务 PDE 与内核 PDE：如果两者指向同一个 PT（共享），则从 PMM 分配新 PT，
    浅拷贝 1024 个 PTE，更新任务 PDE 指向新 PT
  - 新分配的 PT 从内核区域分配（属于内核元数据，不属于用户区域）
  - 效果：内核 PT 的后续修改不会自动同步到已克隆的 PD，反之亦然

### 10 个分配点迁移审计

全部 10 个用户态物理页分配点已从内核区域分配器迁移到用户区域分配器：

| # | 文件 | 函数 | 分配类型 | 标记方式 |
|---|------|------|----------|----------|
| 1-2 | `sched/task.c` | `task_create_user_with_pages` | 用户栈页 + 代码页(n) | `paging_set_user_accessible_for_task` |
| 3 | `fs/elf.c` | `elf_load` | 代码段连续页 | 返回 page 数组，由调用者统一标记 |
| 4 | `fs/elf.c` | `elf_setup_user_stack` | 用户栈页 | 返回单页，由调用者统一标记 |
| 5 | `kernel/main.c` | shell 任务创建 | shell 代码页 | `paging_set_user_accessible_for_task` |
| 6 | `kernel/main.c` | ramfs_test | ramfs_test 代码页 | 同上 |
| 7 | `kernel/main.c` | regtest_a | regtest_a 代码页 | 同上 |
| 8 | `kernel/main.c` | regtest_b | regtest_b 代码页 | 同上 |
| 9 | `kernel/main.c` | test_probe_a | probe_a 代码页 | 同上 |
| 10 | `kernel/main.c` | test_probe_b | probe_b 代码页 | 同上 |

### 隔离验证

- [x] **probe_b 自读**：probe_b 读取自身 magic 0xBB → "B: self OK"，确认私有 PD 中的
  用户页映射正常工作
- [x] **probe_a 自读**：probe_a 读取自身 magic 0xAA → "A: self OK"，同上
- [x] **跨任务读触发 #PF**：probe_a 尝试读取 `[0x0080E000 + 0x200]`（probe_b 的代码页 +
  magic offset）→ #PF Vector 0x0E
- [x] **Error Code 精确验证**：ErrCode=0x05
  - bit 0 = 1：protection violation（页面存在但特权级不够，不是"页面不存在"）
  - bit 1 = 0：read access
  - bit 2 = 1：user-mode
  - 0x05 证明隔离是由硬件页保护机制强制执行的，而非"页表没映射"（那会是 0x04）
- [x] **CR2 验证**：CR2=0x0080E200，精确命中 probe_b magic 的虚拟地址
- [x] **Task ID 验证**：异常处理器输出触发任务的 id 和 cr3，确认是 probe_a（非 probe_b
  或内核任务）

### 文件清单

```
arch/x86/
  paging.h              — +paging_set_user_accessible_for_task(), +paging_clone_kernel_pts()
  paging.c              — +page_is_user_accessible 使用 task_current()->cr3, +按需 PT 克隆
  exception.c           — +CR2 读取, +触发任务 id/cr3 输出
mm/
  pmm.h                 — +pmm_alloc_user_page(), +pmm_alloc_contiguous_user_pages()
  pmm.c                 — +用户区域分配器（从 top_of_memory 向下分配）
sched/
  task.h                — TCB +cr3 字段 (offset 0x14), +task_create_user_with_pages()
  task.c                — +task_create_user_with_pages(), task_create_user 内部适配
  switch.asm            — +CR3 切换逻辑 (mov esp 之后, pop 之前)
fs/
  elf.h                 — elf_load/elf_setup_user_stack 签名改为输出 page 地址
  elf.c                 — 迁移到用户区域分配器, 移除直接 paging_set_user_accessible 调用
kernel/
  main.c                — ELF 路径适配 task_create_user_with_pages, +Phase 12 隔离验证
user/
  test_probe_a.asm      — 隔离验证程序 A（自读 0xAA + 跨任务读 0x0080E200 → #PF）
  test_probe_b.asm      — 隔离验证程序 B（自读 0xBB + yield 循环）
Makefile                — +test_probe_a/test_probe_b 编译规则, +依赖更新
```

## 阶段 13a：单窗口渲染 ✓ (2026-07-12)

### 设计决策

- **最小 window_t**：x, y, w, h, title, title_bar_color, body_color — 无 z_index（单窗口）、无 buffer（双缓冲）、无 flags（隐藏/最小化）
- **直接 framebuffer 绘制**：跳过双缓冲。双缓冲解决的核心问题（遮挡修复）在单窗口场景不存在；多窗口 Z 序合成时再引入
- **静态显示**：窗口内容在 `sti` 之前一次性绘制，之后不再变化。光标与窗口的交互（damage region 合并）留到 Z 序/damage tracking 子阶段
- 决策-006 记录：光标 save/restore 与窗口重绘的时序交互是主动简化，后续必须重新设计

### 窗口外观规格

- 标题栏高度：20px（字体 16px + 上下各 2px padding）
- 边框：1px 深灰 (0x40, 0x40, 0x40)
- 标题栏背景：深蓝灰 (0x30, 0x40, 0x60)
- 标题文字：白色 8×16 位图字体，x+4, y+2
- 主体：浅灰 (0xD0, 0xD0, 0xD0)

### 像素验证（16 点位，区域扫描法）

| # | 采样点 | 坐标 | 预期 RGB | 实测 RGB | 判定 |
|---|--------|------|----------|----------|------|
| 1 | 窗口标题文字 白色 | (35, 146) | (0xFF, 0xFF, 0xFF) | (0xFF, 0xFF, 0xFF) | PASS |
| 2 | 窗口标题栏背景 深蓝灰 | (40, 150) | (0x30, 0x40, 0x60) | (0x30, 0x40, 0x60) | PASS |
| 3 | 窗口主体 浅灰 | (40, 180) | (0xD0, 0xD0, 0xD0) | (0xD0, 0xD0, 0xD0) | PASS |
| 4 | 窗口边框 左上 | (30, 140) | (0x40, 0x40, 0x40) | (0x40, 0x40, 0x40) | PASS |
| 5 | 窗口边框 右上 | (310, 140) | (0x40, 0x40, 0x40) | (0x40, 0x40, 0x40) | PASS |
| 6 | 窗口边框 左下 | (30, 340) | (0x40, 0x40, 0x40) | (0x40, 0x40, 0x40) | PASS |
| 7 | 窗口边框 右下 | (310, 340) | (0x40, 0x40, 0x40) | (0x40, 0x40, 0x40) | PASS |
| 8 | Phase10 灰色ASCII | (61, 64) | (0xAA, 0xAA, 0xAA) | (0xAA, 0xAA, 0xAA) | PASS |
| 9 | Phase10 绿色小写 | (59, 82) | (0x00, 0xCC, 0x00) | (0x00, 0xCC, 0x00) | PASS |
| 10 | Phase10 蓝色大写 | (52, 100) | (0x00, 0x88, 0xFF) | (0x00, 0x88, 0xFF) | PASS |
| 11 | Phase10 橙色数字 | (55, 118) | (0xFF, 0x88, 0x00) | (0xFF, 0x88, 0x00) | PASS |
| 12 | Phase10 标题文字 | (54, 34) | (0xFF, 0xFF, 0xFF) | (0xFF, 0xFF, 0xFF) | PASS |
| 13 | Phase10 黄色矩形 | (500, 350) | (0xFF, 0xFF, 0x00) | (0xFF, 0xFF, 0x00) | PASS |
| 14 | Phase10 红色边框 | (396, 296) | (0xFF, 0x00, 0x00) | (0xFF, 0x00, 0x00) | PASS |
| 15 | Phase10 青色对角线 | (500, 375) | (0x00, 0xFF, 0xFF) | (0x00, 0xFF, 0xFF) | PASS |
| 16 | 背景色 右下角 | (900, 700) | (0x20, 0x20, 0x30) | (0x20, 0x20, 0x30) | PASS |

注：文字类采样点坐标通过区域扫描法确定（在期望行内搜索目标颜色首次命中像素），
非固定坐标，因为 8×16 位图字形的点亮像素精确位置取决于字符形状。

### 文件清单

```
wm/
  window.h           — window_t 结构体 + window_draw() 声明
  window.c           — window_draw() 实现（边框→标题栏→文字→主体）
kernel/
  main.c             — +Phase 13a 窗口绘制调用（FB 就绪后, sti 前）
Makefile             — +wm/window.c 编译规则, +wm/window.h 依赖
docs/
  decisions.md       — +决策-006 光标/窗口交互主动简化
```

## 阶段 13b：输入路由（鼠标点击 → 窗口命中检测）✓ (2026-07-12)

### 设计决策

- **分层架构**：驱动层不知道 window_t 的存在。mouse 驱动通过回调函数指针
  `g_mouse_click_callback` 上报原始事件 (x, y, buttons)；上层（main.c）注册
  回调，在回调中做 window_hit_test() 判断命中
- `mouse.h` 定义 `mouse_click_callback_t` 类型 + extern 函数指针
- `mouse.c` 在 `mouse_process_packet()` 按钮状态变化时调用回调
- `wm/window.h` 提供 `window_hit_test(win, x, y)` — 纯内联，矩形边界判断
- `kernel/main.c` 实现 `on_mouse_click()` 回调 → hit test → 串口输出

### 依赖方向

```
wm/window.h  ←  kernel/main.c (回调处理, hit test)
                    ↓ 注册回调
              drivers/mouse.c (不知道 window_t 的存在)
```

到阶段 13c（多窗口）时，只需在回调处理中遍历窗口列表按 Z 序做 hit test，
mouse.c 无需任何改动。

### 验证（5 点位自检）

窗口边界：x=[30, 310), y=[140, 340)

| # | 坐标 | 位置描述 | 结果 |
|---|------|---------|------|
| 1 | (160, 270) | 窗口主体内 | inside window |
| 2 | (800, 500) | 远距离外 | outside window |
| 3 | (35, 146) | 标题栏内 | inside window |
| 4 | (29, 139) | 左上方紧邻外 | outside window |
| 5 | (30, 140) | 左上角边界 ≥ | inside window |

### QEMU PS/2 鼠标限制说明

PS/2 鼠标协议天然是相对移动——QEMU VNC 的绝对坐标 / QEMU monitor 的
`mouse_move` 命令均无法转换为 guest 内的 PS/2 相对运动（需要 USB tablet
驱动支持，当前未实现）。因此实时鼠标点击验证通过 `sti` 前直接调用回调
的自检方式完成，而非通过 VNC 模拟鼠标移动。

### 文件清单

```
drivers/
  mouse.h           — +mouse_click_callback_t 类型, +g_mouse_click_callback extern
  mouse.c           — +g_mouse_click_callback 定义, 按钮变化时调用回调
wm/
  window.h          — +window_hit_test() 内联（边界判断）
kernel/
  main.c            — +on_mouse_click() 回调, +回调注册, +5点位自检
```

## 阶段 13c：Z 序 + 多窗口合成 ✓ (2026-07-12)

### 设计决策

- **窗口管理器 `wm/wm.c`**：管理 Z 序窗口列表，固定大小指针数组
  `window_t* g_windows[MAX_WINDOWS]`（MAX=16），索引 0 = 底层
- **`wm_draw_all()`**：桌面背景填充 → bottom→top 逐个绘制窗口，
  内部处理光标 hide/show（复用 mouse.c 的 cursor_restore/draw）
- **`wm_handle_click()`**：top→bottom 命中检测，命中则将该窗口移至
  数组末尾（置顶）并触发全量重绘
- **光标接口导出**：`mouse_cursor_hide/show()` 从 mouse.c 导出，
  wm 层合法依赖 drivers 层（上层依赖下层）
- **无双缓冲**：通过 bottom→top 绘制顺序保证遮挡正确

### 依赖关系

```
wm/wm.c  →  wm/window.h  (window_t, window_draw)
         →  drivers/mouse.h  (mouse_cursor_hide/show)
         →  gfx/primitives.h  (fill_rect for desktop)
         →  mm/multiboot.h  (g_framebuffer)

kernel/main.c  →  wm/wm.h  (wm_add_window, wm_draw_all, wm_handle_click)
               →  drivers/mouse.h  (注册回调)
```

### IRQ12 上下文重绘分析

`wm_handle_click()` 的完整调用链运行在 IRQ12 中断上下文（IF=0）中。
`wm_draw_all()` 执行全屏重绘（~1.15M put_pixel @ 1024×768 + 3 窗口），
QEMU TCG 下估计数百毫秒，会延迟 PIT 18.2Hz tick 约 10~20 次。
8259 PIC 电平触发保证中断不丢失，仅调度延迟。详见 Decision-007。

### 验证（5 点位 Z 序自检）

窗口布局：A(50,60) 220×160, B(160,120) 220×160, C(270,180) 220×160
初始 Z 序：A(bottom), B, C(top)

| # | 坐标 | 描述 | 结果 | Z 序变化 |
|---|------|------|------|----------|
| 1 | (400,100) | 空白区域 | no hit | 不变 |
| 2 | (100,100) | 仅 A 内 | A 置顶 | A,B,C → B,C,A |
| 3 | (350,250) | B+C 重叠区 | C 置顶 | B,C,A → B,A,C |
| 4 | (200,140) | A+B 重叠区 | A 置顶 | B,A,C → B,C,A |
| 5 | (400,400) | 空白区域 | no hit | 不变 |

### 文件清单

```
wm/
  wm.h              — NEW: 窗口管理器接口 (wm_add_window, wm_draw_all, wm_handle_click)
  wm.c              — NEW: Z 序数组管理, 点击置顶, 全量重绘, 光标 hide/show 协调
drivers/
  mouse.h           — +mouse_cursor_hide/show 声明
  mouse.c           — +mouse_cursor_hide/show 实现（复用 cursor_restore/draw）
kernel/
  main.c            — 3 demo 窗口 (A/B/C) 替换单窗口, on_mouse_click→wm_handle_click
Makefile            — +wm/wm.c 编译规则, +wm/wm.h 依赖
docs/
  decisions.md      — +决策-007 IRQ12 上下文重绘延迟分析（计时估算+PIC电平触发+3行兜底方案）
```