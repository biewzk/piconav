# Mapsforge 渲染器阅读指南

> 配套代码：`Mapsforge/`（mf_map.c / mf_theme.c / mf_render.c，共约 2800 行）。
> 本文分四部分：**学习路线图** → **`.map` 文件格式参考** → **调试工具箱** → **毕业自测**。
> 文件格式章节与 `mf_open`/`parse_block` 的解析顺序逐字段对齐。

## 心理预期

真正难的只有约 **400 行**（海岸线洪泛 + 扫描线填充），其余一半是查表（mf_theme.c）、一半是胶水代码。每个阶段以一个**可验证的实验**收尾——sim 的 headless + PPM dump 让你 3 秒一次迭代，这是本项目最好的学习仪器。

---

# 第一部分：学习路线图

## 阶段 0：先看现象，不看代码（半天）

1. 读 `README.md` 的「Mapsforge 离线地图引擎」章节（含渲染管线图与洪泛算法设计史）。
2. 跑起来玩：

   ```bash
   cd sim && ./build_sim.sh run          # 默认 monaco.map, 有真实海岸线
   MF_MAP=../resources/macau.map ./build_sim/bin/pico_nav_sim
   ```

3. 重点观察：平移时左上角信息条的 `obj 数 / 渲染耗时`；海陆交界；道路交叉口的壳覆盖效果；启动时终端的 `map ... opened` / `subfile[i]` 日志（见第三部分日志参考）。

## 阶段 1：只读数据结构（mf_map.h，157 行）

**OSM 数据模型一分钟**：地图世界由三种元素构成——Node（一个经纬度点）、Way（一串有序点）、Relation（元素组合）。文件里：

- POI = 一个 Node + 标签；
- Way = 一串增量坐标解码出的点串 + 标签；
- **闭 Way**（首尾节点重合）可当多边形面用，但最终还是由标签决定画成线还是面（环形路是闭合的但仍按线画）；
- 标签形如 `"key=value"`，对象里只存 ID，指向文件头部的两张标签表（POI 表 / Way 表）。

渲染器的一切输入就是一个 `mf_frame_t`。把这几个结构画在纸上：

```
mf_map_t        → 文件句柄 + 子文件表(zoom 区间) + 两张标签表
mf_obj_t        → POI(一个点) 或 Way(一串点), 附标签 ID 和名字
mf_water_tile_t → "这整块瓦片都是海" (来自索引水位标志)
mf_frame_t      → objs[] + water_tiles[]     ← 每帧的全部世界
```

配套概念：`.map` 按 zoom 分 subfile，每个 subfile 是 base_zoom 瓦片网格，索引 5 字节/瓦片。**不必**读懂解析细节的 900 行代码，只要相信契约：

```
mf_view_bbox(view) → mf_query(bbox) → mf_render(frame) → mf_frame_free()
```

（调用点见 `App/Pages/page_livemap.c:63-82`；注意没有瓦片缓存，每次视口变化都完整重走一遍。）

**核心名词分工**（四个名字各司其职，不要混淆）：

> **frame** 是"画什么"（查询结果）→ **view/viewport** 是"怎么看"（世界窗口→设备视口的变换状态）→ **theme** 是"长什么样"（标签→样式）→ **canvas** 是"画在哪"（LVGL 画布）。
>
> viewport 这个名字取自经典图形学的 window→viewport 变换对：center+zoom 定义世界窗口，w×h 定义设备视口。它不是 3D 意义上的 camera——没有朝向、没有透视，所以不叫 camera 是对的。

## 阶段 2：视口与投影（mf_render.c:778-861，约 80 行纯数学）

`mf_view_init / pan / bbox / project`，零依赖、无状态纠缠。带着三个问题读：

- 经度为什么是线性缩放、纬度为什么要过墨卡托公式？（墨卡托：x=k·λ 线性，y=k·ln(tan(π/4+φ/2)) 非线性拉伸，全球映射为正方形——瓦片分片的根基）
- 为什么存在 `project_d` 双精度版？——提示：海岸线屏障若取整到像素会断裂渗水；
- `mf_view_bbox` 里 Mercator y 为何 clamp 到 [0,1]？（防 `exp()/tan()` 溢出）

顺带理解 `mf_open` 里那个 `strcmp(projection, "Mercator")` 校验：投影名声明的是整个文件的坐标解释方式，是所有渲染数学的前提假设。遇到未知投影宁可拒绝加载（`MF_ERR_HEADER`），也不能画出一幅整体扭曲的鬼图。

https://github.com/mapsforge/mapsforge/blob/master/docs/Specification-Binary-Map-File.md

实验：`SIM_LAT/SIM_LON/SIM_ZOOM` 各改一档，对照像素坐标心算验证。

## 阶段 3：样式层 mf_theme.c（460 行，最好读）

全部是"命中即返回"的 if 链 + 静态表，没有算法。抓住三个机制：

1. `classify_way` 的优先级顺序（水系→绿地→landuse→建筑→步行广场→线→23 级道路表）——顺序即语义；
2. rank 的双重身份：面内 z 序 + 线排序键 `(layer+5)*100+rank`；
3. `mf_zoom_width` 的 Q4/Q8 定点查表（目标平台无 GPU、FPU 弱，故不用浮点）。

调色板唯一来源是 `mf_theme.h`（渲染器的背景/网格/光晕色也在这里），改一处全局生效。

实验：随手把 `C_MOTORWAY` 改成红色重编译，30 秒看到反馈闭环。

## 阶段 4：主流程骨架 mf_render()（mf_render.c:927 起）

第一遍读时**把 `fill_coastline()` 当黑盒**，只记 pass 顺序（后画覆盖先画）：

```
网格底 → 陆地底 → 索引水瓦片 → 海矩形 → [海岸线黑盒]
→ nosea/内陆水 → 四组面 → 排序后的两遍线 → 四类标注 → 无数据区修补
```

两个关键问题：为什么标注要分四轮且地名最先？为什么最后还要"修补" bbox 外区域？（答案都在注释里）

实验：

```bash
SDL_VIDEODRIVER=dummy SIM_AUTOQUIT_MS=1500 SIM_DUMP_PPM=/tmp/on.ppm \
    MF_MAP=../resources/macau.map ./build_sim/bin/pico_nav_sim
# 加 MF_NOCOAST=1 再跑一次 dump /tmp/off.ppm, 对比两图
# 开关前后差异 = 洪泛算法的全部产出
```

## 阶段 5：绘制原语（mf_render.c:146-260，约 90 行）

- `fb_fill_polygon`（mf_render.c:146）：经典 even-odd 扫描线填充。拿一个 5 顶点凹多边形在纸上手算某行的交点序列，再对代码逐行走一遍（半开区间规则、插入排序）。注意它**直写帧缓冲**绕过 LVGL 绘制系统——性能关键决定；
- `way_to_pixels`（mf_render.c:201）：投影 + <1.5px 抽稀，理解"屏幕距离"作为抽稀依据的含义。

## 阶段 6：最终 Boss —— fill_coastline（mf_render.c:346 起，约 250 行）

留到最后，且**先读 README 里"为什么用洪泛而非拼环"的历史**（旧方案在低 zoom 会产生大块伪陆地）。然后按五步走：

1. 收集 coastline way 成链；
2. 端点匹配合并（`e6_near` ±2 微度 ≈ 亚米级，支持 4 种拼接方向）；
3. 闭合链 = 岛屿 → 直接填陆色；
4. 其余画成 1-bit 屏障位图（Bresenham 粗圆头线）;
5. 从已知海域瓦片四角 BFS 洪泛 → visited 即海。

杀手锏实验——直接看算法中间产物：

```bash
MF_DUMP_COAST=1 ... ./build_sim/bin/pico_nav_sim
# 得到 /tmp/coast_barrier.pbm 和 /tmp/coast_visited.pbm, 用 GIMP 打开
```

"屏障挡住洪水"瞬间从抽象描述变成看得见的东西。

---

# 第二部分：`.map` 文件格式参考

## B.1 物理布局总览

```shell
+======================================================================+
|                     .map MAP FILE – PHYSICAL LAYOUT                  |
+======================================================================+
|  OFFSET 0x00                                                         |
|  +----------------------------------------------------------------+  |
|  |  MAGIC (20 bytes): "mapsforge binary OSM"                      |  |
|  +----------------------------------------------------------------+  |
|  |  HEADER_SIZE (4 bytes, BE)                                     |  |
|  |  → total bytes of the following header structure               |  |
|  +----------------------------------------------------------------+  |
|                                                                      |
|  +---------------- HEADER (variable length) -----------------------+ |
|  |  version (4 bytes, BE)    |  file_size (8 bytes, BE)           |  |
|  |  map_date (8 bytes, skip) |  bbox (4 x 4 bytes)                |  |
|  |  tile_size (2 bytes, BE)  |  projection (string) = "Mercator"  |  |
|  |  flags (1 byte)           |  [optional start_pos] (if bit 6)   |  |
|  |  [optional start_zoom]    |  [optional lang/comment/creator]   |  |
|  |                            |  (strings, if bits 4,3,2 set)     |  |
|  |  POI tags: count (2 bytes) + each as a string                  |  |
|  |  Way tags: count (2 bytes) + each as a string                  |  |
|  |  subfile count (1 byte)                                        |  |
|  +----------------------------------------------------------------+  |
|                                                                      |
|  +--- SUBFILE #0 METADATA (repeated for each subfile) ------------+  |
|  |  base_zoom (1 byte)  |  zoom_min (1 byte)  |  zoom_max (1)     |  |
|  |  start_addr (8 bytes, BE)  |  sub_file_size (8 bytes, BE)      |  |
|  +----------------------------------------------------------------+  |
|                                                                      |
|  +--- SUBFILE #0 TILE INDEX --------------------------------------+  |
|  |  Each entry: 5 bytes (40 bits)                                 |  |
|  |  [bit39: water flag]  [bits38..0: block offset in file]        |  |
|  |  Number of entries = blocks_width * blocks_height              |  |
|  |  (computed from bbox and base_zoom)                            |  |
|  +----------------------------------------------------------------+  |
|                                                                      |
|  +--- SUBFILE #0 DATA BLOCKS -------------------------------------+  |
|  |  Serialized POI and Way objects, stored per tile               |  |
|  |  Offsets are referenced by the index entries                   |  |
|  +----------------------------------------------------------------+  |
|                                                                      |
|  +-- (Optional) 16-byte debug section before each subfile data ---+  |
|  |  (only if flags & 0x80, i.e. is_debug_file = true)             |  |
|  +----------------------------------------------------------------+  |
|                                                                      |
|  ... (SUBFILE #1 .. #N) ...                                          |
|                                                                      |
|  End of file – no global string pool; strings are stored inline      |
|  inside the header and tags are already resolved during open.        |
+======================================================================+
```

要点：

- 多字节整数一律**大端**；
- 块偏移相对**子文件起点** `start_addr`；
- 渲染查询某块的大小时，从当前索引条目偏移读到**下一个非空条目**偏移（空条目跳过，末块到子文件末尾）——这是 `mf_query` 里最容易踩的坑；
- 瓦片行列由地图 bbox 反算：`boundary_tile_*` 与 `blocks_width/height` 在 `mf_open` 时算好。

## B.2 mf_open 解析流程（mf_map.c:224 起）

1. `memset(m, 0)`，`fopen(path, "rb")`，`fseeko/ftello` 取得 `m->file_size`。
2. 从偏移 0 读 24 字节：

   ```
   ┌──────────────────────────────────────────────────┐
   │ bytes[0..19] = MF_MAGIC ("mapsforge binary OSM") │
   │ bytes[20..23] = header_size (BE uint32)          │
   └──────────────────────────────────────────────────┘
   ```
   校验魔数与 header_size 范围（70 .. 2MB）。

3. 分配 `hdr = malloc(header_size)`，从偏移 24 读入头部，初始化 `mf_reader_t`。
4. 按序解析：

```shell
┌─────────────────────────────────────────────────────────────┐
│ version        (4 bytes BE) → m->version                    │
│ file_size      (8 bytes BE) → verify against ftello         │
│ map_date       (8 bytes, skip)                              │
│ bbox minLat/minLon/maxLat/maxLon (4 x 4 bytes BE, 微度)      │
│ tile_size      (2 bytes BE) → m->tile_size                  │
│ projection     (string)     → must equal "Mercator"         │
│ flags          (1 byte)     → m->flags                      │
│                                                             │
│ if (flags & 0x40) { start_pos.lat_e6 + lon_e6 (4B BE ×2) }  │
│ if (flags & 0x20) { start_zoom (1 byte) }                   │
│ if (flags & 0x10) { string (language, skip) }               │
│ if (flags & 0x08) { string (comment, skip) }                │
│ if (flags & 0x04) { string (created_by, skip) }             │
│                                                             │
│ POI tags:  uint16 n → m->poi_tag_cnt                        │
│            for i in n: string → m->poi_tags[i]              │
│ Way tags:  uint16 n → m->way_tag_cnt                        │
│            for i in n: string → m->way_tags[i]              │
│                                                             │
│ subfile_cnt (1 byte, 1..16) → m->subfile_cnt                │
│                                                             │
│ for each subfile i:                                         │
│   base_zoom (1)  zoom_min (1)  zoom_max (1)                 │
│   start_addr (8 bytes BE)  sub_file_size (8 bytes BE)       │
└─────────────────────────────────────────────────────────────┘
```

5. 对每个子文件计算瓦片边界与索引范围：
   - `index_start_addr = start_addr + (is_debug_file ? 16 : 0)`
   - `boundary_tile_*` 由 bbox 与 base_zoom 经 lat/lon→tile 函数求得
   - `blocks_width/height`、`num_blocks`
   - `index_end_addr = index_start_addr + num_blocks * 5`
6. `free(hdr)`，成功打日志返回 `MF_OK`；任何失败走 `fail:` 打 `LOG_E` 并返回错误码。

### FLAGS 位定义（1 字节）

```shell
┌───────┬──────────────────────────────────────────────┐
│ Bit 7 │ is_debug_file (0x80) – skip 16 bytes before  │
│       │ each subfile's data blocks                   │
├───────┼──────────────────────────────────────────────┤
│ Bit 6 │ optional start position (0x40) – lat+lon     │
├───────┼──────────────────────────────────────────────┤
│ Bit 5 │ optional start zoom (0x20)                   │
├───────┼──────────────────────────────────────────────┤
│ Bit 4 │ optional language preference string (0x10)   │
├───────┼──────────────────────────────────────────────┤
│ Bit 3 │ optional comment string (0x08)               │
├───────┼──────────────────────────────────────────────┤
│ Bit 2 │ optional created-by string (0x04)            │
├───────┼──────────────────────────────────────────────┤
│ Bits  │ reserved (must be 0)                         │
│ 1-0   │                                              │
└───────┴──────────────────────────────────────────────┘
```

可选字段是否出现完全由位决定，且**按位序从高到低依次紧排**在 flags 之后——读取方按固定顺序逐位判断即可，无需额外偏移表。

## B.3 子文件元数据与瓦片索引计算

```shell
For each subfile (mf_subfile_t):
┌───────────────────────────────────────────────────────────────┐
│ base_zoom    : the zoom level used for tile numbering         │
│ zoom_min     : lowest zoom covered by this subfile            │
│ zoom_max     : highest zoom covered by this subfile           │
│ start_addr   : absolute file offset where this subfile begins │
│ sub_file_size: size of this subfile (metadata + index + data) │
└───────────────────────────────────────────────────────────────┘

Computed from bbox and base_zoom:
   left_tile   = floor( lon_to_tile_x(bbox_min.lon, base_zoom) )
   right_tile  = floor( lon_to_tile_x(bbox_max.lon, base_zoom) )
   top_tile    = floor( lat_to_tile_y(bbox_max.lat, base_zoom) )
   bottom_tile = floor( lat_to_tile_y(bbox_min.lat, base_zoom) )

   blocks_width  = right_tile - left_tile + 1
   blocks_height = bottom_tile - top_tile + 1
   num_blocks    = blocks_width * blocks_height

   index_start_addr = start_addr + (is_debug_file ? 16 : 0)
   index_end_addr   = index_start_addr + num_blocks * 5

   (Tile index entries are stored sequentially in row-major order:
    row = ty - top_tile, col = tx - left_tile,
    pos  = index_start_addr + (row * blocks_width + col) * 5)
```

## B.4 内存中的 mf_map_t 与 mf_subfile_t

```shell
+==============================================================+
|                      mf_map_t (in-memory)                    |
+==============================================================+
|  .fp            FILE*                                        |
|  .file_size     int64_t                                      |
|  .version       int32_t                                      |
|  .tile_size     int32_t                                      |
|  .flags         int32_t                                      |
|  .start_zoom    int32_t   (-1 if not present)                |
|  .start_pos     mf_coord_t                                   |
|  .is_debug_file bool                                         |
|  .bbox_min / .bbox_max                                       |
|  .poi_tag_cnt   int32_t   → .poi_tags[] (char**)             |
|  .way_tag_cnt   int32_t   → .way_tags[] (char**)             |
|  .zoom_min / .zoom_max  (global, min/max over all subfiles)  |
|  .subfile_cnt   int32_t                                      |
|  .subfiles[0..15]   mf_subfile_t array                       |
+==============================================================+

   .subfiles[i] (mf_subfile_t)
   +------------------------------------------------------+
   |  base_zoom, zoom_min, zoom_max                       |
   |  start_addr, sub_file_size                           |
   |  index_start_addr, index_end_addr                    |
   |  boundary_tile_left/right/top/bottom                 |
   |  blocks_width, blocks_height, num_blocks             |
   +------------------------------------------------------+
```

标签字符串作为单独的 malloc 副本存放在 `.poi_tags[]` / `.way_tags[]` 中，打开时一次性解析完毕。

## B.5 数据块放大图（parse_block 的解析顺序，mf_map.c:607 起）

```shell
+----------------------------------------------------------------+
| [debug file : 32-B block signature]                            |
| zoom table  : (zoom_max - zoom_min + 1) rows x 2 cols VBE-U    |
|               cumulative (POI, ways) counters;                 |
|               row @query zoom => visible object counts         |
| first_way_offset (VBE-U) <- rel. offset of the Way section     |
|                                                                |
| --- POI section : repeat pois_on_zoom times -----------------  |
|   [debug file : 32 bytes]                                      |
|   lat_delta VBE-S -+                                           |
|                    +-> absolute micro-degrees (vs tile corner) |
|   lon_delta VBE-S -+                                           |
|   special byte    : hi nibble = layer+5, lo nibble = #tags     |
|   tag IDs x n     : VBE-U -> header POI tag table              |
|   feature byte    :                                            |
|       bit7 name(str)   bit6 house(str)   bit5 elevation(VBE-S) |
|                                                                |
| --- WAY section : repeat ways_on_zoom times -----------------  |
|   (seek to first_way_offset)                                   |
|   [debug file : 32 bytes]                                      |
|   way_data_size    : VBE-U                                     |
|   subtile bitmap   : u16                                       |
|   special byte     : same as POI                               |
|   tag IDs x n      : VBE-U -> header way tag table             |
|   feature byte     :                                           |
|       bit7 name(str)     bit6 house(str)     bit5 ref(str)     |
|       bit4 label position (2x VBE-S)                           |
|       bit3 explicit #way-blocks (VBE-U), default 1             |
|       bit2 double-delta encoding flag                          |
|   way blocks x n {                                             |
|       coord blocks x m {                                       |
|           node_count VBE-U  (2..4096)                          |
|           first node: delta vs tile corner, VBE-S x2           |
|           next nodes: delta vs prev node,   VBE-S x2           |
|               (one more delta layer if bit2 set)               |
|       }                                                        |
|   }                                                            |
+----------------------------------------------------------------+
```

POI是一个点位，一个点位有多个属性，比如，他是一个餐厅，它支持无障碍通行.同时，点位有图层的概念，会根据图层决定渲染顺序

解码完成后按视口 bbox 过滤，幸存者进入 `mf_frame_t`；`way_is_closed` 由首尾节点相等判定。

## B.6 基础编码原语（mf_map.c:21-135）

| 原语 | 编码 | 备注 |
|------|------|------|
| 定长整数 | 大端 | u16/i32/u64 |
| VBE-U | 每字节低 7 位数据、最高位为续位 | 最多 5 字节 |
| VBE-S | 同 VBE-U，但**末字节只 6 位数据 + bit6 符号位** | 超 10 字节判损坏 |
| 字符串 | VBE-U 长度 + UTF-8 字节 | |
| 标签 ID | VBE-U，索引头部标签表 | 若表值形如 `%b/%h/%i/%f/%s`，其值字节紧跟在对象流中内联出现 |

坐标全程使用**微度**（1e-6 度）int32，增量解码基于"首点相对瓦片角、后续点相对前点"，因此任何一字节错位都会让整条 way 飘走——阅读时留意 `mf_reader_t` 的越界保护。

## B.7 查询工作流（mf_query，mf_map.c:918 起）

bbox -> Bounding Box
```
mf_query(zoom, bbox, frame)
        |
        v
1. 按 zoom 查找覆盖它的子文件 (zoom_min <= zoom <= zoom_max)
   找不到 → 返回 MF_ERR_NO_SUBFILE
        |
        v
2. 把 bbox 换算成该子文件 base_zoom 下的瓦片范围, 裁剪到边界瓦片内:
     tx_min/max = floor(lon_to_tile_x(...))
     ty_min/max = floor(lat_to_tile_y(...))   注意 y 轴方向相反
        |
        v
3. 遍历范围内每个瓦片 (tx, ty):
     row = ty - boundary_tile_top
     col = tx - boundary_tile_left
     读 5 字节索引条目 → 拆出水位标志(bit39)和块偏移(bits38..0)
        |
        +---> water flag == 1 → 记入 frame.water_tiles (去重)
        |
        +---> 有数据 → 块大小 = 下一个非空条目偏移 - 当前偏移
                        (空条目跳过, 末块到子文件末尾)
                        v
              4. 读整块 → parse_block 反序列化:
                   - POI: 坐标增量、海拔、标签 ID、名称
                   - Way: 节点增量解码、闭合判定
                 按视口 bbox 过滤后压入 frame.objs
        |
        v
5. 返回 MF_OK; frame 同时携带可见对象与全水域瓦片
```

## B.8 livemap 重绘流程（App/Pages/page_livemap.c:63-82）

```shell
用户触发重绘 (拖拽 / 缩放 / GPS 跟随)
         |
         v
 记录开始时间 t0
         |
         v
 mf_view_bbox: 由视口中心+zoom 计算经纬度 bbox (含 8% 边距)
         |
         v
 mf_query 获取可见对象
         |
    +----+----+
    | 查询失败? |
    +----+----+
     (是)|  (否)
         v     v
   显示错误   mf_render 绘制到画布
   清空画布      |
   释放 frame    v
   返回      计算耗时, 更新信息条标签, 通知 LVGL 重绘
                 |
                 v
           mf_frame_free (结束)
```

---

# 第三部分：调试工具箱

## 实验命令速查

```bash
cd sim && ./build_sim.sh                       # 构建 (需 libsdl2-dev)

# 无头冒烟 + 截帧
SDL_VIDEODRIVER=dummy SIM_AUTOQUIT_MS=1500 \
    SIM_DUMP_PPM=/tmp/sim.ppm ./build_sim/bin/pico_nav_sim sample.nmea

# 脚本化按键
SIM_KEY="right,right,enter,down,back" SIM_KEY_MS=300 ...

# 选图选视口
MF_MAP=../resources/macau.map SIM_LAT=22.1935 SIM_LON=113.5451 SIM_ZOOM=14 ...

# 渲染器调试开关
MF_DEBUG=1        # 帧对象数/线数日志 (fprintf, 不走 syslog)
MF_NOCOAST=1      # 关海岸线洪泛(退回瓦片海陆)
MF_DUMP_COAST=1   # 导出屏障/visited 位图到 /tmp/*.pbm
```

## 渲染器日志参考

日志统一走 `App/Utils/log`：真机进 syslog，模拟器镜像到 stderr（`LOG_CONSOLE=0` 可关）。输出格式 `[pico_nav][级别] 消息`，级别 E/W/I/D。

| 级别 | 位置 | 内容 |
|------|------|------|
| `LOG_I` | `mf_open` 成功 | 路径、版本、子文件数、zoom 范围、tile 大小、标签数、bbox |
| `LOG_E` | `mf_open` 失败 | 错误码 + 可读原因（bad magic / bad header / io / out of memory） |
| `LOG_D` | 头部可选字段 | 起点 经纬度 / 起点 zoom / header_size |
| `LOG_I` | 头部解析完 | `header: tile_size=256 projection=Mercator flags=0x7c poi_tags=68 way_tags=140` |
| `LOG_W` | 调试文件 | 提示将跳过 16/32 字节调试节 |
| `LOG_I` | 每个子文件 | zoom 区间、base zoom、offset/size、瓦片网格、origin_tile、索引地址 |
| `LOG_D` | `mf_query` 完成 | `query z14 done: blocks=N objs=N water_tiles=N`（每帧一条） |
| `LOG_D` | `mf_query` 早退 | 无覆盖子文件 / bbox 在图外 |
| `LOG_W` | 块异常 | 尺寸非法(>8MB)、parse_block 数据损坏半途跳过 |
| `LOG_E` | IO 失败 | 索引/数据块读取失败（含行列定位） |
| `LOG_D` | `way_to_pixels` | 超 1024 节点的 way 被截断 |
| `LOG_D` | 海岸线洪泛统计 | `coastline: ways=N chains=N islands=N sea_px=N (P%)`（每帧一条） |
| `LOG_E` | 洪泛分配失败 | chains / barrier / visited / BFS 队列四处 |

monaco.map 打开时的实际输出示例：

```
[D] map header_size: 3726
[D] optional start position: 43.6184,7.5021
[D] optional start zoom: 8
[I] header: tile_size=256 projection=Mercator flags=0x7c poi_tags=68 way_tags=140
[I] subfile[0]: zoom 0-7 base=5 offset=3750 size=3832 grid=1x1 blocks=1 origin_tile=(16,11) index@3750
[I] subfile[1]: zoom 8-11 base=10 offset=7582 size=34972 grid=2x3 blocks=6 origin_tile=(532,372) index@7582
[I] subfile[2]: zoom 12-21 base=14 offset=42554 size=309415 grid=19x31 blocks=589 origin_tile=(8524,5966) index@42554
[I] map '../resources/monaco.map' opened: v5 subfiles=3 zoom 0-21 tile=256 tags=68/140 bbox(43.3838,7.3086)-(43.8529,7.6957)
```

这些字段与第二部分的字符图一一对应，是验证你对格式理解的现成教具。

---

# 第四部分：毕业自测

能不看代码回答以下问题，即算读懂：

1. 为什么低 zoom 下海岸线数据有缺口，也只会局部渗漏而非整块伪陆地？
2. `mf_query` 里块大小为什么要"找到下一个非空条目"？直接读下一行索引会怎样？
3. 两遍道路绘制中，第一遍画壳第二遍画主线，交叉口为什么自然干净？
4. 标注碰撞表的容量上限是多少？满了之后新标注的命运？
5. `project_d` 和 `mf_view_project` 各用在哪，为什么不能合并成一个？
6. 文件头里的投影名为什么必须校验为 "Mercator"？如果有个文件声明别的投影，继续渲染会发生什么？
7. 为什么闭 Way 不一定按面绘制？举一个反例。
