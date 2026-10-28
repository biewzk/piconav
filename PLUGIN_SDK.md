# pico_nav 插件开发手册 (PLUGIN_SDK)

pico_nav 的 GUI 支持把"应用"做成**可安装的应用包**：一个 `.app` 文件
（清单 + 代码 + 资源）通过 webctl 装入设备的 `/data/apps/<name>/`，
**约 1 秒内自动出现在应用抽屉里，无需重启 GUI**。插件与内置页面使用
完全相同的 API（LVGL + 服务层），无需重烧固件。

## 1. 架构一览

```
demo (宿主, /usr/bin/demo, 核心=系统UI: 表盘+抽屉+设置)
 ├─ 导出白名单 (app.exports): lv_* / app_* / msg_* / page_* / config_*
 │   / gps_service_* / trip_store_* / sampler_* / status_bar_* / log_*
 ├─ page_manager: page_register() 注册页面, page_manager_open() 按名导航
 ├─ app_registry: 抽屉条目运行时列表
 ├─ plugin_manager: 扫描 /data/apps
 │    ├─ <name>/manifest.txt + app.so + 资源   ← 应用包(.app 安装)
 │    └─ *.so                                 ← 旧式裸插件(兼容)
 │  1 Hz 监听目录变化 → 热加载新应用 → 抽屉自动重建
 └─ /data/apps/...        ← webctl 上传安装（/data 不参与 A/B，OTA 不清）
```

官方插件（独立 git 仓库）：
`app/plugin_gps`（GPS 接收机页）、`app/plugin_map`（Mapsforge 实时地图
+ 选图页）、`app/plugin_flora`（LA 植物图鉴, 离线数据）。树内示例：
`app/pico_nav/plugins/stopwatch`。

要点：

- **图标归应用所有**：每个应用包自带 `icon.rgb565a8`（40×40
  RGB565+A8 固定格式）并在 `manifest.txt` 声明；宿主只按固定格式解析
  并渲染，不再维护应用图标库。内置 app（行车/设置）的图标仍在固件内。
- **抽屉条目由清单驱动**（`label`/`info`/`data`/`icon`）；需要实时
  预览列的 app 仍可在 `plugin_init()` 里 `app_registry_add()` 注册
  自己的条目（宿主会为同一页面补上清单里的图标）。
- **插件成功加载后终身驻留**：不会 dlclose。热安装只增不减；删除应用
  需要重启 GUI 才会消失。
- **任何插件问题都不会拖垮宿主**：dlopen 失败、缺导出符号、ABI 不匹配、
  init 返回非 0 都只产生一条 WARN 日志后跳过。但插件页面运行期崩溃
  仍然等于整个 GUI 崩溃（无隔离，S99demo 会自动拉起）。

## 2. 应用包格式 (.app)

单文件、小端，由 `app/pico_nav/tools/pack_app.py` 生成：

```
off  size  field
0    8     magic "PICOAPP1"
8    4     u32 manifest_len
12   4     u32 file_count
16   4     u32 index_off          (绝对偏移)
20   ...   manifest 文本 (key=value)
     ...   index: file_count × { u16 name_len, name, u32 off, u32 size }
     ...   文件数据 (绝对偏移)
```

清单键（固定格式；宿主与 webctl 都按此解析）：

| 键 | 说明 |
|----|------|
| `name` | 必需。唯一 id，`[a-z0-9_-]`，安装目录名 |
| `abi` | 必需。插件 ABI，必须等于固件的 `PLUGIN_ABI_VERSION` |
| `label` | 抽屉显示名 |
| `version` | 版本字符串 |
| `page` | 插件注册的页面名（宿主据此把条目和页面关联） |
| `exec` | .so 文件名，默认 `app.so` |
| `icon` | 图标资源文件名（40×40 RGB565+A8 = 4800 字节） |
| `info` | 抽屉灰色列，`\n` 表示换行 |
| `data` | 抽屉白色列（静态文本） |

安装 = 整包解压到 `/data/apps/<name>/`（与打包目录 1:1）；宿主 1 Hz
监听目录 mtime，发现新应用后 dlopen + `plugin_init()` + 抽屉重建。

## 3. 插件契约

`.so` 必须导出（见 `App/Utils/plugin_api.h`，**当前 ABI = 4**）：

```c
const uint32_t plugin_abi_version = PLUGIN_ABI_VERSION;
const char *plugin_name    = "myapp";     /* 全局唯一 */
const char *plugin_version = "1.0.0";
int plugin_init(void);                    /* 注册页面, 0=成功 */
```

```c
int plugin_init(void)
{
    page_register(&my_page);          /* 页面注册(必需) */

    /* 可选：需要动态预览列时自己注册条目；图标仍由清单提供 */
    static const app_entry_t entry = {
        .name = "My App", .icon = "M", .icon_color = 0x00C2FF,
        .page = &my_page,
        .info = "State\nCount",
        .data_get = my_drawer_data,   /* void cb(char *buf, int len) */
    };
    app_registry_add(&entry);
    return 0;
}
```

- **资源定位**：`const char *app_plugin_dir(void)` 在 `plugin_init()`
  期间返回本应用包目录（旧式裸 .so 为空串）。资源都放在该目录下，
  例如 `snprintf(p, n, "%s/data.bin", app_plugin_dir())`；需要后续
  访问就把路径拷贝走。
- 页面生命周期与内置页面一致：`create/destroy/on_show/on_hide/on_key`
  （`page_manager.h`）。长按 ENTER=返回是 page_manager 的默认行为，
  页面自己要用长按 ENTER（如地图缩放）时设 `PAGE_FLAG_NO_AUTO_BACK`。
- 页面参数：`page_manager_open(name, param)` 的 `param` 指针在
  `create()` 内有效（随后被清除），需要在 create 里拷贝。
- **UI 字符串**：内置 Montserrat 无 CJK 字形；抽屉条目的 CJK 名称可以
  写在清单里（UTF-8，宿主用 `lv_font_app_16` 渲染），但**清单里的
  汉字必须在宿主字体子集内**，否则不显示。

## 4. 图标资源（应用自维护）

40×40、RGB565 小端 + 每像素 1 字节 alpha（`LV_IMG_CF_TRUE_COLOR_ALPHA`，
`LV_COLOR_16_SWAP 0`），共 4800 字节，声明在 `manifest.txt` 的
`icon=`。设备端由宿主读入内存直接渲染，无需插件代码参与。

生成：各仓库 `tools/gen_icon.py`（从 pico_nav 的 app 图标栅格化器裁剪，
只保留本应用的 glyph），输出 `assets/icon.rgb565a8`：

```sh
python3 tools/gen_icon.py     # 改形状/配色后重跑(产物已提交)
```

## 5. 编译与打包

```sh
./build.sh          # ARM .so + .app 包 (build/<name>-<ver>.app)
./build.sh host     # 宿主版 .so(模拟器用)
./build.sh clean
```

`make` 仍然只产 `.so`；`make app` 额外把清单 + `.so` + `assets/` +
其他资源打成 `.app`（调用 `pico_nav/tools/pack_app.py`，会校验 ARM ELF、
图标尺寸、清单字段）。

- 工具链必须与固件一致：buildroot glibc 交叉工具链
  （`buildroot/buildroot-2023.02.6/output/host/bin/`），uClibc 工具链
  编出的 .so 加载即崩。
- 插件不链接任何库；LVGL/框架符号在 dlopen 时由宿主解析
  （`app/pico_nav/app.exports`）。

## 6. 部署与调试

```sh
# 方式一：webctl 网页（推荐）—— http://<设备IP>/ → 插件/应用管理 →
#   选择 .app → "安装应用包"。约 1 秒后抽屉自动出现，无需重启。

# 方式二：HTTP API
curl -X PUT --data-binary @flora.app \
     'http://<设备IP>/api/upload?mode=app'

# 方式三：scp（调试期，需手工解包成目录）
scp -O build/flora-1.0.0.app root@<设备IP>:/tmp/
# （推荐直接走方式一/二，省去手工解包）
```

旧式裸 `.so` 仍可 `mode=plugin` 上传到 `/data/apps/`（无清单/资源，
图标回退字母），同样约 1 秒后加载。

```sh
ssh root@<设备IP> grep -E 'plugin|flora' /tmp/demo.log
```

模拟器（宿主）：

```sh
make sim
cd ../pico_nav/sim
PICO_NAV_APPS=../../plugin_flora/build_sim ./build_sim/bin/pico_nav_sim
# 无头回归: SIM_AUTOQUIT_MS / SIM_KEY / SIM_KEY_MS / SIM_DUMP_PPM
```

模拟器也可以直接加载**包目录布局**：把 `manifest.txt + app.so(宿主版)
+ 资源` 放进一个目录，`PICO_NAV_APPS=<父目录>` 即可；运行中把目录
拷进去就能验证热安装。

## 7. 服务消费示例

```c
#include "gps_service.h"
const gps_data_t *g = app_gps();
int fresh = gps_service_is_fresh(g);
gps_service_demand_add();                 /* on_show */
gps_service_demand_release();             /* on_hide */
msg_center_subscribe(app_msg_center(), MSG_GPS, my_gps_cb, self);

int v; config_get_int("myapp.threshold", &v, 10);   /* config_api.h */
config_set_int("myapp.threshold", v + 1);
config_save();

sampler_t *s = sampler_create(1000, 5000, my_poll, my_publish, user);
```

**边界**：HAL 层不导出——插件走服务层；直接读 GPIO/UART 的需求应
作为服务进主线。

## 8. 限制

- 无进程隔离：插件运行期崩溃 = GUI 重启（S99demo 2 s 拉起）。
- ABI 与固件绑定：rootfs 升级后插件需重编译；不匹配会被明确拒载。
- 热安装只增不减：删除 /data/apps/<name>/ 后需重启 GUI 才消失；
  同名升级也要重启（dlopen 不会重新执行已驻留的 .so）。
- RAM 预算 64 MB：应用包目录常驻页缓存；资源由 app 自行管理。
- webctl 无鉴权（受信 LAN 姿态），安装接口做了 magic/名称/ARM ELF/
  大小/路径校验，但不要把 80 端口暴露到公网。

## 9. 版本策略

`PLUGIN_ABI_VERSION`（plugin_api.h）在以下情况 +1：`page_t`/
`page_ops_t`/`app_entry_t` 布局变化、导出函数签名变化、**导出符号集合
收缩**。仅新增导出函数/新页面名不破坏 ABI。

历史：**1** 初始契约 → **2** `app_entry_t` 增加 `img` → **3**
`app_entry_t` 增加 `info`/`data`/`data_get`；GPS/地图拆出独立仓库 →
**4** 应用包（`.app`）：宿主不再导出 `app_icon_{gps,map,stopwatch}_img`
（图标改由各应用的 `icon.rgb565a8` 资源提供），新增
`app_plugin_dir()`，抽屉条目可由 manifest 构建，支持热安装。
ABI 3 及更早的 .so 在 ABI 4 固件上会被拒载，需重新打包安装。
