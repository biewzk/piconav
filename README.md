[toc]

# pico_nav - 里程表 GUI (LVGL v8.1.0)

Pico Nav 里程表界面程序。基于 LVGL v8.1.0，采用自研的「页面管理器 + 消息总线」应用框架，真机使用 Linux frame buffer (`/dev/fb0`) 显示到 ST7789V 2.4" 屏幕（240x320），并提供 SDL2 PC 模拟器用于日常开发与调试。

## 目录结构

> 以下结构取自 `tree -a --dirsfirst`（目录优先、按名排序），`build/` 与 `sim/build_sim/` 为编译产物、`lvgl/`/`lv_drivers/` 为第三方源码，均折叠展示。

```
pico_nav/
├── App/                    # 应用框架与页面
│   ├── Config/
│   │   └── app_config.h    # 应用配置 (分辨率/动画时长/字号)
│   ├── Pages/
│   │   ├── app_registry.c / app_registry.h   # App 注册表 (名称/图标/page 映射)
│   │   ├── page_app_drawer.c / page_app_drawer.h # 应用抽屉 (App 列表 + 滑动高亮)
│   │   ├── page_dialplate.c / page_dialplate.h   # 表盘页 (速度大字 + 距离/时间卡)
│   │   ├── page_home.c / page_home.h             # 主页 (Carousel 面板切换 + 时间表盘)
│   │   ├── page_livemap.c / page_livemap.h       # 离线地图页 (Mapsforge 渲染 + 缩放/平移)
│   │   ├── page_settings.c / page_settings.h     # 设置页
│   │   └── page_startup.c / page_startup.h       # 开机页 (logo, 任意键进入主页)
│   ├── Utils/
│   │   ├── config_api.c / config_api.h   # 用户态配置存储 API (键值, 对接 /dev/config)
│   │   ├── config_ioctl_user.h        # 内核 config ioctl 的用户态镜像
│   │   ├── log.c / log.h               # 日志系统 (syslog + stderr 镜像)
│   │   ├── msg_center.c / msg_center.h # 发布订阅消息总线 (topic 表 + 订阅者链表)
│   │   └── page_manager.c / page_manager.h # 页面管理器 (页面栈 + 生命周期 + 转场)
│   ├── app.c                # 应用入口: 组装框架 + app_init/app_run 主循环
│   └── app.h
├── build/                  # 真机编译产物 (bin/demo)
├── Doc/                    # 设计文档
│   ├── APP_ARCHITECTURE.md # App 框架框图 (Mermaid)
│   └── APP_FLOW.md         # App 运行流程图 (Mermaid)
├── HAL/                    # 硬件抽象层 (真机实现)
│   ├── hal.h               # 接口定义: 按键/时间/退出
│   └── hal_keys_evdev.c    # 真机按键: 扫描 /dev/input/event*
├── lv_drivers/             # LVGL 显示/输入驱动 (fbdev 等)
├── lvgl/                   # LVGL 源码 (v8.1.0)
├── Mapsforge/              # Mapsforge 离线地图引擎
│   ├── mf_map.c / mf_map.h     #   地图文件解析 (头部/标签表/瓦片索引/块解码 + 帧查询)
│   ├── mf_render.c / mf_render.h # 渲染器 (视口投影/多边形填充/道路两遍绘制/海岸线洪泛/标注)
│   └── mf_theme.c / mf_theme.h #   渲染主题 (OSM 标签 -> 样式: 颜色/线宽/等级/标注)
├── resources/              # 离线地图数据 (.map v5)
│   └── macau.map
├── sim/                    # PC 模拟器 (SDL2)
│   ├── build_sim/          # 模拟器编译产物 (bin/pico_nav_sim)
│   ├── build_sim.sh        # 模拟器一键编译/运行
│   ├── hal_keys_sdl.c      # 模拟按键: WASD/方向键/Enter/Space/Esc + 鼠标滚轮/左键拖动
│   ├── main_sim.c / main_sim.h # SDL 窗口 + LVGL 渲染 + NMEA 回放 + 无头测试
│   ├── Makefile            # host 编译规则
│   └── sample.nmea         # 示例 NMEA 数据 (GPRMC, 10 条)
├── third_party/            # 第三方库
│   └── cJSON/              # JSON 解析库 (用于配置系统)
├── AGENTS.md               # AI 编程助手配置
├── build.sh                # 真机一键编译 (推荐)
├── .gitignore
├── lv_conf.h               # LVGL 配置 (16bit RGB565, LV_TICK_CUSTOM)
├── lv_drv_conf.h           # 显示驱动配置 (FBDEV_PATH=/dev/fb0, USE_EVDEV=0)
├── main.c                  # 真机入口: LVGL + fbdev 初始化, 调用 app_init/app_run
├── Makefile                # 真机交叉编译规则
└── README.md
```

## 编译

### 真机 (ARM 交叉编译)

```bash
cd pico_nav
./build.sh          # 编译
./build.sh clean    # 清理后重新编译
```

产物：`build/bin/demo`。

### PC 模拟器 (host 编译)

依赖 SDL2（Ubuntu/Debian: `sudo apt install libsdl2-dev`）：

```bash
cd sim
./build_sim.sh          # 编译 -> build_sim/bin/pico_nav_sim
./build_sim.sh run      # 编译后直接运行 (默认回放 sample.nmea)
./build_sim.sh run my.nmea   # 回放指定 NMEA 文件
```

模拟器必须在 `sim/` 目录下编译，不支持从项目根目录一键编译。

## 运行

### 获取地图文件

渲染器使用 [Mapsforge](https://github.com/mapsforge/mapsforge) 格式的 `.map` v5 二进制地图文件。可以从官方服务器下载预编译的地图：

- **全球地图索引**：https://download.mapsforge.org/maps/
- **亚洲/中国**：https://download.mapsforge.org/maps/v5/asia/china/
- **按字母排序浏览**：在 URL 后追加 `?C=S;O=A`

下载后将 `.map` 文件放入 `resources/` 目录，通过环境变量 `MF_MAP` 指定路径：

```bash
MF_MAP=../resources/china.map ./build_sim/bin/pico_nav_sim
```

也可以从 [OpenStreetMap 数据导出](https://download.geofabrik.de/) 获取 `.osm` 文件，再用 [Mapsforge Map Writer](https://github.com/mapsforge/mapsforge/blob/master/docs/Getting-Started-Map-Writer.md) 转换为 `.map` 格式。

### 真机

```bash
# 将 build/bin/demo 拷贝到开发板 (例如 /oem/ 或 /userdata/)，然后:
chmod +x demo
./demo
```

程序打开 `/dev/fb0` 并进入 `app_run()` 主循环，持续驱动 LVGL 刷新界面。

### 模拟器

```bash
cd sim
./build_sim/bin/pico_nav_sim [sample.nmea]   # 可选参数指定 NMEA 文件
```

窗口 480x640（240x320 放大 2 倍）。所有输入都走同一条 `hal_key_read()` 路径（与真机一致），映射如下：

| 输入 | 动作 |
|------|------|
| `↑` / `w` | KEY_BTN_UP |
| `↓` / `s` | KEY_BTN_DOWN |
| `←` / `a` | KEY_BTN_LEFT |
| `→` / `d` | KEY_BTN_RIGHT |
| `Enter` / `Space` / `+` | KEY_BTN_ENTER |
| `Esc` / `-` | KEY_BTN_BACK |
| 鼠标滚轮 | 上滚 = ENTER（放大），下滚 = BACK（缩小） |
| 鼠标左键拖动 | 按拖动方向连发方向键（平移地图） |
| 关闭窗口 | 退出程序 |

#### 无头测试 (CI/自动化)

模拟器支持几个环境变量，方便在无显示器环境下自动验证：

```bash
# 运行 1.2 秒后自动退出
SDL_VIDEODRIVER=dummy SIM_AUTOQUIT_MS=1200 ./build_sim/bin/pico_nav_sim sample.nmea

# 退出前把当前帧缓冲 dump 为 PPM 文件 (检查渲染结果)
SDL_VIDEODRIVER=dummy SIM_AUTOQUIT_MS=1200 \
    SIM_DUMP_PPM=/tmp/sim.ppm ./build_sim/bin/pico_nav_sim sample.nmea

# 回放按键序列 (按 300ms 间隔注入), 验证交互路径
SIM_KEY="right,right,enter,down,back" SIM_KEY_MS=300 \
    SDL_VIDEODRIVER=dummy SIM_AUTOQUIT_MS=3000 ./build_sim/bin/pico_nav_sim

# 指定初始地图视口 (配合 MF_MAP 选择地图文件)
MF_MAP=../resources/macau.map SIM_LAT=22.1935 SIM_LON=113.5451 SIM_ZOOM=14 \
    SDL_VIDEODRIVER=dummy SIM_AUTOQUIT_MS=1500 SIM_DUMP_PPM=/tmp/map.ppm \
    ./build_sim/bin/pico_nav_sim
```

调试开关：`MF_DEBUG=1`（帧对象/线数日志）、`MF_NOCOAST=1`（禁用海岸线渲染，退回瓦片海/陆模型）。

## App 应用详解

> 架构框图与运行流程图见：[Doc/APP_ARCHITECTURE.md](Doc/APP_ARCHITECTURE.md)（分层框图）、[Doc/APP_FLOW.md](Doc/APP_FLOW.md)（主循环/生命周期/时序图）。

### 目录结构

```
App/
├── app.h / app.c              # 应用入口: 组装框架 + 主循环
├── Config/
│   └── app_config.h           # 屏幕分辨率/动画时长/字号等编译期配置
├── Utils/                     # 通用框架组件 (与页面解耦)
│   ├── config_api.h/.c        # 用户态配置存储 API (键值, 对接 /dev/config)
│   ├── config_ioctl_user.h    # 内核 config ioctl 的用户态镜像
│   ├── log.h/.c               # 日志系统 (syslog + stderr 镜像)
│   ├── msg_center.h/.c        # 发布订阅消息总线 (topic 表 + 订阅者链表)
│   └── page_manager.h/.c      # 页面管理器 (页面栈 + 生命周期 + 转场)
└── Pages/                     # 具体页面 (每个页面一个模块)
    ├── app_registry.h/.c      # App 注册表 (名称/图标/page 映射)
    ├── page_app_drawer.h/.c   # 应用抽屉 (App 列表 + 滑动高亮)
    ├── page_dialplate.h/.c    # 表盘页 (速度大字 + 距离/时间卡)
    ├── page_home.h/.c         # 主页 (Carousel 面板切换 + 时间表盘)
    ├── page_livemap.h/.c      # 离线地图页 (Mapsforge 渲染 + 缩放/平移)
    ├── page_settings.h/.c     # 设置页
    └── page_startup.h/.c      # 开机页 (logo, 任意键进入主页)
```

### 分层关系

```
平台入口 (main.c / main_sim.c)
        │
        ▼
   App/app.c ────┬──> Utils/msg_center (事件总线)
        │        └──> Utils/page_manager (页面栈)
        ▼                    │
   Pages/* (页面) ◄──────────┘ (栈顶页接收按键 / 订阅 GPS)
        │
        ▼
   HAL/hal.h (按键/时间/GPS 抽象)
        │
        ▼
   真机 evdev / SDL2 (模拟)
```

### App 核心职责

`App/app.c` 是应用与平台之间的唯一桥梁：

- `app_init()`：依次创建消息中心 → 初始化页面管理器（并订阅 `MSG_KEY`）→ 初始化 HAL（按键/GPS）→ 压入主页 `page_home`（Carousel 面板切换 + 时间表盘）。
- `app_run()`：主循环（由平台 `main` 调用），见 [Doc/APP_FLOW.md](Doc/APP_FLOW.md) 主循环图：
  1. `hal_key_read()` 读按键事件，发布到 `MSG_KEY`；
  2. `hal_gps_read()` 读 GPS 数据，发布到 `MSG_GPS`；
  3. `lv_timer_handler()` 驱动 LVGL 刷新；
  4. `hal_delay_ms(5)` 节流（~200Hz 轮询）。
- `app_msg_center()`：暴露全局消息中心实例，供页面/订阅者使用。

## 应用框架说明

### 消息总线 msg_center（发布/订阅）

**设计**：固定 `topic` 表（`MSG_KEY`、`MSG_GPS`），每个 topic 维护一条订阅者单向链表，`publish` 时遍历链表依次回调。

```
struct msg_center { msg_sub_t *topics[MSG_COUNT]; }
struct msg_sub   { handler; user; next; }        // 链表节点
```

| 函数 | 说明 |
|------|------|
| `msg_center_create()` | 创建实例（calloc 清零 topic 表） |
| `msg_center_publish(mc, topic, data)` | 遍历该 topic 订阅者链表，逐个回调 `handler(topic, data, user)` |
| `msg_center_subscribe(mc, topic, handler, user)` | 头插法挂到 topic 链表，返回节点句柄 |
| `msg_center_unsubscribe(mc, topic, sub)` | 从链表中摘除并释放节点 |
| `msg_center_destroy(mc)` | 释放所有链表节点及实例 |

**使用约定**：
- 页面在 `on_show` 订阅所需消息，`on_hide` 立即退订（避免隐藏页仍在消费事件）。
- 回调入参 `user` 即页面私有数据（`page_t.user`），避免使用全局变量。
- 新增主题：在 `msg_center.h` 的枚举追加并同步 `MSG_COUNT`，无需改其他代码。

### 页面管理器 page_manager（页面栈 + 生命周期 + 转场）

**页面描述结构**：

```
page_t {
    const char      *name;    // 页面名 (调试用)
    const page_ops_t *ops;    // 生命周期回调表
    lv_obj_t        *scr;     // 页面全屏容器 (lv_obj_create(NULL) 生成)
    void            *user;    // 页面私有数据 (create 中分配, destroy 中释放)
}

page_ops_t {
    create(self);   // 首次进栈时构建 UI (self->scr)
    destroy(self);  // 出栈/替换时释放 user 等资源
    on_show(self);  // 成为当前页 (常在此订阅消息)
    on_hide(self);  // 被覆盖/出栈 (常在此退订消息)
    on_key(self, ev); // 收到按键事件 (仅栈顶页会被调用)
}
```

**页面栈操作**（深度上限 `PAGE_STACK_MAX = 8`）：

| 操作 | 行为 | 转场动画 |
|------|------|---------|
| `page_manager_push(page)` | 压栈，新页成为栈顶 | 覆盖滑入 `OVER_LEFT` (500ms ease-out) |
| `page_manager_pop()` | 出栈，回到上一页 | 覆盖滑出 `OVER_RIGHT` (500ms ease-out) |
| `page_manager_replace(page)` | 替换栈顶（不加深，如 Startup→Home） | 淡入 `FADE_ON` (500ms) |

**生命周期时序**（以 push 为例）：
```
1. 旧页 on_hide()      —— 退出前台，退订消息
2. 新页 build(): 首次 create() 构建 UI
3. 新页 on_show()      —— 成为栈顶，订阅消息
4. lv_scr_load_anim()  —— LVGL 转场切换屏幕
```

**按键分发**：页面管理器在 `page_manager_init` 时订阅 `MSG_KEY`，回调中取**栈顶页**并调用其 `on_key`。因此按键天然只作用于当前可见页。

### HAL 抽象层

真机与模拟器通过 `HAL/hal.h` 定义的同一套接口接入，由各自 Makefile 选择编译不同实现：

| 接口 | 真机 (HAL/) | 模拟器 (sim/) |
|------|------------|--------------|
| `hal_key_init/read` | `hal_keys_evdev.c` (evdev) | `hal_keys_sdl.c` (SDL 键盘) |
| `hal_gps_init/read` | `hal_gps_stub.c` (占位) | `hal_gps_nmea.c` (NMEA 回放) |
| `hal_delay_ms/tick_get` | 各自实现 | SDL_Delay/SDL_GetTicks |
| `hal_should_quit` | 恒 0 | 窗口关闭/autoquit 置 1 |

`gps_data_t` 结构: `fix / lat / lon / speed_kmh / alt_m / sats`。

> 注意：`key_id_t` 枚举使用 `KEY_BTN_*` 前缀命名，避免与 Linux 头文件
> `linux/input-event-codes.h` 中的 `KEY_*` 宏（`KEY_UP=103`、`KEY_ENTER=28` 等）
> 发生标识符冲突。曾因冲突导致真机按键被映射成原始 Linux 码而非枚举值，页面无法导航。

### 日志系统 log

基于 **syslog**（`App/Utils/log.h/.c`），用于排查运行故障。所有日志消息均为英文。

**输出**：
- 真机：走 `syslog()` → BusyBox syslogd → `/var/log/messages`
- 模拟器：默认同时镜像到 stderr（终端直接可见）；设 `LOG_CONSOLE=0` 关闭镜像（仅进 syslog）
- PC 主机上模拟器日志落在 `/var/log/syslog`

**用法**：在源文件开头 `#include "App/Utils/log.h"`，程序入口 `main()` 调用 `log_init()`，之后使用宏：

```c
LOG_E("...", ...);   // 错误 (ERR)
LOG_W("...", ...);   // 警告 (WARN)
LOG_I("...", ...);   // 信息 (INFO)
LOG_D("...", ...);   // 调试 (DEBUG)
```

每条日志自动附带 `文件:行号` 与进程号（syslog 端），便于定位代码位置。当前埋点：应用启动/退出、框架初始化（msg_center/page_manager/HAL）、按键事件、GPS 更新、页面 push/pop/replace、evdev 设备扫描、GPS stub 提示。

**已覆盖点**：

| 模块 | 埋点内容 |
|------|---------|
| `main.c` / `main_sim.c` | 启动/退出、fbdev 初始化 |
| `App/app.c` | 框架各组件初始化、按键/GPS 事件 |
| `Utils/page_manager.c` | 页面 push/pop/replace 与栈满告警 |
| `HAL/hal_keys_evdev.c` | 设备扫描/打开/失败 |
| `HAL/hal_gps_stub.c` | GPS 未接入提示 |

### 主循环 app_run()

```
while (!hal_should_quit()) {
    读按键 -> publish(MSG_KEY)
    读GPS -> publish(MSG_GPS)
    lv_timer_handler()
    hal_delay_ms(5)
}
```

## 页面导航

> 当前采用「主页 + App」模型：`app_init()` 压入 `page_home` 作为根页面，通过 Carousel 左右滑动切换面板，从应用抽屉中选择并打开各个 App。

| 操作 | 跳转 |
|------|------|
| Startup 任意键 | replace → Home |
| Home `←/→` | Carousel 面板切换 (时间表盘 / 应用抽屉 / 预留) |
| App Drawer `↑/↓` | 选择 App (滑动高亮条动画) |
| App Drawer `Enter` | push → 打开选中的 App |
| App `Back` | pop → 返回 Home |
| LiveMap `方向键` | 平移地图 |
| LiveMap `Enter` / `Back` | 放大 / 缩小 (最小缩放时 Back 返回 Home) |

## 页面详解

### page_startup 开机页

- **职责**：启动品牌展示，等待用户按键进入主界面。
- **UI**：深色背景 `0x1a1a2e` + 居中蓝色 `0x00c2ff` "Pico\nNav" 大字 logo（48 号字）+ 白色副标题 "GPS Navigation Computer" + 底部版本号 `v0.2 Framework`。
- **按键**：任意键 `page_manager_replace(&page_home)` 直接进入主页（replace 不加深栈）。
- **无私有数据**（不设 `user`）。

### page_home 主页 (Carousel + 时间表盘)

- **职责**：应用根页面，包含 Carousel 面板切换和时间表盘。
- **私有数据** `home_priv_t`：
  ```
  { lv_obj_t *panels[3];      // Carousel 面板 (表盘/应用抽屉/预留)
    lv_obj_t *container;       // Carousel 容器
    int       cur;             // 当前面板索引
    bool      animating;       // 动画锁
    lv_obj_t *time_label;      // 时间标签
    lv_obj_t *date_label;      // 日期标签
    lv_timer_t *clock_timer;   // 每秒刷新时间
    lv_anim_timeline_t *entrance_anim; } // 入场动画时间线
  ```
- **Carousel**：3 个全屏面板通过 `lv_anim_t` 水平滑动切换（500ms ease-out）。面板 0 为时间表盘（渐变壁纸 + 日期/时间/标题），面板 1 为应用抽屉，面板 2 为预留。
- **入场动画**：使用 `lv_anim_timeline` 实现 X-TRACK 风格交错入场（日期滑入 → 时间淡入 → 标题滑入 → 提示淡入）。
- **按键**：
  - `LEFT` / `RIGHT` → Carousel 面板切换
  - `UP` / `DOWN` → 应用抽屉选择项移动
  - `ENTER` → 应用抽屉中打开选中的 App

### page_app_drawer 应用抽屉

- **职责**：显示可用 App 列表，支持选择和打开。
- **UI**：标题 "应用" + 滑动高亮条 + App 列表（图标 + 名称）+ 底部提示。
- **滑动高亮**：独立 `lv_obj_t` 背景条，通过 `lv_anim_t` 以 200ms ease-out 滑动到选中位置。
- **实现**：作为 `page_home` 面板 1 的内容构建，非独立 page_t。

### app_registry App 注册表

- **职责**：维护可用 App 的静态注册表（名称/图标/颜色/page 指针）。
- **新增 App**：只需 ① 创建 Page 文件 ② 在 `s_apps[]` 数组加一行，无需修改框架代码。

### page_dialplate 表盘页（行车信息）

- **职责**：显示当前速度、累计距离与骑行时间。
- **UI**：背景 `0x0f0f1a`；中央 48 号大字显示速度；"km/h" 单位；底部左右为距离与时间。
- **按键**：`BACK` → `page_manager_pop()` 返回主页。

### page_livemap 离线地图页

- **职责**：Mapsforge 离线地图渲染与浏览。
- **渲染**：`mf_query()` 取视口 bbox 内对象 → `mf_render()` 绘制 → 信息条显示 zoom/对象数/渲染耗时。
- **按键**：
  - `UP/DOWN/LEFT/RIGHT` → 平移（单击 32px；按住 70ms 自动重复 24px）
  - `ENTER` → 放大一级
  - `BACK` → 缩小时一级；已最小则 `page_manager_pop()` 返回主页

### page_settings 设置页

- **职责**：设置菜单（亮度/单位/路线偏好/关于）。
- **按键**：
  - `UP` / `DOWN` → 移动选择
  - `LEFT` / `BACK` → `page_manager_pop()` 返回主页
  - `ENTER` → 确认选择

## Mapsforge 离线地图引擎

`Mapsforge/` 是自包含的 Mapsforge 二进制地图（.map v5）解析与渲染库，**不依赖 App 框架**（仅依赖 LVGL 的画布绘制接口与 `mf_map`/`mf_theme`）。整个渲染链路为：**读文件 → 解块 → 帧对象 → 样式解析 → 分遍绘制**。

### 模块职责

| 模块 | 职责 |
|------|------|
| `mf_map` | 打开 .map 文件、解析头部/标签表/子文件表；`mf_query()` 按视口 bbox+zoom 定位瓦片、读取索引与数据块、delta 解码出 POI/Way 帧；把索引中的「全水域瓦片」水标志位一并导出 |
| `mf_theme` | 将每个对象的 OSM 标签解析为样式（组/等级/颜色/线宽/可见 zoom 区间/标注参数）。见「样式分类」 |
| `mf_render` | 墨卡托投影 → 分遍绘制（海陆/绿地/建筑/道路/标注/POI），含扫描线多边形填充、两遍道路绘制、海岸线洪泛填海、文字光晕与碰撞检测 |

三者通过 `mf_frame_t` 衔接：`mf_map` 填帧（对象数组 + 全水域瓦片表），`mf_render` 消费帧并产出画布像素。

### 数据流

```
page_livemap (每帧)
   │ mf_view_bbox()  ── 视口 bbox (微度, 含 8% 边距)
   ▼
mf_query(m, zoom, bbox, &frame)      ← mf_map: 解块, 输出对象数组 + water_tiles
   │
   ▼
mf_render(m, &view, &frame, canvas)  ← mf_render + mf_theme: 分遍绘制到 240x320 RGB565
   │
   ▼
lv_canvas 帧缓冲 → 屏幕
```

---

### mf_map — 地图文件解析

#### 文件头部（`mf_open`）

按 Mapsforge v5 规范顺序解析，全部使用带越界保护的**内存读取器** `mf_reader_t`：

1. **魔数** `"mapsforge binary OSM"`（20 字节）+ 剩余头部大小（4 字节 BE，须 70~2MB）。
2. **文件版本**（4 字节，须 1~5）、**文件大小**（8 字节，与磁盘大小核对）、**地图日期**（8 字节，跳过）。
3. **边界框**：4 个 int32（minLat/minLon/maxLat/maxLon，单位微度）→ 存 `bbox_min`/`bbox_max`。
4. **瓦片尺寸**（2 字节）、**投影名**（字符串，须 `"Mercator"`）。
5. **标志字节** `flags`：0x80 调试文件、0x40 起始坐标、0x20 起始 zoom、0x10 语言偏好、0x08 注释、0x04 创建者。
6. **可选字段**：起始经纬度、起始 zoom、语言/注释/创建者字符串（只读丢弃）。
7. **POI 标签表**与 **Way 标签表**：先 2 字节数量，再逐个读字符串（`"key=value"` 形式），存表供帧对象按 ID 索引。
8. **子文件表**：1 字节数量，每个子文件读 base_zoom/zoom_min/zoom_max + 起始地址/大小（各 8 字节）。随后计算：
   - `index_start_addr`（调试文件跳过 16 字节调试头）；
   - 由地图 bbox 反算**瓦片边界** `boundary_tile_*`、`blocks_width/height`、`num_blocks`、`index_end_addr`（索引每瓦片 5 字节）。

#### 变长整数与可变标签

- **VBE-U**：无符号变长整数，每字节低 7 位、高位为续位（`r_vbe_u`）。
- **VBE-S**：有符号变长整数，末字节第 6 位为符号位（`r_vbe_s`），最多 10 字节、超限视为损坏（此前修复过 `r_vbe_s` 移位未限界的 UB）。
- **可变标签**（`r_consume_tag_value`）：标签值形如 `%b/%h/%i/%f/%s` 时按类型消耗对应字节，保证后续字节流对齐。

#### 瓦片索引与块查询（`mf_query` → `parse_block`）

`mf_query` 流程：

1. 取 zoom 对应的子文件（`mf_get_subfile`）；把 bbox 经纬度转成**该子文件 base_zoom 下的瓦片行列**范围，裁剪到子文件瓦片边界。
2. 逐块读 5 字节索引条目：
   - 高 1 位（`MF_INDEX_WATER_BIT`）为**全水域瓦片**标志 → 收集进 `frame->water_tiles`（带去重）。
   - 低 39 位为块偏移；块大小 = 下一块偏移 − 当前偏移（末块到子文件末尾）。
3. 读整块到内存 → `parse_block` 解码：
   - **zoom 表**：`(zoom_max−zoom_min+1)` 行 × 2 列的累积 POI/Way 计数，取查询 zoom 对应行得到本 zoom 的 POI/Way 数。
   - **POI**：VBE-S 相对瓦片角增量坐标 → 绝对微度；special 字节高 4 位 layer、低 4 位标签数；读标签 + feature 字节（bit7=name、bit6=house、bit5=elevation）；坐标超出 bbox 则跳过。
   - **Way**：way_data_size → 子瓦片位图 → special 字节 → 标签 + feature 字节（bit7=name、bit6=house、bit5=ref、bit4=标签坐标、bit3=way 块数、bit2=double-delta）→ 多个 way 块 × 多个坐标块，逐节点做**增量坐标解码**（首点相对瓦片角，后点相对前点；double-delta 再叠一层），最后按 bbox 包围盒过滤。`way_is_closed` 由首尾节点相同判定。

`mf_frame_free` 统一释放帧内 name/way_nodes/对象数组/水瓦片表。

---

### mf_theme — 样式分类

`mf_style_get()` 按对象类型分发：POI → `classify_poi`，Way → `classify_way`。产出 `mf_style_t`：

```
group      绘制分组 (WATER/LANDUSE/GREEN/PEDAREA/BUILDING/LINE/POI)
rank       组内 z 序 (线: 越大越重要, 后画在上层)
fill/outline   面填充色 / 面描边色
line/casing     线颜色 / 道路描边(壳)颜色
width_q4    zoom15 基准线宽 (1/16 px)
min_zoom   最小可见 zoom
label_zoom / label_max_zoom  标注显示区间 (-1 = 永不标)
label_color / label_size     标注颜色与字号
poi_dot / poi_dot_zoom        POI 圆点颜色与最小 zoom
is_area     该 way 是否按面填充
```

#### Way 分类顺序（命中即返回，优先级从上到下）

1. **水系**：`natural=sea/bay` → 海瓦片矩形（rank 0，画在最底层）；`natural=coastline` → 不在此分类（由专门的洪泛海岸线 pass 处理）；`natural=nosea` → 陆地占位多边形（rank 1，画在海岸线之上）；`natural=water`/`landuse=reservoir/basin` → 内陆水体（rank 2）。
2. **绿地**：forest/wood、park/garden（带名称标注）、grass/meadow、scrub/heath、beach/sand、cemetery、pitch/playground/stadium、swimming_pool。
3. **建成区/土地利用**：residential、commercial/retail、industrial/railway、construction、parking、pier；医院/学校等机构面（rank 6/7）。
4. **建筑**：`building=*`（z≥14，带描边）、航站楼/酒店/博物馆/城堡。
5. **步行广场**：`area=yes` + pedestrian/footway 闭环。
6. **水系线** `waterway=*`、**铁路** `railway=rail`（其余 railway 跳过）、**障碍物** `barrier=*`。
7. **道路**（23 级表，含 motorway→cycleway）：每级定义 rank/颜色/描边/基准宽/min_zoom；隧道（`tunnel=yes`）混合陆地色去壳；rank≥50 从 z12 起标名、rank≥20 从 z14~16 起标名。

#### 线宽缩放

`mf_zoom_width()`：基准宽（z15, 1/16px）× 查表因子 `2^((z−15)·0.75)`（Q8 定点，z0~z21 各一档），结果四舍五入到像素且最小 1px。低 zoom 道路在屏幕上成比例变细，避免低 zoom 糊成一片。

#### POI 分类顺序

1. **地名标注**（无圆点，大字号，zoom 上限隐藏）：country/city/town/village/suburb/hamlet，rank=90（标注最先绘制，最优先）。
2. **类别圆点 + 名称**（30+ 类）：医院/药店/餐饮/商店/旅游/铁路/加油/停车/学校等，每类定义圆点色、圆点最小 zoom、名称最小 zoom；名称在圆点下方 10px。
3. **兜底**：其余 POI（长凳/栏杆/ATM…）z≥17 显示灰色小点，无名称。

---

### mf_render — 渲染管线

`mf_render()` 为单帧入口，绘制顺序严格固定（后画的覆盖先画的）：

```
 1. 网格底 (0xd9d9d9)                 ← 无数据区域标识
 2. 地图 bbox 填陆地色 (0xf5f3ee)      ← 有数据区域的基底
 3. 索引全水域瓦片 → 填海色 (0xa9d3e6)
 4. 海瓦片矩形 (MF_G_WATER rank 0)     ← 粗糙海块兜底
 5. 海岸线洪泛填海 (见下节)             ← 用 coastline 重建真实海陆
 6. nosea 陆地占位 + 内陆水体 (rank 1..1000)   ← 覆盖在海岸线之上
 7. 面: LANDUSE → GREEN → PEDAREA → BUILDING
 8. 线: 先全部描边 (casing pass) 再全部填充 (fill pass)
 9. 标注: 地名 → 道路名 → 绿地名 → POI 圆点+名
10. 后处理: bbox 外重绘无数据网格 (0xc8d8e8 底 + 网格线)
```

#### 投影与视口（`mf_view_*`）

- **Web Mercator**：`mf_view_project` 把微度经纬度 → 画布像素（经度线性缩放、纬度经墨卡托公式）；`project_d` 为双精度版（投影不取整，供海岸线等亚像素精度使用）。
- **平移** `mf_view_pan`：按 zoom 把像素位移 dx/dy 反算为经纬度增量（纬度经墨卡托反变换），并维护微度跟踪值。
- **视口 bbox** `mf_view_bbox`：由中心点 + 画布尺寸 + 8% 边距 `RENDER_MARGIN` 计算可见经纬度矩形，供 `mf_query` 取块。

#### 扫描线多边形填充（`fb_fill_polygon`）

自研 even-odd 扫描线填充，直接写画布帧缓冲：
- 逐行求多边形各边与该行中心线的交点（半开区间规则，正确处理顶点）；
- 交点按插入排序（边数少）后成对填充区间；
- 无三角剖分、无裁剪，任意凹凸/自交多边形均健壮；行范围裁剪到画布。

用于面绘制、岛屿陆地填充。

#### 两遍道路绘制

所有线要素先按 `(layer+5)*100 + rank` 排序（qsort），次要在下、主要在上：
- **第 1 遍**：全部道路画描边壳（宽度 = 主宽 + 2px，`round_start/end` 圆头），先画的壳会被后画的壳/主线覆盖，交叉口自然干净；
- **第 2 遍**：全部道路画主线（宽度 = `mf_zoom_width` 结果）。

#### 海岸线洪泛填海（`fill_coastline`）

**背景**：.map 文件里 `natural=sea/nosea` 多边形是低 zoom 下的粗糙瓦片矩形（数据不可靠，且 coastline 数据中 way 方向不一致），因此当帧内存在 `natural=coastline` 时，改用海岸线真实几何重建海陆。

**算法（洪泛，无多边形拼装）**：

```
 1. 收集 frame 内所有 coastline way → 链 (coast_chain_t)
 2. 按端点匹配合并链 (e6_near, 亚米级, 支持 4 种拼接方向)
 3. 闭合链 = 岛屿 → 扫描线填充陆地色 (0xf5f3ee)
 4. 分配 1-bit 屏障位图 (240×320/8 = 9600 B)
 5. 把所有海岸线段 Bresenham 画成粗屏障线 (半径随 zoom: z<14 为 2, z≥14 为 3)
 6. 从「索引全水域瓦片」四角 (已知海) 做 BFS 4-连通洪泛,
    遇屏障位停止 → visited 位图 = 海
 7. visited 像素 → 直接写帧缓冲为海色 (0xa9d3e6)
```

**为什么用洪泛而非拼环**：旧实现先按「道路节点投票」判定每个 run 的陆地方向，再沿画布边界把 run 连成闭合多边形。低 zoom 数据碎片化时，边界行走会在实际海岸线之外生成大块伪陆地（V 形/跨海陆块），且平移时不稳定。洪泛完全**不依赖 way 方向、不做多边形拼装**：屏障线 + BFS 天然区分海陆，碎片缺口只会造成局部小渗漏而非整块伪陆地。岛屿由闭合链单独填充。

#### 标注（`text_halo` + 碰撞检测）

- 文字绘制为**白色光晕 + 前景色**（上/下/左/右各偏移 1px 画白色，再居中画前景），MAPS.ME 风格、深色道路上可读。
- **碰撞检测**：`draw_ctx_t.lbl[]` 矩形表（上限 `LBL_MAX=96`），每帧累计已标矩形，新标注须不与任何已有矩形相交（`LBL_PAD=2px` 内边距）才绘制。
- **道路名去重**：同名道路 90px 半径内不重复标注（`road_name_seen`），避免分段道路重名刷屏。
- **标注优先级**：地名（rank≥90）→ 道路名（由主到次倒序）→ 绿地/公园名 → POI 名。
- **字体**：检测文本含 `>=0x80` 字节（CJK）自动切 `simsun_16_cjk`，否则按字号用 `montserrat_10/12/14/16`。

#### 性能优化

- **节点抽稀**（`way_to_pixels`）：投影后相邻像素距离 <1.5px 的节点丢弃（闭合线保留尾点），减少绘制段数。
- **数据防护**：`WAY_NODES_MAX=1024` 跳过超大/损坏 way；VBE-S 超 10 字节判损。
- **帧级去重**：全水域瓦片在收集时去重；同名道路标注去重。
- **一次样式解析**：`mf_render` 先为整帧算好 `styles[]`，所有 pass 共享，避免重复标签匹配。
- **洪泛内存**：屏障/visited 位图各 ~9.6KB + BFS 环形队列 ~300KB（上限整屏像素），均用后即释放。

### 关键数据结构

```
mf_frame_t {                     // 一帧查询结果
    mf_obj_t        *objs;       // 对象数组 (POI/Way, 由 mf_query 填充)
    int32_t          count;
    mf_water_tile_t *water_tiles;// 索引全水域瓦片 (base zoom 瓦片行列)
    int32_t          water_tile_cnt;
}
mf_obj_t {
    type; layer; tag_ids[16]; name;
    pos (POI 坐标) | way_nodes[] / way_node_cnt / way_is_closed (Way)
}
mf_style_t { group; rank; fill/outline/line/casing/width_q4;
             min_zoom; label_zoom/max/color/size; poi_dot/poi_dot_zoom; }
mf_view_t { zoom; center_lat/lon; w; h }   // 视口
```

## .map 文件格式参考

### 物理布局总览

```
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

### mf_open 解析流程

1. `memset(m, 0)`，`fopen(path, "rb")`，`fseeko/ftello` 取得 `m->file_size`。
2. 从偏移 0 读 24 字节：

   ```
   bytes[0..19] = MF_MAGIC ("mapsforge binary OSM")
   bytes[20..23] = header_size (BE uint32)
   ```

   校验魔数与 header_size 范围（70 .. 2MB）。

3. 分配 `hdr = malloc(header_size)`，从偏移 24 读入头部，初始化 `mf_reader_t`。
4. 按序解析：

```
version        (4 bytes BE) → m->version
file_size      (8 bytes BE) → verify against ftello
map_date       (8 bytes, skip)
bbox minLat/minLon/maxLat/maxLon (4 x 4 bytes BE, 微度)
tile_size      (2 bytes BE) → m->tile_size
projection     (string)     → must equal "Mercator"
flags          (1 byte)     → m->flags

if (flags & 0x40) { start_pos.lat_e6 + lon_e6 (4B BE ×2) }
if (flags & 0x20) { start_zoom (1 byte) }
if (flags & 0x10) { string (language, skip) }
if (flags & 0x08) { string (comment, skip) }
if (flags & 0x04) { string (created_by, skip) }

POI tags:  uint16 n → m->poi_tag_cnt
           for i in n: string → m->poi_tags[i]
Way tags:  uint16 n → m->way_tag_cnt
           for i in n: string → m->way_tags[i]

subfile_cnt (1 byte, 1..16) → m->subfile_cnt

for each subfile i:
  base_zoom (1)  zoom_min (1)  zoom_max (1)
  start_addr (8 bytes BE)  sub_file_size (8 bytes BE)
```

5. 对每个子文件计算瓦片边界与索引范围：
   - `index_start_addr = start_addr + (is_debug_file ? 16 : 0)`
   - `boundary_tile_*` 由 bbox 与 base_zoom 经 lat/lon→tile 函数求得
   - `blocks_width/height`、`num_blocks`
   - `index_end_addr = index_start_addr + num_blocks * 5`
6. `free(hdr)`，成功打日志返回 `MF_OK`；任何失败走 `fail:` 打 `LOG_E` 并返回错误码。

### FLAGS 位定义

| Bit | 名称 | 说明 |
|-----|------|------|
| 7 | is_debug_file (0x80) | 跳过每个子文件数据块前 16 字节调试头 |
| 6 | optional start position (0x40) | lat+lon |
| 5 | optional start zoom (0x20) | 起始缩放级别 |
| 4 | optional language preference (0x10) | 语言偏好字符串 |
| 3 | optional comment (0x08) | 注释字符串 |
| 2 | optional created-by (0x04) | 创建者字符串 |
| 1-0 | reserved | 必须为 0 |

可选字段是否出现完全由位决定，且**按位序从高到低依次紧排**在 flags 之后——读取方按固定顺序逐位判断即可，无需额外偏移表。

### 数据块解析顺序（parse_block）

```
[debug file : 32-B block signature]
zoom table  : (zoom_max - zoom_min + 1) rows x 2 cols VBE-U
              cumulative (POI, ways) counters;
              row @query zoom => visible object counts
first_way_offset (VBE-U) <- rel. offset of the Way section

--- POI section : repeat pois_on_zoom times ---
  [debug file : 32 bytes]
  lat_delta VBE-S -+
                   +-> absolute micro-degrees (vs tile corner)
  lon_delta VBE-S -+
  special byte    : hi nibble = layer+5, lo nibble = #tags
  tag IDs x n     : VBE-U -> header POI tag table
  feature byte    :
      bit7 name(str)   bit6 house(str)   bit5 elevation(VBE-S)

--- WAY section : repeat ways_on_zoom times ---
  (seek to first_way_offset)
  [debug file : 32 bytes]
  way_data_size    : VBE-U
  subtile bitmap   : u16
  special byte     : same as POI
  tag IDs x n      : VBE-U -> header way tag table
  feature byte     :
      bit7 name(str)     bit6 house(str)     bit5 ref(str)
      bit4 label position (2x VBE-S)
      bit3 explicit #way-blocks (VBE-U), default 1
      bit2 double-delta encoding flag
  way blocks x n {
      coord blocks x m {
          node_count VBE-U  (2..4096)
          first node: delta vs tile corner, VBE-S x2
          next nodes: delta vs prev node,   VBE-S x2
              (one more delta layer if bit2 set)
      }
  }
```

POI是一个点位，一个点位有多个属性，比如，他是一个餐厅，它支持无障碍通行.同时，点位有图层的概念，会根据图层决定渲染顺序

解码完成后按视口 bbox 过滤，幸存者进入 `mf_frame_t`；`way_is_closed` 由首尾节点相等判定。

### 查询工作流（mf_query）

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

### 内存布局

```
mf_map_t (in-memory):
  .fp            FILE*
  .file_size     int64_t
  .version       int32_t
  .tile_size     int32_t
  .flags         int32_t
  .start_zoom    int32_t   (-1 if not present)
  .start_pos     mf_coord_t
  .is_debug_file bool
  .bbox_min / .bbox_max
  .poi_tag_cnt   int32_t   → .poi_tags[] (char**)
  .way_tag_cnt   int32_t   → .way_tags[] (char**)
  .zoom_min / .zoom_max  (global, min/max over all subfiles)
  .subfile_cnt   int32_t
  .subfiles[0..15]   mf_subfile_t array

.subfiles[i] (mf_subfile_t):
  base_zoom, zoom_min, zoom_max
  start_addr, sub_file_size
  index_start_addr, index_end_addr
  boundary_tile_left/right/top/bottom
  blocks_width, height, num_blocks
```

标签字符串作为单独的 malloc 副本存放在 `.poi_tags[]` / `.way_tags[]` 中，打开时一次性解析完毕。

---

## 关键配置说明

| 配置项 | 位置 | 当前值 | 说明 |
|--------|------|--------|------|
| `APP_SCR_HOR/VER` | `App/Config/app_config.h` | 240/320 | 屏幕分辨率 |
| `APP_DISP_BUF` | 同上 | 240*320 | 整屏 RGB565 显示缓冲 |
| `PAGE_ANIM_TIME` | 同上 | 500ms | 页面转场与 Carousel 动画时长 (X-TRACK 标准) |
| `LV_COLOR_DEPTH` | `lv_conf.h` | 16 | RGB565 |
| `LV_TICK_CUSTOM` | `lv_conf.h` | 1 | 使用 `custom_tick_get()` (main.c / main_sim.c) |
| `FBDEV_PATH` | `lv_drv_conf.h` | `/dev/fb0` | frame buffer 设备路径 |
| `USE_EVDEV` | `lv_drv_conf.h` | 0 | 真机按键走自研 evdev HAL，不依赖 LVGL indev |

## 新增页面

1. 在 `App/Pages/` 下新建 `page_xxx.h/.c`
2. 定义 `page_t page_xxx`（实现 `create/on_show/on_hide/on_key` 等回调）
3. 在 `App/Pages/app_registry.c` 的 `s_apps[]` 数组中添加一行注册
4. 在需要跳转的地方 `page_manager_push(&page_xxx)` 即可
5. 无需改 Makefile（`App/Pages/*.c` 通配已覆盖）