# pico_nav 插件开发手册 (PLUGIN_SDK)

pico_nav 的 GUI 支持把"应用"做成**可安装的应用包**：一个 `.app` 文件
（清单 + 代码 + 资源）通过 webctl 装入设备的 `/data/apps/<name>/`，
**约 1 秒内自动出现在应用抽屉里，无需重启 GUI**。

应用包有两种形态，由清单的 `kind` 决定：

| kind | 形态 | 运行方式 |
|------|------|----------|
| `exe`（推荐） | **独立可执行程序** | 抽屉选中后宿主 `fork+exec`，应用自己接管屏幕/按键/串口，退出后宿主恢复 |
| `plugin` | 进程内 `.so` 插件 | 宿主 `dlopen`，页面与宿主同进程（崩溃会带走 GUI） |

独立程序模型下系统 UI 只负责"列出 + 启动"，应用也可以在 shell 里直接
执行（`/data/apps/flora/flora`），与手机上的 App 一致。

## 1. 架构一览

```
demo (宿主, /usr/bin/demo, 核心=系统UI: 表盘+抽屉+设置)
 ├─ 导出白名单 (app.exports): lv_* / app_* / msg_* / page_* / config_*
 │   / gps_service_* / trip_store_* / sampler_* / status_bar_* / log_*
 ├─ page_manager: page_register() 注册页面, page_manager_open() 按名导航
 ├─ app_registry: 抽屉条目运行时列表
 ├─ plugin_manager: 扫描 /data/apps
 │    ├─ <name>/manifest.txt + <exe> + 资源   ← 应用包 kind=exe
 │    ├─ <name>/manifest.txt + app.so + 资源 ← 应用包 kind=plugin
 │    └─ *.so                                 ← 旧式裸插件(兼容)
 │  1 Hz 监听目录签名 → 热加载新应用 → 抽屉自动重建
 ├─ app_launcher: kind=exe 的启动器
 │    fork+exec → 释放按键/UART → 等待退出 → 重新接管并全屏重绘
 └─ /data/apps/...        ← webctl 上传安装（/data 不参与 A/B，OTA 不清）

独立应用 (kind=exe, 自己的进程)
 ├─ 链接 libpiconav.a (LVGL + page_manager + 服务层 + HAL)
 ├─ 自己打开 /dev/fb0、/dev/input/event*，按需打开 GPS 串口
 └─ 把抽屉预览写到 <包目录>/preview.txt，宿主每秒读取
```

官方插件（独立 git 仓库）：
`app/plugin_gps`（GPS 接收机页）、`app/plugin_map`（Mapsforge 实时地图
+ 选图页）、`app/plugin_flora`（LA 植物图鉴, 离线数据）。树内示例：
`app/pico_nav/plugins/stopwatch`。

要点：

- **独立进程（kind=exe）**：应用是一份普通 ELF 可执行文件，由抽屉启动。
  它拥有自己的 LVGL 实例和硬件，崩溃只影响自己（宿主 `waitpid` 后重绘
  恢复），不需要"重启系统 UI"；同一个程序也能在 shell 里直接跑。
- **图标归应用所有**：每个应用包自带 `icon.rgb565a8`（40×40
  RGB565+A8 固定格式）并在 `manifest.txt` 声明；宿主只按固定格式解析
  并渲染，不再维护应用图标库。内置 app（行车/设置）的图标仍在固件内。
- **抽屉条目由清单驱动**（`label`/`info`/`data`/`icon`）；需要实时
  预览列的 app 仍可在 `plugin_init()` 里 `app_registry_add()` 注册
  自己的条目（宿主会为同一页面补上清单里的图标）。
- **单前台所有者**：设备只有一块 `/dev/fb0`，没有合成器。宿主每秒两次扫描
  `/proc/*/fd`，发现别的进程打开了 framebuffer（例如你从串口直接跑的
  app）就自动挂起（停渲染、关按键、放串口），对方退出后自动重绘恢复。
  所以串口调试不用再先 `S99demo stop`。
- **进程内插件终身驻留**：不会 dlclose。热安装只增不减；删除应用需要
  重启 GUI 才会消失（独立程序退出后自然不存在，只是抽屉条目还在）。
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
| `kind` | `exe` = 独立可执行程序；`plugin`（默认）= 进程内 `.so` |
| `page` | 应用首个页面名（`kind=plugin` 时宿主据此关联条目；`kind=exe` 仅作说明/运行时的 root_page） |
| `exec` | 可执行文件名（`kind=exe`，如 `flora`）或 `.so` 文件名（默认 `app.so`） |
| `preview` | `kind=exe`：抽屉白色列的预览文件名（应用自己写，宿主每秒读） |
| `icon` | 图标资源文件名（40×40 RGB565+A8 = 4800 字节） |
| `info` | 抽屉灰色列，`\n` 表示换行 |
| `data` | 抽屉白色列（静态文本） |

安装 = 整包解压到 `/data/apps/<name>/`（与打包目录 1:1，`kind=exe` 的
`exec` 会被 `chmod 0755`）；宿主每秒比对目录签名（名字 + mtime + 大小），
发现变化就重新扫描：`kind=exe` 只登记抽屉条目，`kind=plugin` 再
dlopen + `plugin_init()`。

## 3. 独立应用 (kind=exe)

### 3.1 运行模型

```
抽屉 ENTER
  → app_launcher: 暂停 LVGL 主循环, 释放按键设备 + GPS 串口
  → fork + exec /data/apps/<name>/<exec>
       应用: 打开 /dev/fb0, LVGL 初始化, 打开按键, (按需)打开 GPS 串口
       应用: 自己的页面栈 + 长按 ENTER 返回
       应用: 长按 ENTER 在根页面 = 退出进程
  → 宿主 waitpid, 重新打开按键/串口, 清空按键队列, 全屏重绘
```

- **硬件交接**：同一时刻只有一个进程使用屏幕/按键/串口。宿主在 exec 前
  释放（`hal_key_shutdown` / `hal_gps_suspend`），子进程退出后重新获取。
- **退出**：根页面上长按 ENTER 退出；页面自己占用长按 ENTER（如地图
  缩放的 `PAGE_FLAG_NO_AUTO_BACK`）时，**持续按住 ENTER 4 秒**由运行
  时强制退出（保险丝）。
- **崩溃隔离**：应用段错误只结束它自己，宿主立即回到抽屉。

### 3.2 应用代码

链接静态运行时 `pico_nav/build/lib/libpiconav.a`（LVGL、page_manager、
msg_center、gps_service、config、HAL 都在里面），然后：

```c
#include "App/runtime/app_runtime.h"

extern int plugin_init(void);          /* 复用原插件：page_register(...) */

static void register_pages(void) { (void)plugin_init(); }

int main(void)
{
    static const app_runtime_cfg_t cfg = {
        .name               = "flora",
        .root_page          = "flora_home",
        .register_pages     = register_pages,
        .use_gps            = 0,          /* 1 = 打开接收机串口 */
        .use_config_watcher = 1,
        .after_start        = NULL,       /* 可选：建定时器/写首帧预览 */
    };
    return app_runtime_run(&cfg);
}
```

页面代码（`page_t` + `page_register`/`page_manager_open`）与进程内插件
**完全一致**，不需要改。资源定位用 `app_plugin_dir()`（运行时返回
`/proc/self/exe` 所在目录，并把 cwd 切过去）。

> 注意：应用包里的 `.so` 用宿主导出的符号（`dlopen`）；独立可执行文件
> 用静态运行时。同一个仓库可以同时构建两者（`make` / `make exe`）。

### 3.3 抽屉预览（固定格式）

独立进程的实时数据宿主拿不到，因此约定一个**预览文件**：

- 清单写 `preview=preview.txt`；
- 应用调用 `app_preview_write("...")`（原子写 `<包目录>/preview.txt`），
  最多 3 行 ASCII，`\n` 分隔；
- 抽屉每秒读取该文件填入白色列（`data_get` 优先于它）。

GPS 应用就是这样在退出后仍显示"最后一次定位"的。

## 4. 插件契约

进程内插件（`kind=plugin`）的 `.so` 必须导出（见
`App/Utils/plugin_api.h`，**当前 ABI = 6**）：

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

## 5. 图标资源（应用自维护）

40×40、RGB565 小端 + 每像素 1 字节 alpha（`LV_IMG_CF_TRUE_COLOR_ALPHA`，
`LV_COLOR_16_SWAP 0`），共 4800 字节，声明在 `manifest.txt` 的
`icon=`。设备端由宿主读入内存直接渲染，无需插件代码参与。

生成：各仓库 `tools/gen_icon.py`（从 pico_nav 的 app 图标栅格化器裁剪，
只保留本应用的 glyph），输出 `assets/icon.rgb565a8`：

```sh
python3 tools/gen_icon.py     # 改形状/配色后重跑(产物已提交)
```

## 6. 编译与打包

```sh
./build.sh          # ARM .so + 独立可执行 + .app 包
./build.sh host     # 宿主版 .so(模拟器用)
./build.sh clean
```

- `make` → `<name>.so`（进程内插件 / 模拟器用）
- `make exe` → `<name>`（独立可执行，链接 `$(ROOT)/build/lib/libpiconav.a`）
- `make app` → `build/<name>-<ver>.app`（清单 + 可执行 + `assets/` +
  其他资源，调用 `pico_nav/tools/pack_app.py`，校验 ARM ELF、图标尺寸、
  清单字段）

先构建运行时归档：`cd app/pico_nav && make lib`（root `./build.sh apps`
会自动执行）。

- 工具链必须与固件一致：buildroot glibc 交叉工具链
  （`buildroot/buildroot-2023.02.6/output/host/bin/`），uClibc 工具链
  编出的 .so 加载即崩。
- 插件不链接任何库；LVGL/框架符号在 dlopen 时由宿主解析
  （`app/pico_nav/app.exports`）。

## 7. 部署与调试

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

独立应用也能在设备上直接跑（调试期最方便）：

```sh
/etc/init.d/S99demo stop
cd /data/apps/flora && ./flora        # 自己接管屏幕，长按 ENTER 退出
/etc/init.d/S99demo start
```

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

> 模拟器只支持 `kind=plugin`（进程内）。`kind=exe` 的应用是 ARM
> 可执行程序、直接用 fbdev/evdev，请在设备上验证（可先 `S99demo stop`
> 后裸跑）。

## 8. 服务消费示例

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

## 9. 限制

- **kind=exe**：崩溃只结束该应用，宿主 `waitpid` 后立即恢复；但屏幕/按键/
  串口在应用运行期间归它所有（宿主不渲染）。应用必须自己释放资源后退出。
- **kind=plugin**：无隔离，插件运行期崩溃 = GUI 重启（S99demo 2 s 拉起）。
- ABI 与固件绑定：rootfs 升级后都要重编译；不匹配会被明确拒载。
  独立应用静态链接 `libpiconav.a`，换固件后同样要重新打包。
- 热安装只增不减：删除 `/data/apps/<name>/` 后需重启 GUI 才从抽屉消失；
  进程内插件同名升级也要重启（dlopen 不重跑已驻留的 .so），独立应用
  重新安装后下次启动即为新版本。
- **kind=exe 拿不到宿主的实时数据**：抽屉的白色列靠应用自己写的
  `preview.txt`（退出前的最后状态）；需要实时数据的场景请用 `plugin`。
- RAM 预算 64 MB：独立应用各自静态链接 LVGL（磁盘占用大、运行时只跑
  一个）；插件与宿主共享一份 LVGL。
- webctl 无鉴权（受信 LAN 姿态），安装接口做了 magic/名称/ARM ELF/
  大小/路径校验，但不要把 80 端口暴露到公网。

## 10. 版本策略

`PLUGIN_ABI_VERSION`（plugin_api.h）在以下情况 +1：`page_t`/
`page_ops_t`/`app_entry_t` 布局变化、导出函数签名变化、**导出符号集合
收缩**。仅新增导出函数/新页面名不破坏 ABI。

历史：**1** 初始契约 → **2** `app_entry_t` 增加 `img` → **3**
`app_entry_t` 增加 `info`/`data`/`data_get`；GPS/地图拆出独立仓库 →
**4** 应用包（`.app`）：宿主不再导出 `app_icon_{gps,map,stopwatch}_img`
（图标改由各应用的 `icon.rgb565a8` 资源提供），新增
`app_plugin_dir()`，抽屉条目可由 manifest 构建，支持热安装 → **5**
`app_entry_t` 增加 `exec`（`kind=exe` 独立可执行应用）→ **6**
`app_entry_t` 增加 `preview`（独立应用通过预览文件更新抽屉白色列）。
旧 ABI 的 `.so` 会被明确拒载，重新打包安装即可。
