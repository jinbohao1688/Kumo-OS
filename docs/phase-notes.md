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

## Phase 5 踩坑

### PDE.U/S=0 导致 Ring3 指令取指 #PF（error code=0x05）

**日期**：2026-07-11

**场景**：实现 `paging_set_user_accessible()` 时，只设置了 PTE.U/S=1，
未设置 PDE.U/S。结果 Ring3 代码一执行就 #PF，error code=0x05
（user-mode read, protection violation — 注意 32-bit non-PAE 模式下
I/D bit 恒为 0，取指 fault 和数据 fault 的 error code 看起来完全一样）。

**根因**：两级页表的权限是层级化的。PDE.U/S=0 意味着整个 4MB 区域是
supervisor-only，覆盖其下所有 PTE 的 U/S 设置。即使 PTE.U/S=1，
CPU 在 page walk 时先检查 PDE，发现 PDE.U/S=0 即触发 protection fault。

**教训**：修改页表权限时必须同时检查 PDE 和 PTE 两级。单级修改后要用
GDB 手动 `x/1xw` 检查两级页表项的值，不能只看 PTE。这个坑在切换到
PAE（三级）或长模式（四级）时会变得更隐蔽——层级越深，遗漏某一级
U/S 的概率越大。

### switch_to + iret 组合栈帧设计

**日期**：2026-07-11

**背景**：一个新用户任务首次被调度时，switch_to 需要自然地过渡到
return_to_ring3 trampoline，再 iret 到 Ring3。这三个机制（上下文切换、
Ring3 返回、iret 特权级切换）消耗同一段栈空间，必须按顺序布局：

```
TCB.esp → [ebx/esi/edi/ebp]    ← switch_to 的 pop 序列
          [return_to_ring3]      ← switch_to 的 ret 跳转目标
          [EIP/CS/EFLAGS/ESP/SS] ← iret 帧（5 dwords）
TSS.esp0 → kernel_stack_top
```

**关键约束**：
1. switch_to 先 pop 4 个 callee-saved 寄存器，再 ret → 消耗 20 字节后
   到达 iret 帧，所以 iret 帧从 TCB.esp+20 开始
2. TCB.esp（switch_to 恢复点）≠ TSS.esp0（CPU 中断入口点）。
   TCB.esp 是"上次切出时的保存点"，TSS.esp0 是内核栈绝对顶部。
   对于新任务，TCB.esp = kstack_top - 40；TSS.esp0 = kstack_top
3. task_yield 必须在 switch_to 之前更新 TSS.esp0，否则新任务运行期间
   发生中断/syscall 时 CPU 会压栈到旧任务的内核栈（栈踩踏）

**后续价值**：任何涉及"从调度器首次启动一个低特权级任务"的场景
（进程 fork、信号处理、用户态线程），都需要设计类似的组合栈帧。
核心原则是先确定最底层的 CPU 机制（iret/sysret）需要什么栈布局，
再往上叠加调度器的 pop/ret 序列，自底向上构建。

## Phase 7 踩坑

### strcmp_word 寄存器复用导致自我覆盖（指针被数据操作破坏）

**日期**：2026-07-11

**场景**：Shell 的 `strcmp_word` 函数用于比较用户输入与已知命令字符串。
函数接收两个指针：`ebx`=line_buf 地址，`edx`=命令字符串地址。
循环内逐字节比较：

```asm
strcmp_word:
    push edx          ; 保存 edx
    xor  ecx, ecx
.cmp_loop:
    mov  al, [ebx + ecx]
    mov  dl, [edx + ecx]   ; ← BUG: dl 是 edx 的低字节！
    cmp  al, dl
    ...
    pop  edx
    ret
```

`mov dl, [edx + ecx]` 将命令字符串的一个字节读入 `dl`——但 `dl` 正是
`edx` 的低 8 位。第一次迭代（ecx=0）后，`edx` 的低字节被覆盖为字符值
（如 'h'=0x68），后续迭代 `[edx + ecx]` 访问的地址不再是原始命令字符串
地址，而是被破坏后的地址。函数末尾 `pop edx` 恢复了调用者的 edx，
但在循环内部，edx 已经面目全非。

**症状**：Shell 将所有命令（help/ls/cat/echo/run）都判为 "unknown command"。
但 `puts`（用 `esi` 做字符串指针）正常输出 banner 和 prompt——说明 base
address（ebp）是正确的，数据段引用的地址计算也是对的。问题只在 `strcmp_word`
这一个函数内部。

**修复**：将命令字符串指针从 `edx` 切换到 `esi`：
```asm
strcmp_word:
    push esi
    mov  esi, edx       ; esi = 命令字符串指针（不再用 edx 做索引）
    xor  ecx, ecx
.cmp_loop:
    mov  al, [ebx + ecx]
    mov  dl, [esi + ecx] ; dl 仍做临时数据寄存器，但 esi 不变
    cmp  al, dl
    ...
    pop  esi
    ret
```

**可迁移经验**：在 x86 汇编中，**一个寄存器不能同时身兼"地址指针"和
"临时数据寄存器"两个角色**。`al/ah` 会破坏 `eax`，`dl/dh` 会破坏 `edx`，
以此类推。当你写 `mov dl, [edx + offset]` 时，edx 在指令执行后就不再
是原来的指针了。这个模式与阶段 2/4 中栈布局错位（push/pop 顺序不对
导致结构体字段偏移量全部错位）属于同一类 bug——**资源复用时的隐式覆盖**。
后续写任何涉及寄存器双重角色的汇编代码时，先在注释里标注每个寄存器的
"角色"（指针 / 临时数据 / 循环计数器），确认没有角色冲突。

### dispatch 段编辑残留导致 help 命令丢失

**日期**：2026-07-11

**场景**：调试过程中在 `.dispatch` 段加了一段 PRINT debug 代码，
随后用 Edit 工具恢复原代码时，new_string 比 old_string 短，
漏掉了 `call cmd_help_handler; jmp main_loop` 两行。
结果 `.dispatch` 在 `jnz .try_ls` 之后直接落入了 `.try_ls` 标签，
"help" 匹配成功后实际执行的是 "ls" 的处理逻辑（而 "ls" 匹配它又不匹配），
一路 fall-through 到 `.unknown`。

**教训**：Edit 工具按精确字符串替换，不会提示"替换后代码逻辑是否完整"。
涉及控制流（if/else 分支、函数调用链）的编辑，替换后应肉眼确认
每个分支都有正确的出口（jmp/ret/call + jmp）。

### QEMU `-serial stdio` 在管道输入时丢失首字节

**日期**：2026-07-11

**场景**：用 `printf 'help\n' | qemu-system-i386 -serial stdio` 向
虚拟机 COM1 串口发送输入时，guest 收到的第一个字节丢失（'h' 未到达），
导致 "help" 变成 "elp"。

**根因**：QEMU 的 `-serial stdio` 在 stdin 不是终端（isatty）时，
内部初始化路径可能与终端模式不同，导致管道的第一个字节被内部消耗
（可能是 termios 设置或缓冲区同步相关）。此为 QEMU 行为，非内核 bug。

**规避方案**：
1. 测试时在输入前加 `sleep 3`，等 QEMU/guest 完全就绪后再发送数据
2. 或在输入前面加一个无意义的前导字符作为"牺牲字节"（`printf 'xhelp\n'`）
3. 或使用 `-nographic`（其串口 multiplex 逻辑与 `-serial stdio` 不同，
   在 echo pipe 测试中未见首字节丢失）
4. 生产环境（真实硬件 / 终端直接连接）不受此问题影响

**后续价值**：自动化测试脚本中如果通过管道向 QEMU 串口发送命令，
务必在启动后加 `sleep N` 确保 QEMU 初始化完成，且第一个命令前
可以发送一个无害的换行符作为"暖机"字符。

## Phase 8a 踩坑

### strcmp_word 比较顺序导致所有带参数命令失败

**日期**：2026-07-11

**场景**：阶段 8a 实现 ADR-004 后运行第一个带参数命令 `run ramfs_test`，
结果 `strcmp_word` 返回不匹配，所有带参数命令（`cat /test`、`echo hi`、
`run bad_ptr` 等）全部被判为 "unknown command"。

**根因**：`strcmp_word` 的 `.cmp_loop` 先执行 `cmp al, dl; jne .no_match`，
再检查 `cmp dl, 0; je .check_end`。当命令字符串已全部匹配完（dl=0，
即 NUL 终止符），而用户输入的下一个字符是空格（al=0x20）时，
`cmp al, dl`（0x20 vs 0x00）不相等，直接跳到 `.no_match`，永远不会
到达 `.check_end`（那个专门处理"命令匹配完毕，检查后续字符是否为
空格或 NUL"的逻辑）。

例如 `"run ramfs_test"` 与 `"run"` 比较：
- 前 3 次迭代（r/u/n）全部匹配，inc ecx
- 第 4 次迭代：al=' ' (0x20), dl=0 → cmp 不等 → jne .no_match → 返回 1（不匹配）

**修复**：将 `cmp dl, 0; je .check_end` 移到 `cmp al, dl; jne .no_match`
**之前**。当命令字符串已结束时（dl=0），先跳到 `.check_end` 检查用户
输入的下一个字符是否为空格或 NUL，而不是直接比较字符值。

**为什么阶段 7 没发现**：阶段 7 的验证只跑过 `help` 和 `ls` 两个命令——它们
都不带参数，第一个空格之后没有更多字符，所以在 `.check_end` 检查时
`al` 刚好是 NUL（行缓冲末尾），走的是 `.match` 路径，不触发这个问题。
`cat`、`echo`、`run` 三个命令虽然代码写完整了，但从没被真实调用过。

**可迁移经验**：新写的测试用例经常会意外触发到旧代码里从没被覆盖过的
分支——这个 bug 不是因为阶段 8a 修改了 `strcmp_word`，而是阶段 8a
第一次真正执行了带参数命令的代码路径。**只测"能跑起来的那条路"不够，
要主动测不同的输入形状**（空输入、带一个参数、带多个参数、参数含特殊
字符等）。阶段 7 "最小闭环验证通过"这个结论，只对当时实际跑过的
`help`/`ls` 路径成立，带参数命令是"假设正确的未验证分支"。
