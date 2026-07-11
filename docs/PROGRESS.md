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

