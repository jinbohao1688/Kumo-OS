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

## ADR-005: ELF 加载器 PIE-only 方案（共享页目录架构限制 → 已由阶段 12 解除）

**日期**：2026-07-11（阶段 8b），状态更新 2026-07-12（阶段 12）

**原始决策**：ELF 加载器采用 PIE-only 方案（手工 NASM PIC，零重定位），
程序通过 `call/pop ebp; sub ebp, get_eip` 做 EIP discovery，一切数据寻址
通过 `[ebp + offset]` PC-relative。不使用 .rela.dyn 重定位表。

**原始理由**（阶段 8b）：所有任务共享同一 Page Directory（TCB 无 CR3 字段）。
两个非 PIE 程序若 vaddr 重叠，内存映射必然冲突。因此程序必须用
position-independent 方式加载——这不是简化选择，而是共享 PD 架构下的必然。

**状态更新（2026-07-12，阶段 12）**：阶段 12 已引入每任务独立页目录。
每个任务拥有私有的 PD，用户态 PDE/PTE 按需克隆，物理页面通过用户/内核
区域分离（0x800000 分界）保证不重叠。**共享页目录的限制已解除**——从技术
上讲，现在每个任务可以拥有独立的虚拟地址空间，非 PIE 程序加载到各自的
固定 vaddr 不会产生冲突。

**当前仍保持 PIE-only 的原因**：隔离（每任务独立地址空间）和"支持任意加载
地址（标准 p_vaddr）"是两个正交问题。阶段 12 只解决了前者。支持标准 ELF
p_vaddr 还需要：
1. 链接脚本改为非零基址（如 0x08048000，传统 Linux i386 用户空间基址）
2. 确保不同程序的 p_vaddr 段不重叠，或实现地址空间布局随机化
3. 可能需要实现 .rela.dyn 重定位处理

当前所有用户程序（shell、test_*、hello_elf）均为手工 NASM PIC，运行良好。
切换到标准 p_vaddr 的收益（可运行 GCC 编译的标准 ELF）在当前阶段不迫切，
待后续有明确的"加载外部编译的 C 用户程序"需求时再实现。

## 决策-006: 光标 save/restore 与窗口重绘的时序交互 — 单窗口阶段暂不处理

**日期**：2026-07-12（阶段 13a）

**背景**：阶段 11b 的鼠标光标使用 save/restore 机制（cursor_save 读取
framebuffer 像素 → cursor_draw 绘制白色箭头 → cursor_restore 恢复背景）。
此机制不关心屏幕像素来源（测试图案或窗口内容），在单窗口阶段工作正常，
因为窗口内容在 `sti` 之前一次性绘制，之后不再变化。

**决策**：单窗口阶段（13a）**不处理**光标与窗口重绘的时序交互，包括：
- 光标 save 时窗口正在被重绘
- 窗口重绘覆盖光标区域导致光标"消失"
- 多个绘制源（窗口重绘 + 光标更新）的 damage region 合并

**理由**：上述所有问题只在窗口内容发生变化时才出现（窗口移动、大小改变、
内容更新、Z 序变化导致遮挡区域暴露）。单窗口阶段窗口是静态的，无重绘
事件，光标 save/restore 与窗口内容的交互等价于与阶段 10 测试图案的交互。

**后续动作**：在多窗口/可拖动窗口/Z 序合成子阶段（13c+），必须重新设计
framebuffer 写入的协调机制。预期方案：所有 framebuffer 写入路径（窗口
重绘、光标更新）先检查 cursor_visible 区域 → cursor_restore → 执行写入 →
cursor_draw。更通用的方案是引入 damage region 机制，将光标区域作为一个
独立的 damage rect 参与合并。

**重要**：这是一个**主动简化**，不是"已验证过没问题"的结论。当前阶段
依赖的前提（窗口静态、sti 前一次性绘制）会在后续子阶段被推翻。

## 决策-007: wm_draw_all() 在 IRQ12 上下文同步执行 — 已知延迟，暂不解决

**日期**：2026-07-12（阶段 13c）

**背景**：阶段 13c 引入多窗口 Z 序管理。鼠标点击 → `wm_handle_click()`
→ `wm_draw_all()` 的完整调用链运行在 IRQ12 的中断上下文（IF=0）。
`wm_draw_all()` 执行全屏桌面背景填充 + 所有窗口 bottom→top 重绘，
总像素量约 1.15M（1024×768 桌面 + 3 个 400×300 窗口）。

**耗时估算**（QEMU TCG 模拟，无 KVM）：

| 操作 | 像素量 |
|------|--------|
| 桌面背景 fill_rect | ~786K |
| 3 窗口 body fill | ~324K |
| 3 窗口标题栏 fill | ~24K |
| 3 窗口边框 draw_rect | ~15K |
| 标题 draw_string | ~4K |
| **合计** | **~1.15M put_pixel** |

单次 `put_pixel()` 在 QEMU TCG 中约几十到几百条宿主机指令。
保守估计全量重绘需 **数百毫秒到 1 秒**。

**PIT 与中断时序影响**：当前 PIT 频率 18.2 Hz（BIOS 默认，未重编程），
tick 周期 ≈ 55ms。一次 `wm_draw_all()` 会延迟 10~20 个 tick。

**但中断不丢失**。8259 PIC 对 CPU 的 INTR 信号是电平触发（level-sensitive）：
IF=0 期间 PIC 持续维持 INTR 有效；IRQ12 的 `iret` 恢复 IF=1 后，CPU
立即响应排队的 IRQ0。tick 计数落后一截，但 PIT 硬件自由运行，时间基准
不漂移。

**决策**：当前阶段**不解决**此延迟问题。理由：

1. KumoOS 无实时性需求，点击时调度延迟几百毫秒在当前开发阶段可接受
2. 阶段 13d（damage tracking）将全屏 ~1M 像素缩小到脏矩形 ~100K 像素，
   重绘耗时自动降一个数量级，问题自然缓解
3. 阶段 13c 只有 3 个 demo 窗口，手动点击频率极低，用户感知不到

**备选方案（3 行兜底）**：如果 damage tracking 之前就需要解决，只需：

```c
/* IRQ12 回调中设置 flag，不在中断上下文重绘 */
static volatile int wm_needs_redraw = 0;

/* idle loop 中检查并执行（IF=1，不阻塞中断） */
if (wm_needs_redraw) { wm_draw_all(); wm_needs_redraw = 0; }
```

IRQ12 回调改为 `wm_needs_redraw = 1;` 即返回，重绘在 idle loop 中以
IF=1 执行，完全解耦中断延迟与重绘开销。3 行改动，不涉及架构调整。

**触发条件**（何时需要采取备选方案）：
- 窗口数量增加导致重绘时间超过 100ms，或
- 引入窗口拖动（motion 事件频率远高于 click），或
- 出现计时相关 bug 追溯到 IRQ0 延迟

**后续动作**：阶段 13d（damage tracking）实现后，全量重绘次数大幅减少，
此问题自动降级。如 damage tracking 后仍有关键路径触发全屏重绘，再评估
是否引入 deferred redraw。

## 决策-008: 按钮 pressed 态视觉不跟随光标移动实时更新

**日期**：2026-07-12（阶段 14）

**背景**：按钮按下（`g_pressed_button != NULL`）后，光标移出按钮范围时，
多数 GUI 系统会将按钮视觉恢复为 idle 色（hover-leave 效果），移回时
重新显示 pressed 色。这能提供"按住不放但反悔了"的直观反馈。

**决策**：当前阶段不做移动中的动态视觉更新。按下后保持 pressed 色直到释放，
无论光标移动到哪里。

**理由**：

1. **缺少 motion callback 机制**：当前 `g_mouse_click_callback` 仅在按钮
   状态变化（按下/释放）时触发，不生效率。移动事件（dx/dy packet）不调用
   此回调。要实现动态视觉反馈，需新增 `g_mouse_move_callback`，这本身
   是一个独立的小设计——移动回调频率远高于点击回调（每个 PS/2 packet 都
   携带 dx/dy），且需在回调中做按钮 hit test。
2. **无法在自检中验证**：当前所有验证通过 `sti` 前直接调用 `on_mouse_click()`
   来模拟，移动事件无法模拟。即使实现了动态反馈，也无法在串口自检中验证
   其正确性。
3. **逻辑正确性不依赖视觉**：按钮状态机（释放位置决定触发/取消）与视觉
   是否跟随光标移动是两个正交问题。`g_pressed_button` 的语义只取决于
   按下/释放两个时间点的 hit test 结果，中间的视觉状态不影响最终行为。

**后续接入方案**（需要时）：在 `mouse.h` 新增 `mouse_move_callback_t` 类型
和 `g_mouse_move_callback` 变量（与 `mouse_click_callback` 同级）；
`mouse_process_packet()` 在移动分支（dx != 0 || dy != 0）调用此回调。
button 模块注册此回调，在回调中根据 `g_pressed_button` 状态和当前光标位置
动态调用 `button_draw()` 切换颜色。改动量约 20 行，不涉及架构调整。
