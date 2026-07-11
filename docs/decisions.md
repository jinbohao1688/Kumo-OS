# 架构决策记录 (Architecture Decision Records)

## ADR-001: 32 位保护模式起步

**日期**：2026-07-10

**决策**：先在 32 位保护模式（i686）下完成阶段 1~3（引导→中断→分页），
之后再过渡到 64 位长模式。

**理由**：
- 32 位保护模式的 GDT/分页/中断模型更简单，学习资料丰富（OSDev Wiki、JamesM's tutorial 等）
- 吃透 32 位机制后切 64 位只是多一层 PML4 + 寄存器变宽，心智负担小
- 直接上 64 位长模式的进入过程本身（32→PAE→长模式）就是一段不小的复杂度，
  容易在阶段 1 就卡住打击积极性

**范围影响**：交叉编译器选用 `i686-elf-gcc` 而非 `x86_64-elf-gcc`；
链接脚本、GDT/IDT 结构均按 32 位设计。

**状态更新（2026-07-11，阶段 5 完成后）**：截至阶段 5（Ring0↔Ring3 + 多任务
调度），项目保持 32 位，暂无切换 64 位计划。如阶段 6/7 遇到 64 位相关的明确
需求（如 >4GB 物理内存支持、64 位用户程序），再重新评估。当前 32 位方案在
QEMU 128MB VM 中运行良好，无 urgent pressure 切换。

## ADR-002: 交叉工具链使用已有预编译版本

**日期**：2026-07-10

**决策**：交叉工具链使用已有的预编译 `i686-elf-gcc 13.2.0`（位于
`~/i686-elf-tools/`），未从源码重新构建。

**理由**：
- 已有工具链版本足够新（GCC 13.2.0 + Binutils 2.41），完全满足内核开发需求
- 从源码构建交叉编译器需要额外依赖（libgmp-dev、libmpc-dev、libmpfr-dev）
  且编译耗时 30-60 分钟，在当前阶段没有附加价值
- 与 OSDev Wiki 教程的差异仅在于编译器版本号，不影响功能

**注意事项**：如果后续遇到编译器版本导致的问题，可以考虑从源码构建以对齐
教程推荐的版本组合（如 GCC 11.x + Binutils 2.37）。

## 经验-001: ISR 双路径必须分别用真实异常验证

**日期**：2026-07-11

**背景**：x86 异常中有的自动压入 error code（如 #GP/#PF），有的不压（如 #DE）。
我们的 ISR trampoline 使用两类宏——`ISR_NOERR`（手动 push dummy 0）和
`ISR_ERR`（不 push，CPU 已压入 error code）——来统一栈布局。

**经验**：两条路径必须**分别用真实异常触发来验证**，不能只做代码审查。

- **ISR_NOERR 路径**：用 `div %ecx`（ECX=0）触发 #DE（向量 0），验证 err_code 字段
  为占位 0、寄存器 dump 未错位。
- **ISR_ERR 路径**：用 `mov $0x18, %ax; mov %ax, %ds`（越界段选择子）触发 #GP
  （向量 13），验证 err_code = 0x0018（坏选择子值）被正确读取、栈布局未被破坏。

**为什么重要**：压栈顺序和 C 结构体字段顺序不匹配时，编译通过、不崩溃，但读出来
的寄存器值全部错一位——这是极难事后排查的 bug。两条路径分别实测，成本很低
（改一行触发代码、重编译、跑一次 QEMU），但能把 32 个 ISR 入口的宏分配错误
一网打尽。

**后续价值**：凡涉及"汇编压栈 + C 解析栈结构"的代码（上下文切换、syscall 参数
传递等），都应该套用这个思路：分别找一条触发路径来验证结构体对齐，不要相信
"看起来顺序一样"。

## ADR-003: 暂不做 NULL 页 guard（物理页 0 可访问）

**日期**：2026-07-11

**决策**：初始恒等映射范围包含物理页 0（虚拟地址 0x0 → 物理地址 0x0），
不设置 NULL 页写保护。

**理由**：
- 全物理内存恒等映射（0 到 top_of_memory）是最简单的初始方案，避免任何
  映射边界条件，降低分页开启瞬间的出错面
- 低 1MB 区域当前被 PMM 排除不用但不被破坏；如果将来需要 VGA text mode
  buffer（0xB8000）或其他低端内存用途，无需额外映射步骤
- 空指针解引用虽然不会触发 #PF，但在纯实验环境中，内核崩溃不会导致数据
  丢失——这个问题在阶段 5/6（用户态）之前不构成实际威胁

**后续动作**：在阶段 5/6（用户态）之前，可通过单独清除 PTE[0] 的 Present 位
来解映射 NULL 页，使空指针解引用触发 #PF 而非静默访问。成本仅一个 PTE 写
操作，不涉及重新分配页表。

**状态更新（2026-07-11，阶段 5 完成后）**：用户态已上线（Ring3 任务运行中），
NULL 页 guard 的触发条件已满足。当前所有用户代码均为内核控制的测试 blob，
不会主动解引用 NULL 指针，因此风险可控。阶段 6 引入 ramfs + 用户程序加载后，
如果用户程序来源不再完全受控（如从文件系统加载任意二进制），应在加载首个
外部用户程序之前完成 NULL 页 unmap。实现方式：在 paging_init() 末尾或新增
`paging_unmap_null_page()` 中清除 PTE[0].P 位 + invlpg。

**已实现（2026-07-11，阶段 8a）**：`paging_unmap_null_page()` 已实现并接入
kmain 初始化流程（paging_init() 之后、kheap_init() 之前）。实现：
- 清除 `PTE[0].P`（Present 位）
- `invlpg` 刷新地址 0x0 的 TLB 条目
- 验证：`test_null` 用户程序解引用 NULL 触发 #PF（Vector 0x0E, ErrCode 0x04），
  异常处理器正确捕获并打印寄存器 dump

## ADR-004: 用户态 syscall 指针校验 — 当前阶段记作技术债

**日期**：2026-07-11（阶段 6 引入文件系统 syscall 时）

**背景**：32 位恒等映射下，内核（Ring0）可以解引用任意用户态传进来的指针，
包括指向内核私有数据/代码的指针。x86 在 32 位模式下没有 SMEP/SMAP
（这些是 64 位长模式才有的 CR4 控制位）。新增的 read/write/open syscall
均接收用户态 buffer/path 指针，当前**不做任何校验**直接解引用。

**风险示例**：
- 用户调用 `write(0, 0x00101000, 100)` → 把内核代码区内容写到文件（信息泄漏）
- 用户调用 `read(0, 0x00101000, 100)` → 用文件内容覆盖内核代码（系统破坏）

**为什么当前不紧急**：
- 所有用户代码仍是内核手工编写的测试 blob，不会故意传恶意地址
- 阶段 6 不涉及从文件系统加载外部用户程序，攻击面为零

**决策**：当前阶段在 syscall_dispatch.c 的 read/write 路径加注释标记
`FIXME: validate user buffer`，不实现校验逻辑。

**后续实现方案**（触发条件：首次从文件系统加载外部用户程序之前）：
新增两个内核函数：

```c
int copy_from_user(void *kernel_dst, const void *user_src, uint32_t size);
int copy_to_user(void *user_dst, const void *kernel_src, uint32_t size);
```

内部遍历 buffer 覆盖的每个页，查页表确认 PDE.U/S=1 且 PTE.U/S=1
（即该页确实是用户态可访问的）。任一页不满足则返回 `-EFAULT`。
实现位置：`arch/x86/paging.c`（复用页表结构知识）。

**影响范围**：所有接受用户态指针的 syscall（read/write/open 的 path/buf
指针、后续的 execve/stat 等）都需要切换为 copy_from/to_user 模式。
syscall_dispatch 中不再直接解引用用户指针，改为先 copy 到内核缓冲区
（或至少先校验页权限）。

**已实现（2026-07-11，阶段 8a）**：在 `arch/x86/paging.c` 中实现：
- `page_is_user_accessible(vaddr)` — 内部函数，检查单页的 PDE.U/S + PTE.U/S + PTE.P
- `is_user_accessible_range(vaddr, size)` — 遍历区间内所有页
- `copy_from_user(kernel_dst, user_src, size)` — 预验证 + memcpy
- `copy_to_user(user_dst, kernel_src, size)` — 预验证 + memcpy
- `copy_from_user_string(kbuf, user_ptr, max_len)` — 逐字节扫描，仅在跨越
  4KB 页边界时验证新页，遇到 NUL 即停止，绝不触碰字符串范围外的页面

接入的 syscall（共 6 个指针全部接入校验）：
- SYSCALL_OPEN — copy_from_user_string(path)
- SYSCALL_READ — kmalloc 内核缓冲区 → vfs_read → copy_to_user(user_buf)
- SYSCALL_WRITE — copy_from_user(user_buf) → vfs_write
- SYSCALL_READDIR — copy_from_user_string(path) + copy_to_user(name_buf)
- SYSCALL_RUN — copy_from_user_string(name)
- SYSCALL_WRITECONSOLE — copy_from_user(buf) → serial_putchar 逐字节输出

验证结果：
- `test_bad_ptr`：传入内核地址 0x100000 被正确拦截，OPEN 返回 -1
- `test_boundary`：路径字符串在页面偏移 0xFFD（距页尾 3 字节）处，
  逐字节扫描仅验证当前页，NUL 后停止，文件操作成功（BOUNDARY_OK）
