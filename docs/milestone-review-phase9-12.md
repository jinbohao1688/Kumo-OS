# Kumo OS 里程碑回顾：阶段 9-12

**日期**：2026-07-12
**范围**：从 Framebuffer 到每任务独立页目录（进程隔离）

---

## 1. 技术栈清单（按依赖顺序）

```
阶段 9  Framebuffer 基础
  └── 阶段 10  2D 绘图原语 + 字体渲染
        └── 阶段 11a  抢占式调度
        └── 阶段 11b  PS/2 鼠标驱动
              └── 阶段 12  每任务独立页目录（进程隔离）
```

| 阶段 | 主题 | 解决的问题 | 稳定性 |
|------|------|-----------|--------|
| 9 | Framebuffer 基础 | 从纯串口输出跨越到图形显示——Multiboot2 framebuffer tag 解析 + MMIO 跨范围物理内存映射，使内核获得可写的像素帧缓冲 | 稳定。color_info 六字段核对正确，paging_map_phys_range 边界精确，纯色填充验证通过 |
| 10 | 2D 绘图原语 + 字体 | 在裸帧缓冲之上建立像素级绘图能力——put_pixel/draw_line/draw_rect/fill_rect 四个原语 + 自制 8×16 位图字体的 draw_string，无第三方数据依赖 | 稳定。9 采样点程序化验证全部命中，95 字符全字符集人工目视核对通过 |
| 11a | 抢占式调度 | 从协作式调度升级到 IRQ0 定时器驱动的抢占式调度——task_yield 加 cli 保护，switch_to 栈自描述性机制验证，250+ tick 无寄存器混叠 | 稳定。regtest_a/b 六寄存器标记验证 0 次 FAIL，Shell/ELF/VFS 功能回归全部通过 |
| 11b | PS/2 鼠标驱动 | 第一个外设中断驱动（非定时器）——PS/2 控制器 8 步初始化、IRQ12 接入（pushad 规格）、3 字节包状态机、光标 save/restore 显示 | 稳定。VNC 视觉验证通过，光标移动/按钮状态上报正常，颜色污染 bug 已修复 |
| 12 | 进程隔离 | 每任务独立页目录，硬件强制地址空间隔离——用户/内核物理内存区域分离（0x800000 分界）、按需 PT 克隆、CR3 在 switch_to 中切换、10 个分配点全部迁移至用户区域 | 稳定。probe_a 跨任务读触发 #PF ErrCode=0x05，精确验证隔离由硬件页保护强制执行 |

## 2. Bug 清单（阶段 9-12）

### Bug #1: `multiboot_tag_framebuffer_t.framebuffer_bpp` 字段类型错误

- **阶段**：9（Framebuffer 基础）
- **症状**：color_info 的六项字段（red/green/blue 的 field_position + mask_size）全部读到错位值，例如 `red_field_position` 读到的是 `green_mask_size` 的值
- **根因**：`framebuffer_bpp` 被定义为 `uint32_t`（4 字节），但 GRUB 源码中实际为 `uint8_t`（1 字节）。多出的 3 字节导致后续所有字段（framebuffer_type、reserved、red_field_position...）整体偏移 3 字节
- **为什么早期没发现**：纯色填充不依赖 color_info 字段，仅凭 framebuffer_addr/pitch/width/height/bpp 即可工作。如果到了阶段 10 字体渲染才需要根据 color_info 构造正确的 RGB 像素值，到那时颜色显示异常但程序不崩溃——会比当前更难定位
- **修复**：`framebuffer_bpp` 改为 `uint8_t`，结构体加 `__attribute__((packed))`。修复后逐字段核对 GRUB 源码偏移量，六项 color_info 值与 QEMU `-vga std` 32bpp 的已知固定值（red pos=16 mask=8, green pos=8 mask=8, blue pos=0 mask=8）全部吻合
- **经验分类**：外部规范定义的结构体，每个字段的类型和大小必须对照官方头文件源码逐字段核实，不能凭直觉判断。`__attribute__((packed))` 是必要防御，但前提是字段类型本身正确——packed 不会修复类型写错导致的偏移量错误

### Bug #2: `sti` 时机错误 → #PF（抢占式调度前置条件违反）

- **阶段**：11a（抢占式调度）
- **症状**：`sti` 放在 `paging_init` 和 VFS 初始化之间（比 `task_init()` 早约 60 行），定时器中断触发时 `g_current == NULL`，`task_yield()` 解引用 NULL → #PF
- **根因**：抢占式调度要求任务系统已初始化（`g_current` 指向有效 TCB），但 `sti` 在 `task_init()` 之前执行，期间任何一个 IRQ0 都会导致空指针解引用。"早开中断让硬件尽早工作"的直觉与"中断 handler 需要任务系统就绪"的前置条件冲突
- **修复**：将 `sti` 移到 idle 循环之前（`task_init()` 之后），确保中断首次触发时调度器已就绪
- **经验分类**：`sti` 的位置在抢占式调度系统中是关键决策点——原则是"中断开启的时刻 = 系统已准备好接收中断并正确处理它们的时刻"，不是"能开就尽早开"

### Bug #3: 鼠标光标 save/restore 颜色污染

- **阶段**：11b（鼠标驱动）
- **症状**：光标移动经过阶段 10 已绘制内容时，背景颜色被污染（例如青色对角线经过光标后变成绿色）
- **根因**：记录不完整。该 bug 在早期开发迭代中发现并通过截图证据确认存在，后续在会话中断前已修复（VNC 视觉验证通过：光标划过文字/矩形/青色对角线，背景完好无损）。但由于中间没有 commit，无法从 git 历史追溯到"哪一次具体的代码变更解决了该问题"
- **当前代码的 save/restore 机制**（逻辑上完整且对称）：
  - `cursor_save()`：逐像素读取光标 12×20 区域 → `cursor_backup[]`（通过 `get_pixel` 三路 32/24/16bpp）
  - `cursor_restore()`：将 `cursor_backup[]` 逐像素写回旧光标位置（通过 `put_pixel` 三路 32/24/16bpp）
  - `cursor_draw()`：save → fill_rect（白色内部）→ draw_rect（黑色边框）→ 更新 cursor_old
- **记录缺口说明**：由于会话中断时该 bug 的修复代码已在 working tree 中但未 commit，无法精确定位到是 cursor_save()、cursor_restore()、put_pixel() 还是 get_pixel() 中的哪一处具体改动解决了该问题。当前代码的 get_pixel ↔ put_pixel 对所有 bpp 路径一致且对称，推测修复可能涉及：get_pixel 初版未正确实现（如 pitch 计算错误、bpp 路径遗漏）、或 cursor_restore 的边界检查。此缺口作为"未及时 commit 导致上下文丢失"的教训，已在阶段 11b phase-notes 中记录
- **经验分类**：参见 phase-notes.md 阶段 11b 第一条——"每完成一个可验证的小阶段，应该及时 commit"

### Bug #4: `page_is_user_accessible` 硬编码内核 PD 导致 Shell 静默失败

- **阶段**：12（进程隔离）
- **症状**：Shell 任务拥有私有 PD 后完全无输出——无 banner、无 prompt、无崩溃。系统其他方面完全正常（tick 持续、其他任务输出正常、中断响应正常）
- **根因**：`page_is_user_accessible()` 硬编码使用 `pd_phys`（内核 PD）做页表遍历。Phase 12 之后，用户页仅在各任务的私有 PD 中标记为 U/S=1，内核 PD 中这些 PT 是共享的、不会被标记。当 `copy_from_user` → `is_user_accessible_range` → `page_is_user_accessible` 验证用户指针时，查询内核 PD 发现 U/S=0 → 验证失败 → syscall 返回 -1 → Shell 所有 syscall 静默失败
- **关键洞察**：两边各自没崩溃、但语义不匹配导致静默失效——内核正确地拒绝了"看起来没有 U/S 权限"的页面，Shell 正确地处理了 -1 返回值（跳过该操作）。这在各自语义下都是"正确"的，合在一起却是功能完全失效。这是整条主线一直强调要提防的 bug 类型
- **修复**：`page_is_user_accessible` 改为使用 `task_current()->cr3`（当前任务的私有 PD）做页表遍历。cr3==0 时回退到 `pd_phys`。修复后增加了不变量注释：`task_current()->cr3 == CPU 实际 %cr3`，该不变量在当前所有调用路径上成立（仅从 syscall_dispatch 到达）
- **经验分类**：引入 per-task 数据结构后，任何"遍历/校验用户可访问性"的函数都要重新检视是否隐含了"只有一个 PD"这个已经不再成立的假设。这类假设失效不会导致崩溃，只会导致功能安静地失效——比崩溃更难发现

### 确认：无遗漏

以上 4 个 bug 覆盖了阶段 9-12 中所有被实际发现、诊断并修复的具体问题。阶段 10 的"像素采样 vs 人工目视验证"是方法论经验（不是代码 bug），阶段 11a 的"中断门架构天然提供临界区保护"是架构洞察（不是 bug），阶段 12 的不变量验证是预防性分析（不是已发生的 bug）。详见 phase-notes.md 各阶段条目。

## 3. 当前技术债清单

### 已清偿

| 技术债 | 原始阶段 | 清偿阶段 | 说明 |
|--------|---------|---------|------|
| ADR-003：NULL 页可访问 | 3 | 8a | `paging_unmap_null_page()` 清除 PTE[0].P，空指针解引用 → #PF |
| ADR-004：用户态指针无校验 | 6 | 8a | `copy_from/to_user` + `page_is_user_accessible`，6 个 syscall 全部接入 |
| 共享页目录限制（ELF PIE-only） | 8b | 12 | 阶段 12 引入每任务独立 PD，共享 PD 的架构限制已解除。但当前仍保持 PIE-only（NASM PIC），因为"隔离"和"支持标准 p_vaddr"是两个正交问题——后者还需要链接脚本改为非零基址 + 可能的 .rela.dyn 重定位处理 |

### 从上一里程碑继承、仍未处理的

| 技术债 | 原始阶段 | 当前状态 |
|--------|---------|---------|
| kheap 跨页 coalescing | 8b | coalesce_forward/backward 仍限单页边界。当前用户程序缓冲区小（<4KB），尚未触发实际问题。exec 路径用 PMM 直接分配绕过了 heap。长期建议：buddy allocator 或 slab |
| RamFS 仅单层目录 | 6 | 不支持 `/a/b/c` 嵌套路径。当前所有文件在 `/` 下，够用。需要时改为树状 namei 解析即可 |
| 无磁盘驱动 | — | 文件系统完全在内存中，重启丢失。这是后续驱动阶段（ATA/AHCI + FAT32）的范畴 |
| 无用户程序编译工具链 | 8b | 用户程序需手写 NASM 汇编，不支持 C 用户程序。需要 i686-elf 交叉编译的 userspace runtime（crt0 + libc stub） |
| 32 位限制 | 1 | 不支持 >4GB 物理内存、不支持 64 位用户程序。暂无切换计划 |

### 阶段 11a-12 新识别、待处理的技术债

| # | 技术债 | 严重程度 | 说明 |
|---|--------|---------|------|
| 1 | 光标 save/restore 与抢占式调度的交互 | **已确认为安全，无需处理** | 见下方专项分析 |
| 2 | IRQ12/IRQ0 未来 framebuffer 并发风险 | 低（当前安全） | 当前 IRQ0 仅 tick++ 和 task_yield()，不操作 framebuffer。IRQ12 操作 framebuffer 时 IF=0（中断门），IRQ0 无法抢占。但如果后续在 IRQ0 中添加 framebuffer 操作（如时钟 widget），需要确认中断门保护仍然覆盖所有 framebuffer 访问路径。如果未来切换到 trap gate（允许中断嵌套），framebuffer 将成为需要显式锁定的共享资源 |
| 3 | 光标绘制缺少通用脏矩形（damage region）机制 | 中 | 当前仅 save/restore 单个 12×20 光标区域。如果未来有多个独立更新的 UI 元素（窗口、光标、菜单等），需要通用的 damage tracking + 合并重绘机制。当前的单光标 backup buffer 方案不扩展 |
| 4 | PMM 用户区域分配器无 free 接口 | 中 | `pmm_alloc_user_page()` 只有分配，没有对应的 `pmm_free_user_page()`。当前所有用户任务永久运行（无退出），但后续实现进程终止时需要释放用户物理页 |
| 5 | 缺少任务终止/资源回收机制 | 中 | 与 #4 相关。当前无法杀死任务、无法回收其私有 PD 和用户页。probe_a 触发 #PF 后只能 HALT 整个系统，无法实现"杀死单个任务、系统继续运行" |

#### 专项分析：光标 save/restore 的临界区保护

**问题**：阶段 11a 确认了 `task_yield()` 需要 `cli` 保护（因为 idle 循环路径 IF=1）。鼠标的 cursor_save/restore 操作在 IRQ12 (pushad 规格) 下执行——是否可能被 IRQ0 的 task_yield() 打断而导致 framebuffer 状态不一致？

**分析**：

IRQ12 处理流程：
```
irq12_entry (interrupt gate, IF=0)
  → irq_common_stub (pushad)
    → irq_handler (int_no=44)
      → mouse_handle_interrupt()
        → cursor_restore()    ← 读 cursor_backup[] + 写 framebuffer
        → cursor_draw()       ← cursor_save() 读 framebuffer + fill_rect + draw_rect
      → pic_send_eoi(IRQ12)
      → return (不做 task_yield，IRQ12 路径不触发调度)
    ← irq_handler 返回
  ← irq_common_stub: popad, add esp,8, iret → IF 恢复为 1
```

IRQ0 处理流程：
```
irq0_entry (interrupt gate, IF=0)
  → irq_common_stub (pushad)
    → irq_handler (int_no=32)
      → tick_count++
      → pic_send_eoi(IRQ0)
      → task_yield()         ← 可能 switch_to 到另一个任务
        → cli (IF 已为 0, 无实际效果)
        → switch_to
    ← irq_handler 返回
  ← ... → iret → IF 恢复为 1
```

关键保护链路：

1. **两个 IRQ 都使用中断门（type 0x8E）**：CPU 在进入时自动清 IF。IRQ12 执行期间 IF=0，IRQ0 无法抢占 IRQ12 的 cursor save/restore
2. **EOI 在 business logic 之后**：`pic_send_eoi` 在 `mouse_handle_interrupt` 返回后才发送，整个鼠标数据处理在 ISR 保护下完成
3. **IRQ12 路径不触发 task_yield**：只有 IRQ0（`int_no==32`）调用 task_yield()。IRQ12 处理完即返回，不会发生"在 cursor save/restore 中间切到另一个任务"
4. **即使 IRQ0 在 IRQ12 返回后立即触发**：IRQ12 的 iret 恢复 IF=1 后，如果 IRQ0 pending，会立即进入 IRQ0 handler。但此时 mouse_handle_interrupt 已完全结束，framebuffer 状态一致。IRQ0 handler 调用 task_yield() 切换到另一个任务时，光标 save/restore 的中间状态不存在

**结论**：**光标 save/restore 在 IRQ12 中断门下天然受 IF=0 保护，不需要额外的 cli 临界区**。当前架构下不存在"task_yield 打断 cursor_restore 导致 framebuffer 半绘制状态被另一个任务看到"的风险。

**注意**：这个安全性依赖于"中断门清 IF"的架构选择（阶段 2 的决策）。如果将来出于延迟考虑将某些 IRQ 改为 trap gate（不清 IF），则需要重新评估所有 framebuffer 访问的并发安全性。

## 4. 阶段 13（窗口系统与合成器）复杂度预估

### 与前序阶段的对比

| 维度 | 阶段 5 (Ring3) | 阶段 12 (进程隔离) | 阶段 13 (窗口系统) |
|------|---------------|-------------------|-------------------|
| 硬件支撑 | x86 特权级、TSS、iret 帧 | CR3、页表、MMU 硬件 page walk | **无**——纯软件架构 |
| 参考设计 | Intel/AMD 手册，业界标准实现 | Intel/AMD 手册，OSDev wiki | 有概念参考（X11、Wayland、Win32），但**无硬件强制约束指导设计** |
| 出错表现 | #PF、#GP——硬件立即报错 | #PF 0x05/0x04——硬件报错且定位精确 | **视觉异常**——颜色错、窗口叠放不对、闪烁——无硬件信号，只能人眼判断 |
| 不可逆性 | 高（页表写错可能静默破坏内存） | 高（CR3 切错导致任务看到错误内存） | 低（framebuffer 像素值错误可覆盖，无持久损害） |
| 正确性验证 | GDB 单步寄存器值 + iret 不 crash | Errcode 精确匹配 + 任务隔离测试 | **人眼目视——类似阶段 10 字体验证** |
| 单次修改→验证周期 | ~30s（编译+QEMU+GDB） | ~30s（编译+QEMU+串口输出） | ~30s（编译+QEMU+VNC viewer） |
| 主要风险类型 | 硬件机制理解错误 → crash | 硬件机制理解错误 → crash 或静默数据错误 | **架构设计错误 → 推到重来** |

阶段 13 与阶段 10（2D 绘图原语）有相似之处——都是"产出像素"——但阶段 10 的原语是**无状态**的（put_pixel 不记得上一次画了什么），而窗口系统是**有状态**的（窗口位置、Z 序、damage 列表都是随时间变化的状态）。这使得阶段 13 的正确性更难验证——一个窗口在"正确"位置，但你可能无法从静态截图中判断它是否按正确的 Z 序叠放。

### 核心复杂度来源

1. **数据所有权模型**：谁拥有窗口的像素缓冲区？
   - 方案 A：内核分配 + 用户任务通过 syscall 写入（安全但慢）
   - 方案 B：用户任务拥有 buffer，合成器读取（需要共享内存或 IPC）
   - 方案 C：用户任务直接写入 framebuffer 的裁剪区域（危险——无隔离）

2. **重绘触发模型**：
   - 轮询式：合成器定期全屏重绘（简单但浪费 CPU）
   - 事件驱动：窗口标记 dirty → 合成器合并 damage region → 最小重绘（复杂但高效）
   - 混合：事件驱动 + 周期性全屏刷新兜底（防闪烁）

3. **输入路由**：鼠标点击落在 (x,y)，需要从 Z 序顶向下查找第一个包含该坐标的窗口，将事件转发给对应任务。这要求每个任务有一个事件队列——这是 Kumo OS 目前没有的 IPC 机制

4. **与现有系统的交互面**：
   - 鼠标驱动（阶段 11b）：坐标输入源
   - 绘图原语（阶段 10）：合成器的像素输出工具
   - 调度器（阶段 11a）：合成器本身是一个任务，需要在"无新事件时 yield"
   - 进程隔离（阶段 12）：每个应用窗口对应一个用户任务，有独立的地址空间——窗口 buffer 不能直接被合成器任务读取（跨地址空间）

### 第 4 点展开：跨地址空间的 buffer 共享

这是阶段 13 与阶段 12 的**直接冲突点**：

- 阶段 12 的核心成果是**隔离**：任务 A 无法读任务 B 的内存
- 阶段 13 的核心需求是**共享**：合成器需要读取所有窗口的像素 buffer 来合成最终画面

有三种解决路径：

| 路径 | 实现方式 | 复杂度 | 对阶段 12 的影响 |
|------|---------|--------|-----------------|
| A: 内核合成 | 窗口 buffer 由内核分配，应用通过 syscall 绘制（类似 Linux DRM/DRI 的 kernel modesetting） | 中 | 不需要跨任务共享内存，隔离完全保留 |
| B: 共享内存映射 | 在合成器任务的私有 PD 中显式映射窗口 buffer 的物理页（`paging_set_user_accessible_for_task` 的扩展用法） | 中-高 | 需要在隔离模型中开一个受控的"洞"，设计上需仔细记录哪些页面被跨任务共享 |
| C: 内核态合成器 | 合成器作为内核线程运行，使用内核 PD，可以访问所有物理内存 | 低 | 合成器运行在 Ring0 有最高权限，bug 影响面大。但在教学 OS 上下文中是可接受的简化 |

### 建议的实现顺序

```
Phase 13a: 单窗口 + 内核态合成器（路径 C）
  ├── 定义一个 window_t 结构体（x, y, w, h, z, buffer, dirty）
  ├── 应用通过 syscall 创建窗口 → 内核分配 buffer → 返回 window_id
  ├── 应用通过 syscall 写入 buffer（类似 write(fd, buf, size)）
  ├── 合成器任务：遍历窗口列表，blit dirty 窗口到 framebuffer
  └── 验证：一个应用创建一个窗口，写入像素，合成器正确显示

Phase 13b: 输入路由
  ├── 鼠标点击 → 合成器确定目标窗口 → 发送事件到对应任务
  ├── 需要一个简单的事件队列（或至少一个 pending_event 字段在 TCB 中）
  └── 验证：点击不同窗口区域，正确任务收到事件

Phase 13c: Z 序 + 多窗口
  ├── 窗口叠放，合成器从底向上 blit
  ├── 支持 focus 切换（点击窗口将其提升到 Z 序顶部）
  └── 验证：两个应用的窗口重叠，Z 序正确

Phase 13d: Damage tracking 优化
  ├── 仅重绘 dirty region，而非整个窗口
  ├── dirty rect 合并（相邻/重叠的 dirty rect 合并为一个大 rect）
  └── 验证：窗口局部更新不触发全屏重绘，性能可感知提升
```

**推荐从路径 C（内核态合成器）起步**。理由：
- 这是三条路径中最简单的（零 IPC、零跨地址空间共享）
- 在 128MB QEMU VM + 单核 CPU 的教学场景下，Ring0 合成器的风险可控
- 如果将来需要更强的隔离（如支持第三方应用），可以平滑升级到路径 A（内核分配 + syscall 绘制，合成器仍在 Ring0）而无需改变应用接口
- 前序阶段已经熟悉了"内核拥有全部物理内存视图"的模型，没有认知切换成本

### 风险总结

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| 设计出错误的抽象（如 window_t 字段不够或过多） | 中 | 从最小 window_t（仅 x/y/w/h/buffer）开始，随需求增加字段，不提前设计 |
| 跨地址空间 buffer 共享与隔离模型冲突 | 中 | 起点用内核态合成器（路径 C），绕过问题而不是提前解决它 |
| 视觉 bug 难以自动化验证 | 中 | 每个子阶段结束时截图存档 + 人工目视核对（沿用阶段 10 方法论） |
| 性能（全屏重绘）在 QEMU 中可接受但真实硬件可能卡顿 | 低 | Damage tracking（13d）是性能优化，不影响功能正确性，可以后做 |
| 合成器任务的调度策略（何时 yield、何时 wakeup） | 低 | 初始用简单的"有事件时运行、无事时 yield"，类似当前 Shell 的 poll 模型 |

**总体复杂度评级**：与阶段 12 相当或略高（阶段 12 是"设计对 = 硬件保证对"，阶段 13 是"设计对 = 看着对，设计错 = 看着错"）。但与阶段 5 相比高出两级——阶段 5 的每一个步骤都有 Intel 手册的精确指导（GDT 描述符字段、TSS 格式、iret 栈布局），阶段 13 没有这种权威参考。

## 统计

- **阶段 9-12 开发周期**：~1 天（2026-07-11 ~ 2026-07-12）
- **commit 数**：7（framebuffer → 2D 原语 → 抢占式 → 鼠标驱动 → 鼠标文档补全 → 进程隔离 → 文档更新）
- **新增源代码文件**：~8（gfx/primitives.c, gfx/font.c, drivers/mouse.c, user/regtest_a.asm, user/regtest_b.asm, user/test_probe_a.asm, user/test_probe_b.asm）
- **发现并修复的 bug**：4 个（详见第 2 节）
- **清偿的技术债**：1 项（共享页目录限制，阶段 12）
- **新识别的技术债**：5 项（详见第 3 节），其中 1 项经专项分析确认安全、无需处理

## 文档体系（截至阶段 12）

```
docs/
  decisions.md                     — ADR-001~005 + 经验记录
  phase-notes.md                   — 各阶段踩坑记录与可迁移经验（阶段 1-12）
  PROGRESS.md                      — 开发进度（按阶段 checklist）
  milestone-review-phase0-8.md     — 阶段 0-8 里程碑回顾
  milestone-review-phase9-12.md   — 本文档
```
