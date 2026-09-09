# map 文件格式参考

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

