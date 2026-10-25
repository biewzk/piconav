# pico_nav 插件开发手册 (PLUGIN_SDK)

pico_nav 的 GUI 支持把"应用"做成可安装的插件：一个 `.so` 文件放进设备的
`/data/apps/` 目录，重启 GUI 后自动加载并出现在应用抽屉里。插件与内置
页面使用完全相同的 API（LVGL + 服务层），无需重烧固件。

## 1. 架构一览

```
demo (宿主, /usr/bin/demo)
 ├─ 导出白名单 (app.exports): lv_* / app_* / msg_* / page_* / config_*
 │   / gps_service_* / trip_store_* / sampler_* / map_view_* / mf_*
 │   / status_bar_* / log_*
 ├─ page_manager: page_register() 注册页面, page_manager_open() 按名导航
 ├─ app_registry: app_registry_add() 注册抽屉条目
 ├─ plugin_manager: 启动时扫描 /data/apps/*.so → dlopen → ABI 校验
 │   → plugin_init()
 └─ /data/apps/*.so        ← webctl 上传安装（/data 不参与 A/B，OTA 不清）
```

要点：

- **插件 = 静态链接进宿主的普通模块**，只是放在独立 .so 里。页面对象
  用 `static` 存储，`plugin_init()` 里注册；页面构建仍是懒加载（推入
  页面栈时才 create，弹出即销毁）。
- **插件成功加载后终身驻留**：不会 dlclos，页面栈里可能还有它的页面。
  只有 init 失败/ABI 不符才会被卸载跳过。
- **任何插件问题都不会拖垮宿主**：dlopen 失败、缺导出符号、ABI 不匹配、
  init 返回非 0 都只产生一条 WARN 日志后跳过。但插件页面运行期崩溃
  仍然等于整个 GUI 崩溃（无隔离，S99demo 会自动拉起）。

## 2. 插件契约

插件 .so 必须导出以下符号（见 `App/Utils/plugin_api.h`）：

```c
#include "plugin_api.h"   /* App/Utils/plugin_api.h */

const uint32_t plugin_abi_version = PLUGIN_ABI_VERSION;
const char *plugin_name    = "myapp";     /* 全局唯一 */
const char *plugin_version = "1.0.0";

int plugin_init(void)
{
    /* 1. 注册页面 */
    page_register(&my_page);
    /* 2. 注册抽屉条目（icon 是单字符, montserrat 字体渲染） */
    static const app_entry_t entry = { "My App", "A", 0x00C2FF, &my_page };
    if (app_registry_add(&entry) < 0)
        return -1;
    return 0;
}
```

- `plugin_abi_version` 必须等于宿主的 `PLUGIN_ABI_VERSION`。导出 API
  的结构体布局/函数签名变化时版本号 +1，旧插件会被明确拒载
  （日志 `ABI %u, host expects %u`），重新编译即可。
- `plugin_init()` 在 dlopen 后立即调用一次；失败返回非 0。
- 页面生命周期与内置页面完全一致：`create/destroy/on_show/on_hide/
  on_key`（`page_manager.h`）。长按 ENTER=返回是 page_manager 的默认
  行为，页面自己要用长按 ENTER（如地图缩放）时设
  `PAGE_FLAG_NO_AUTO_BACK`。
- 页面参数：`page_manager_open("livemap", path)` 的 `param` 指针在
  `create()` 内有效（随后被清除），需要在 create 里拷贝进私有数据。
- **UI 字符串注意**：内置字体（Montserrat）无 CJK 字形，中文标签不会
  显示；如需中文界面文本需自带字体或用转义字节嵌入（参考秒表插件的
  UTF-8 转义写法）。

## 3. 编译

`plugins/stopwatch/Makefile` 是模板，核心就一条链接规则——**不链接
任何库**，所有符号由宿主在 dlopen 时解析：

```make
arm-buildroot-linux-gnueabihf-gcc -O2 -g0 -fPIC -shared \
    -std=c99 -DLV_CONF_INCLUDE_SIMPLE \
    -I<repo根> -I<repo根>/lvgl -I<repo根>/App/Utils -I<repo根>/App/Pages \
    -o stopwatch.so stopwatch.c
```

- 工具链必须与固件一致：buildroot glibc 交叉工具链
  （`buildroot/buildroot-2023.02.6/output/host/bin/`），uClibc 工具链
  编出的 .so 加载即崩（C 库不匹配，与 app 侧规矩相同）。
- `make sim` 产出宿主版 .so，供 SDL 模拟器加载（逻辑/布局验证，不能
  交叉部署）。
- 仓库内编译：`cd app/pico_nav/plugins/stopwatch && make`（设备）/
  `make sim`（宿主）。

## 4. 部署与调试

```sh
# 方式一：webctl 上传（推荐）—— 浏览器打开 http://<设备IP>/
#   插件卡片 → 选择 .so → 上传（自动 ELF/ARM 校验，落到 /data/apps/）
#   → 点"重启应用"（或 S99demo restart）

# 方式二：scp（调试期）
scp -O -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \
    build/stopwatch.so root@<设备IP>:/data/apps/
ssh root@<设备IP> S99demo restart   # 或杀掉 demo, 监督循环会拉起

# 看加载日志
ssh root@<设备IP> grep plugin /tmp/demo.log
```

模拟器（宿主插件）：

```sh
cd app/pico_nav/sim
PICO_NAV_APPS=../plugins/stopwatch/build_sim ./build_sim/bin/pico_nav_sim
# 无头回归:
PICO_NAV_APPS=... SIM_AUTOQUIT_MS=11000 \
SIM_KEY="right,down,down,down,enter,enter,enter" SIM_KEY_MS=900 \
SIM_DUMP_PPM=/tmp/stopwatch.ppm SDL_VIDEODRIVER=dummy ./build_sim/bin/pico_nav_sim
```

环境变量：`PICO_NAV_APPS` 覆盖插件目录（默认 `/data/apps`）。

## 5. 服务消费示例

插件与内置页面同一套服务，"要轮子直接拿"：

```c
/* GPS 数据池 + 1s 采样需求（进页面要，出页面还） */
#include "gps_service.h"          /* 经 -I<App/Utils> */
const gps_data_t *g = app_gps();          /* app.h */
gps_service_demand_add();                 /* on_show */
gps_service_demand_release();             /* on_hide */
msg_center_subscribe(app_msg_center(), MSG_GPS, my_gps_cb, self);

/* 地图：a) 跳系统地图页  b) 内嵌地图控件  c) 直接用渲染引擎 */
page_manager_open("livemap", "/data/maps/city.map");
map_view_t *mv = map_view_create(parent, "/data/maps/city.map"); /* App/Utils/map_view.h */
#include "mf_map.h"  /* Mapsforge 引擎亦可 */

/* 配置（@ 前缀为易失键，仅存内存不落盘） */
int v; config_get_int("myapp.threshold", &v, 10);   /* config_api.h */
config_set_int("myapp.threshold", v + 1);
config_save();                                      /* 显式 commit 落盘 */

/* 周期任务（与 GPS 采样同一组件） */
sampler_t *s = sampler_create(1000, 5000, my_poll, my_publish, user);

/* 运行时消息主题（插件私有话题） */
msg_topic_t t = msg_topic_register(app_msg_center(), "myapp.alert");
```

**边界**：HAL 层（`hal_*.h` 的函数）不导出——那是 sim/真机差异的接缝，
插件走服务层；直接读 GPIO/UART 这类需求应作为服务进主线。

## 6. 限制

- 无进程隔离：插件运行期崩溃 = GUI 重启（监督循环兜底，2 s 拉起）。
- ABI 与固件绑定：rootfs 升级（尤其 LVGL/头文件变化）后插件需重编译；
  不匹配会被拒载而不是崩溃。
- RAM 预算 64 MB：插件页面懒构建，但 .so 文本段常驻页缓存；单插件
  控制在 ~100 KB 量级，避免全屏大缓冲（地图控件已做按需分配）。
- webctl 无鉴权（受信 LAN 姿态），插件上传接口做了 ELF magic/架构/
  大小校验，但不要把 80 端口暴露到公网。

## 7. 版本策略

`PLUGIN_ABI_VERSION`（plugin_api.h）在以下情况 +1：
page_t/page_ops_t 布局变化、app_entry_t 布局变化、任何导出函数签名
变化。仅新增导出函数/新页面名不破坏 ABI，可不变。固件侧同步在
PLUGIN_SDK.md 与根 AGENTS.md 记录。
