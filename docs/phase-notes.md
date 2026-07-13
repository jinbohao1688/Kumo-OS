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

## Phase 8b 踩坑

### 两条可能产生相同输出的执行路径，靠 argv[0] 差异 + 时序差异区分验证

**日期**：2026-07-11

**场景**：阶段 8b Step 5（shell exec 命令接入）验证时，存在两条可能产出
"Hello from ELF!" 的执行路径：
1. Step 4 硬编码路径：kmain 直接在启动时调用 `task_create_user(elf_entry, 'E', user_esp)`
2. Step 5 exec 路径：用户敲入 `exec /hello.elf` → shell 解析 → syscall → exec_elf_from_path

两条路径输出完全相同的内容，如果只看串口有 "Hello from ELF!" 就判断
"exec 成功了"，有可能 Shell 的 exec 命令根本就是 no-op，只是恰好在同一
时刻 Step 4 的硬编码任务输出了相同内容——即"巧合成功"。

**解决方案**：设计两条路径产生**可区分的副作用**：
1. **argv[0] 差异**：Step 4 传的程序名是 `"hello_elf"`，exec 传入的是
   用户实际输入的路径 `"/hello.elf"`。Stack dump 中 argv[0] 字符串不同。
2. **时序差异**：Step 4 的 ELF 任务在 task_init 之后立即创建，输出出现在
   第一个 tick 之前；exec 的任务在用户交互之后创建，输出出现在几十个 tick
   之后（本次实测 tick 0x2D）。

**可迁移经验**：当无法从输出内容本身区分"巧合成功"和"真正走完整链路"时，
需要主动设计可区分的验证证据。常见手法：
1. **数据差异性**：不同路径传入不同的数据（如 argv[0]、文件内容、参数值），
   在输出中体现差异
2. **时序/顺序差异性**：如果两条路径的执行时机天然不同（如启动阶段 vs
   交互阶段），利用计时器/tick 计数区分
3. **增量验证**：先禁用一条路径（如注释掉 Step 4 的硬编码调用），单独验证
   另一条，再两条同时启用，确认输出出现两次且内容可区分

这次 argv[0] + tick 时序的双重区分方案，成本极低（一行代码不改，仅靠已有
的 stack dump 和 tick 输出），但完全排除了"巧合成功"的混淆可能。

## Phase 9 踩坑

### multiboot_tag_framebuffer_t 结构体的 framebuffer_bpp 字段类型错误

**日期**：2026-07-11

**场景**：初版 `multiboot_tag_framebuffer_t` 将 `framebuffer_bpp` 写成
`uint32_t`（4 字节），凭经验认为"bpp 是个整数字段，应该是 4 字节对齐"。
实际 GRUB 的源码（`multiboot2.h`）中 `framebuffer_bpp` 是 `uint8_t`（1 字节），
`framebuffer_type` 紧随其后（1 字节），然后是 `reserved`（2 字节）。

**后果**：伪造的  4 字节 bpp 导致后续所有字段（framebuffer_type、reserved、
red_field_position、red_mask_size、green_field_position、green_mask_size、
blue_field_position、blue_mask_size）整体偏移 3 字节。`red_field_position`
读到的实际是 `green_mask_size` 的值，`red_mask_size` 读到的是
`blue_field_position`，以此类推全部错位。

**这个 bug 在填充纯色的验证目标下完全测不出来**——因为纯色填充不依赖
color_info 字段。要到阶段 10 字体渲染或后续需要根据 framebuffer_type
正确处理颜色格式时，才会表现为"颜色不对但程序不崩溃"，而这种间接的症状
比崩溃更难定位根因。

**修复**：将 `framebuffer_bpp` 改为 `uint8_t`，结构体用
`__attribute__((packed))` 确保编译器不插入填充字节。修复后逐字段核对
GRUB 官方源码的偏移量：

| 偏移 | 字段 | 大小 |
|------|------|------|
| 0 | type | u32 |
| 4 | size | u32 |
| 8 | framebuffer_addr | u64 |
| 16 | framebuffer_pitch | u32 |
| 20 | framebuffer_width | u32 |
| 24 | framebuffer_height | u32 |
| 28 | framebuffer_bpp | **u8** |
| 29 | framebuffer_type | u8 |
| 30 | reserved | u16 |
| 32 | red_field_position | u8 |
| 33 | red_mask_size | u8 |
| 34 | green_field_position | u8 |
| 35 | green_mask_size | u8 |
| 36 | blue_field_position | u8 |
| 37 | blue_mask_size | u8 |

验证方式：串口打印 color_info 六项值，与 QEMU `-vga std` 32bpp 下的
已知固定值对照——red: pos=16 mask=8, green: pos=8 mask=8,
blue: pos=0 mask=8。六项全部命中，确认偏移量正确。

**可迁移经验**：对外部规范定义的结构体（GRUB Multiboot、ACPI 表、ELF header
等），**每一个字段的类型和大小都必须对照官方头文件源码逐字段核实，不能
凭经验/直觉判断**。即使是看起来"显然应该是 4 字节对齐"的字段（如 bpp），
在实际代码中也可能是更小的类型。`__attribute__((packed))` 是必要的防御，
但前提是字段类型本身正确——packed 不会修复类型写错导致的偏移量错误。

## Phase 10 踩坑

### 程序化像素采样 vs 人工目视确认——两类验证覆盖不同的缺陷

**日期**：2026-07-11

**场景**：阶段 10 验证时，先用 Python 脚本对 QEMU 截图的指定坐标做像素采样，
确认几何原语的填充色、描边色、对角线颜色、以及各文本行的前景色全部精确命中
预期值。所有采样点通过后，若就此下结论"字体渲染正确"，会漏掉一类重要缺陷：
字形内部 bit 排列错乱、笔画断裂、字符间粘连——这些问题只要字形区域内还有
少量像素被点亮，稀疏坐标采样就可能命中"非背景色"这个判定，程序无法区分
"字形长得像它应该有的样子"和"像素点亮了但字形是歪的"。

**解决方案**：对全部 95 个字符做两轮验证——
1. **程序化像素采样**：验证颜色、坐标、几何形状精确性（机器做，无遗漏）
2. **人工目视扫过全字符集**：直接将字体数据渲染为 ASCII 点阵图，逐字符检查
   基线对齐、笔画连续性和可读性（人眼做，覆盖"输出内容本身对不对"的判断）

两类验证各司其职——自动化采样查"数据从 A 到 B 传对了没有"，人眼查"数据
本身设计对了没有"。

**可迁移经验**：凡是涉及"产出视觉内容"的阶段（字体渲染、图片解码、UI 控件
绘制），除颜色/坐标采样外，必须补充人工目视确认。**"有输出"不等于"输出内容
正确"**——这类缺陷编译器测不出、逻辑测试测不出、坐标采样也测不出，只有人眼
能判断。类似阶段 7 `strcmp_word` 比较顺序 bug 一样，属于"能跑但不一定对的路径"。

## 阶段 11：抢占式调度 (2026-07-11)

### 核心洞察：中断门架构天然提供临界区保护

阶段 11 最关键的发现是**不需要给每个内核数据结构单独加锁**，因为在当前架构下：

1. 所有内核进入路径（IRQ 的中断门 0x8E、syscall 的中断门 0xEE、异常的中断门
   0x8E）都自动清 IF，意味着全部内核代码在 IF=0 下执行
2. 抢占的唯一触发点是 Ring3 用户态代码被定时器中断——此时没有任何内核数据
   结构处于"正在被修改中"的状态
3. 唯一的 IF=1 → 调度器路径是 idle 循环（`for(;;) task_yield()`），
   仅需在 `task_yield()` 开头加一条 `cli` 即完成全部保护

这与预想的"需要在 kmalloc/kfree/VFS/PMM 到处加 cli/sti"完全不同。实际经验是：
**中断门选择的架构决策在阶段 2 就做对了，到阶段 11 直接受益。**

### switch_to 对调用深度不敏感的机制理解

`switch_to` 只做 5 件事：push 4 个 callee-saved 寄存器 → 保存当前 esp →
加载下一个 esp → pop 4 个寄存器 + ret。

**本质概括：`switch_to` 不是"切换任务"，而是"换了一根栈，然后 ret"。**
任务切换 = 栈切换 = esp 换了一个值，然后 CPU 继续沿着新栈的返回地址链走。

关键在 `ret` 指令的语义：**无条件弹出栈顶 4 字节作为 EIP**。它不检查调用深度，
不关心栈上有几层调用帧，不依赖任何任务状态机或调度器元数据。

而"栈顶那 4 字节是什么"完全由任务当初是怎么进入 switch_to 决定的——
`call switch_to` 压入的返回地址永远指向 `task_yield` 里 `call switch_to` 的下一条
指令。所以无论任务从哪条路径进入 task_yield，switch_to 恢复后一定先回到
task_yield，然后 C 调用约定沿栈帧自然 unwind：

| 任务当初的路径 | ret 回到 | 再返回到 | 最终收尾 |
|---|---|---|---|
| idle loop `for(;;) task_yield()` | task_yield | kmain 循环体 | 再次调用 task_yield |
| syscall SYSCALL_YIELD | task_yield | syscall_dispatch → … → iret | Ring3, IF=1 |
| IRQ0 抢占 | task_yield | irq_handler → popad → iret | Ring3, IF=1 |
| 首次启动 (task_create_user) | return_to_ring3 | — | Ring3 entry |

**三条路径的 unwind 链条表**完整对应了实际代码结构：

1. **首次启动路径**：`task_create_user` 构造的栈上 sp[4]=return_to_ring3，
   switch_to 的 ret 跳到 return_to_ring3 → iret 弹出 EIP/CS/EFLAGS/ESP/SS → Ring3

2. **syscall yield 路径**：switch_to 保存的 esp 指向 task_yield 栈帧 → 恢复到
   另一个同样从 task_yield 进入 switch_to 的任务 → ret → task_yield →
   syscall_dispatch → syscall_handler (popa+iret) → Ring3

3. **IRQ0 抢占路径**：栈上多了 pushad 帧 + err/vec + 硬件 iret 帧（4 层额外的
   数据压在 task_yield 上面），但 switch_to 保存的 esp 指向 task_yield 的栈帧，
   上面的一切完整不动。恢复时沿着 task_yield → irq_handler → irq_common_stub
   (popad 恢复全部 8 个寄存器) → add esp,8 → iret → Ring3

**这解释了为什么 4 条汇编指令就能实现上下文切换**，以及为什么同一个 switch_to
对三种不同深度的调用链都不敏感。核心是栈的自描述性——每个栈帧自带返回地址，
ret 链自然 unwrap，不需要 switch_to 知道它上面有几层、是什么。

**可迁移经验**：阶段 12 如果在 switch_to 基础上做改造（如加 CR3 切换），插入点
必须在 `mov esp, [eax]` 之后、`pop+ret` 之前——此时新任务的栈已加载但寄存器
还未恢复，CR3 切换不会影响 pop 的目标寄存器，旧任务的 CR3 在 eax 中仍然可用。

### sti 时机错误 → #PF 的教训

初版将 `sti` 放在 `paging_init` 和 VFS 初始化之间（比 `task_init()` 早约 60 行），
结果定时器中断触发时 `g_current == NULL`，`task_yield()` 解引用 NULL → #PF。

当时考虑的逻辑是"早开中断让硬件尽早工作"，但忽略了**抢占式调度要求任务系统
已初始化**这个前置条件。这是典型的初始化顺序依赖——跟阶段 4 把 `tss_init()` 放到
`task_init()` 之前是同一类教训。

**可迁移经验**：`sti` 的位置在抢占式调度系统中是关键决策点，必须放在所有依赖
`g_current != NULL` 的代码路径之后。原则：**中断开启的时刻 = 系统已准备好接收中断
并正确处理它们的时刻**，而不是"能开就尽早开"。

## 阶段 11b：PS/2 鼠标驱动 (2026-07-12)

### 鼠标驱动会话中断——代码已完成但无 commit、无文档的教训

**日期**：2026-07-12

**场景**：阶段 11b 的鼠标驱动在上一会话中完成了全部代码（PS/2 初始化 8 步流程、
IRQ12 接入、3 字节包状态机、光标 save/restore），但遇到一个视觉 bug（光标划过
对角线时青色变成绿色），调试到一半会话中断了。新会话重建上下文时发现：

1. **零 commit**：鼠标驱动的所有改动（9 个文件，~400 行新增）全部在 working tree
   中，没有任何 commit 记录
2. **零文档**：PROGRESS.md 和 phase-notes.md 完全没有提到鼠标驱动的任何内容
3. **bug 修复上下文丢失**：颜色污染 bug 在代码中已被修复（VNC 验证通过），但
   无法从 git 历史追溯到"是哪一次改动、哪一个具体代码变更解决了该问题"

**后果**：新会话需要先花大量时间考古——读取所有 uncommitted diff、对照文档确认
当前状态、推断哪些代码是功能实现、哪些是 bug 修复。而一个简单的 `git log` 本
可以在几秒内回答这些问题。

**可迁移经验**：**每完成一个可验证的小阶段，应该及时 commit，不要攒着一大批
未提交的改动。** 具体标准：

1. **可验证的最小单元 = 一个 commit**：不要等到"整个阶段 11 全部完成"才提交。
   抢占式调度验证通过 → commit；IRQ12 能收到中断 → commit；光标能在屏幕上
   绘制 → commit。这些 commit 在新会话中就是清晰的路标
2. **文档随代码一起 commit**：PROGRESS.md 和 phase-notes.md 的更新应该与对应
   的代码改动在同一个 commit 中（或紧随其后），不应该等到"整个阶段收尾"再补
3. **bug 修复必须单独 commit**：修复颜色污染 bug 应该是一个独立 commit（
   `fix: cursor restore corrupted background color`），这样以后遇到类似问题时
   可以直接 `git show` 看到完整的修复 diff

**为什么这很重要**：会话会中断，上下文会丢失。但 git 历史和文档是持久的——
它们是未来自己（或其他人）理解"这段代码为什么存在、为了解决什么问题"的唯一
线索。没有 commit 记录的代码 = 没有历史的代码 = 每次都需要重新考古。

### PS/2 初始化中的时序细节

不同于内核中其他子系统的初始化（GDT→IDT→PIC→PMM→paging→kheap，每一步都是
单向依赖，不存在"外界主动发数据给内核"），PS/2 鼠标是第一个需要处理"外部设备
在初始化期间就开始发送数据"的场景：

1. **Enable Data Reporting (0xF4) 之后，鼠标立即开始流式发送 3 字节包**。
   但此时 sti 尚未执行，IRQ12 无法触发 ISR，数据堆积在 PS/2 输出缓冲区
   （8042 Output Buffer）中
2. **解决方案**：sti 之前调用 `mouse_drain_buf()`，轮询 Status Register bit0
   （Output Buffer Full），清空最多 16 字节
3. **额外防御**：`mouse_process_packet()` 丢弃第一个组装的包——即使 drain 之后
   仍有残留，也不会导致后续所有包的 3 字节边界永久偏移

**可迁移经验**：任何在 sti 之前就需要与外设交互的驱动（网卡 DMA 缓冲区预填、
磁盘 AHCI 端口初始化等）都可能遇到同样的"数据先到了但 ISR 还没就绪"问题。
drain + 首包丢弃的双重防御是低成本的安全网。

## 阶段 12：每任务独立页目录 (2026-07-12)

### page_is_user_accessible 硬编码内核 PD 导致静默失败

**日期**：2026-07-12

**场景**：Phase 12 引入每任务独立 PD 后，Shell 任务（拥有私有 PD）启动后
完全无输出——无 banner、无 prompt、无崩溃。现象是"系统正常运行（tick 持续、
其他任务输出正常），但 Shell 静默地什么都不做"。

**根因**：`page_is_user_accessible()` 在 `arch/x86/paging.c:150` 硬编码使用
`pd_phys`（内核 PD）做页表遍历。Phase 12 之后，用户页仅通过
`paging_set_user_accessible_for_task()` 标记在各任务的**私有 PD** 中。
当内核通过 `copy_from_user` → `is_user_accessible_range` →
`page_is_user_accessible` 验证用户指针时，查询的是内核 PD（用户页在内核 PD
中未被标记为 U/S），验证失败 → syscall 返回 -1 → Shell 的所有 syscall
（WRITECONSOLE 输出 banner、READCHAR 读输入）全部静默失败。

Shell 不崩溃——它只是收到 -1 返回值，按设计跳过该操作。内核也不崩溃——
它只是正确地拒绝了"看起来没有 U/S 权限"的页面访问。两边各自的行为在各自
的语义下都是"正确"的，但合在一起造成功能完全失效。

**修复**：`page_is_user_accessible` 改为使用 `task_current()->cr3`（当前任务
的私有 PD）做页表遍历。当 cr3==0（idle/内核任务）时回退到 `pd_phys`。

**不变量验证**：修复引入了一个隐含前提——"调用 `page_is_user_accessible` 时，
`task_current()->cr3` 必须等于 CPU 实际加载的 `%cr3`"。完整调用图分析确认：
所有调用者（`copy_from_user`/`copy_to_user`/`is_user_accessible_range`）仅
从 `syscall_dispatch`（int 0x80）路径到达，此时任务由 `task_yield()` 放到 CPU
上（先设 g_current 再 switch_to 加载 cr3），不变量成立。IRQ handler 和任务
创建路径不使用这些函数。不变量已写入代码注释。

**可迁移经验**：引入 per-task 数据结构后，任何"遍历/校验用户可访问性"的函数
都要重新检视是否隐含了"只有一个 PD"这个已经不再成立的假设。这类假设失效
**不会导致崩溃，只会导致功能安静地失效**——比崩溃更难发现，因为：
1. 没有 #PF、没有 triple fault、没有寄存器 dump
2. 串口输出中看不到任何错误信息
3. 系统在其他方面完全正常（tick 持续、其他任务运行、中断响应正常）
4. 只有通过"预期输出没有出现"这种缺失性证据才能发现问题

排查这类问题时，第一步应该是检查"验证函数用的是谁的视角"——是内核的全局
视角（旧假设），还是当前任务的私有视角（新现实）。不要被"两边都没崩溃"的
表象欺骗——崩溃是 bug 的朋友，静默失效是 bug 的伪装。

---

## 阶段 18 第一次尝试 (commit e978444，未推送)：系统卡死

**日期**：2026-07-13

**现象**：VNC 下系统彻底卡死（画面静止、鼠标光标不动），具体触发场景未
定位到精确操作。

**初步诊断**：
- 默认 build（无 DEV_BUILD）nographic 模式正常启动到 idle loop，串口输出
  "Shell: enabling interrupts (sti) + entering idle/scheduler loop..."
- DEV_BUILD 下全部自检（Phase 15/16/17）PASS，Phase 12 隔离验证预期 halt
- QEMU `-d int,cpu_reset -no-reboot -no-shutdown` 未记录任何意外异常或三重 fault
- 静态分析 `wm_mark_dirty()`、`wm_flush_dirty()`、`on_mouse_move()` 中所有
  循环均为有界循环（g_window_count ≤ 16、fill_rect 受 framebuffer 尺寸限制）

**定性结论**：不是 IF=0 下的死循环（draw_line 那种），也不是中断关闭下的
死锁。最可能是 **IRQ12 上下文中 `wm_flush_dirty()` 的帧缓冲操作链**
（mouse_cursor_hide → fill_rect → window_draw → on_redraw → calc_redraw
→ button_draw×16 → calc_refresh_display → mouse_cursor_show）在密集 PS/2
运动包下触发了某种边界条件。

**最可疑的代码位置**：
1. `wm_flush_dirty()` 在 IRQ12 上下文（IF=0）中调用 `mouse_cursor_hide/show()`，
   而 `mouse_process_packet()` 在同一次中断中已经做过 cursor_restore + cursor_draw
2. `calc_redraw()`（完整重绘计算器窗口 16 按钮+显示屏）被 `wm_flush_dirty()` 
   的窗口相交判定触发，在每次拖动帧中都可能执行
3. `wm_mark_dirty()` 的 framebuffer clamping 逻辑在边界坐标（窗口贴近屏幕边缘）
   下可能产生意料之外的 dirty rect

**处置**：已 `git reset --hard` 回滚到阶段 17 完成时的 commit (49d0c1b)。
commit e978444 未推送，不会被远程仓库引用。

**教训 — 流程层面**：
- 这次跳过方案讨论直接写代码，导致一个可能通过讨论就提前发现的问题
  （IRQ12 上下文里嵌套 cursor hide/show + calc 全量重绘）直到写完才暴露
- Damage tracking 这类涉及 IRQ 上下文、光标协调、多个绘制子系统交互的改动，
  必须在方案阶段就对每个新增循环的边界条件、调用深度、IF=0 下的时间开销
  做逐项确认，不能凭"都是 bounded loop"的直觉直接动手

**教训 — 技术层面**（待阶段 18 重新方案讨论时结合诊断结论一并考虑）：
- IRQ12 上下文里的帧缓冲操作总量需要更精确的建模（不只是像素数，还包括
  window_draw 内部 draw_line 的步数、calc_redraw 的 button_draw 调用量）
- `mouse_cursor_hide/show()` 在 `mouse_process_packet()` 已经做过 cursor
  save/restore 的前提下，是否可以在 `wm_flush_dirty()` 里省略
- 是否需要将 `wm_flush_dirty()` 的实际绘制工作推迟到 IRQ 上下文之外执行
  （例如只标记 dirty，在下一次 timer tick 或 idle 时执行 flush）
