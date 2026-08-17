# pico_nav - 里程表 GUI (LVGL v8.1.0)

Pico Nav 里程表界面程序。基于 LVGL v8.1.0，采用自研的「页面管理器 + 消息总线」应用框架，真机使用 Linux frame buffer (`/dev/fb0`) 显示到 ST7789V 2.4" 屏幕（240x320），并提供 SDL2 PC 模拟器用于日常开发与调试。

## 目录结构

```
pico_nav/
├── main.c                  # 真机入口: LVGL + fbdev 初始化, 调用 app_init/app_run
├── lv_conf.h               # LVGL 配置 (16bit RGB565, LV_TICK_CUSTOM)
├── lv_drv_conf.h           # 显示驱动配置 (FBDEV_PATH=/dev/fb0, USE_EVDEV=0)
├── lvgl/                   # LVGL 源码 (v8.1.0)
├── lv_drivers/             # fbdev 等显示/输入驱动
├── lv_demos/               # LVGL 官方 demo (当前未使用)
├── App/                    # 应用框架与页面
│   ├── app.h / app.c       # 全局消息中心 + app_init/app_run 主循环
│   ├── Config/
│   │   └── app_config.h    # 应用配置 (分辨率/动画时长/字号)
│   ├── Utils/
│   │   ├── log.h / log.c           # 日志系统 (syslog + stderr 镜像)
│   │   ├── msg_center.h/.c     # 发布订阅消息总线 (topic 表 + 订阅者链表)
│   │   └── page_manager.h/.c   # 页面管理器 (页面栈 + 生命周期 + 转场)
│   └── Pages/
│       ├── page_startup.h/.c   # 开机页 (logo, 任意键进入表盘)
│       ├── page_dialplate.h/.c # 表盘页 (速度大字 + 距离/时间卡)
│       ├── page_livemap.h/.c   # 实时地图页 (占位)
│       └── page_settings.h/.c  # 设置页 (占位)
├── HAL/                    # 硬件抽象层 (真机实现)
│   ├── hal.h               # 接口定义: 按键/时间/GPS/退出
│   ├── hal_keys_evdev.c    # 真机按键: 扫描 /dev/input/event*
│   └── hal_gps_stub.c      # 真机 GPS: 占位 (Phase 3 接入)
├── sim/                    # PC 模拟器 (SDL2)
│   ├── main_sim.c / main_sim.h  # SDL 窗口 + LVGL 渲染 + PPM 无头测试
│   ├── hal_keys_sdl.c      # 模拟按键: WASD/方向键/Enter/Space/Esc
│   ├── hal_gps_nmea.c      # 模拟 GPS: NMEA 文件回放
│   ├── sample.nmea         # 示例 NMEA 数据 (GPRMC, 10 条)
│   ├── Makefile            # host 编译规则
│   ├── build_sim.sh        # 模拟器一键编译/运行
│   └── build_sim/          # 模拟器编译产物 (bin/pico_nav_sim)
├── Makefile                # 真机交叉编译规则
├── build.sh                # 真机一键编译 (推荐)
├── APP_ARCHITECTURE.md     # App 框架框图 (Mermaid)
├── APP_FLOW.md             # App 运行流程图 (Mermaid)
└── build/                  # 真机编译产物 (bin/demo)
```

## 编译

### 真机 (ARM 交叉编译)

依赖 buildroot glibc 工具链（与 rootfs 运行库一致）：

```
~/code/odometer/buildroot/buildroot-2023.02.6/output/host/bin/
```

```bash
cd gui/pico_nav
./build.sh          # 编译
./build.sh clean    # 清理后重新编译
```

产物：`build/bin/demo`（ARM 32 位 ELF，动态链接 glibc）。

### PC 模拟器 (host 编译)

依赖 SDL2（Ubuntu/Debian: `sudo apt install libsdl2-dev`）：

```bash
cd gui/pico_nav/sim
./build_sim.sh          # 编译 -> build_sim/bin/pico_nav_sim
./build_sim.sh run      # 编译后直接运行 (默认回放 sample.nmea)
./build_sim.sh run my.nmea   # 回放指定 NMEA 文件
```

也可以在项目根目录执行 `./build.sh sim` 一键编译模拟器。

## 运行

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

窗口 480x640（240x320 放大 2 倍），键盘映射如下：

| 键位 | 动作 |
|------|------|
| `↑` / `w` | KEY_BTN_UP |
| `↓` / `s` | KEY_BTN_DOWN |
| `←` / `a` | KEY_BTN_LEFT |
| `→` / `d` | KEY_BTN_RIGHT |
| `Enter` / `Space` | KEY_BTN_ENTER |
| `Esc` | KEY_BTN_BACK |
| 关闭窗口 | 退出程序 |

#### 无头测试 (CI/自动化)

模拟器支持两个环境变量，方便在无显示器环境下自动验证：

```bash
# 运行 1.2 秒后自动退出
SDL_VIDEODRIVER=dummy SIM_AUTOQUIT_MS=1200 ./build_sim/bin/pico_nav_sim sample.nmea

# 退出前把当前帧缓冲 dump 为 PPM 文件 (检查渲染结果)
SDL_VIDEODRIVER=dummy SIM_AUTOQUIT_MS=1200 \
    SIM_DUMP_PPM=/tmp/sim.ppm ./build_sim/bin/pico_nav_sim sample.nmea
```

## App 应用详解

> 架构框图与运行流程图见：[APP_ARCHITECTURE.md](APP_ARCHITECTURE.md)（分层框图）、[APP_FLOW.md](APP_FLOW.md)（主循环/生命周期/时序图）。

### 目录结构

```
App/
├── app.h / app.c              # 应用入口: 组装框架 + 主循环
├── Config/
│   └── app_config.h           # 屏幕分辨率/动画时长/字号等编译期配置
├── Utils/                     # 通用框架组件 (与页面解耦)
│   ├── msg_center.h/.c        # 发布订阅消息总线
│   └── page_manager.h/.c      # 页面管理器 (页面栈 + 生命周期 + 转场)
└── Pages/                     # 具体页面 (每个页面一个模块)
    ├── page_startup.h/.c
    ├── page_dialplate.h/.c
    ├── page_livemap.h/.c
    └── page_settings.h/.c
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

- `app_init()`：依次创建消息中心 → 初始化页面管理器（并订阅 `MSG_KEY`）→ 初始化 HAL（按键/GPS）→ 压入开机页 `page_startup`。
- `app_run()`：主循环（由平台 `main` 调用），见 [APP_FLOW.md](APP_FLOW.md) 主循环图：
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
| `page_manager_push(page)` | 压栈，新页成为栈顶 | 左移进入 `MOVE_LEFT` |
| `page_manager_pop()` | 出栈，回到上一页 | 右移返回 `MOVE_RIGHT` |
| `page_manager_replace(page)` | 替换栈顶（不加深，如 Startup→Dialplate） | 淡入 `FADE_ON` |

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

| 操作 | 跳转 |
|------|------|
| Startup 任意键 | replace → Dialplate |
| Dialplate `Enter` | push → Settings |
| Dialplate `←/→` | push → LiveMap |
| Settings `←` | pop → 返回 Dialplate |
| LiveMap `←/→/Enter` | pop → 返回 Dialplate |

## 页面详解

### page_startup 开机页

- **职责**：启动品牌展示，等待用户按键进入主界面。
- **UI**：深色背景 `0x1a1a2e` + 居中蓝色 `0x00c2ff` "Pico\nNav" 大字 logo（48 号字）+ 白色副标题 "GPS Navigation Computer" + 底部版本号 `v0.2 Framework`。
- **按键**：任意键 `page_manager_replace(&page_dialplate)` 直接进入表盘（replace 不加深栈）。
- **无私有数据**（不设 `user`）。

### page_dialplate 表盘页（主界面）

- **职责**：实时显示当前速度、累计距离与骑行时间（Phase 3 前为 GPS 模拟数据）。
- **私有数据** `dial_priv_t`：
  ```
  { lv_obj_t *speed_label;  // 速度大字
    lv_obj_t *dist_label;   // 距离
    lv_obj_t *time_label;   // 时间
    msg_sub_t *gps_sub; }   // MSG_GPS 订阅句柄
  ```
- **UI**：背景 `0x0f0f1a`；中央 48 号大字显示速度（有定位白色、无定位灰色 `0x555555`）；"km/h" 单位；底部左右为距离 `X.X km` 与时间 `00:00`；底部提示 `OK:菜单 L/R:地图`。
- **消息**：`on_show` 订阅 `MSG_GPS`（`dial_on_gps` → 更新速度/距离），`on_hide` 退订。
- **按键**：
  - `KEY_BTN_ENTER` → `push(&page_settings)`（设置菜单）
  - `KEY_BTN_LEFT` / `KEY_BTN_RIGHT` → `push(&page_livemap)`（实时地图）
- **注意**：`on_key` 只响应按下（`ev->is_press`），避免重复事件触发多次跳转。

### page_livemap 实时地图页

- **职责**：Phase 4 地图占位，验证从表盘进入/返回的导航链路。
- **UI**：背景 `0x12101a` + 标题 "LiveMap" + 居中占位文本 `地图占位 (Phase 4)` + 底部提示 `LEFT/RIGHT: 返回`。
- **按键**：`KEY_BTN_LEFT` / `KEY_BTN_RIGHT` / `KEY_BTN_ENTER` 任一 → `page_manager_pop()` 返回表盘。
- **无私有数据**、无消息订阅。

### page_settings 设置页

- **职责**：设置项列表 + 上下选择高亮（Phase 3 挂接实际设置逻辑）。
- **私有数据** `settings_priv_t`：
  ```
  { lv_obj_t *labels[4];  // 各设置项 label
    int       sel; }      // 当前选中项
  ```
- **UI**：背景 `0x0f0f1a` + 标题 "设置" + 4 项列表：`亮度 / 单位 / 路线偏好 / 关于`（选中项蓝色文字 `0x00c2ff` + 深蓝背景 `0x223344`，未选中灰色）；底部提示 `UP/DOWN:选择 OK:确认 LEFT:返回`。
- **按键**：
  - `KEY_BTN_UP` / `KEY_BTN_DOWN` → 移动 `sel`（循环 0..3）并重绘
  - `KEY_BTN_LEFT` → `page_manager_pop()` 返回表盘
  - `KEY_BTN_ENTER` → 占位（Phase 3 挂接）

## 关键配置说明

| 配置项 | 位置 | 当前值 | 说明 |
|--------|------|--------|------|
| `APP_SCR_HOR/VER` | `App/Config/app_config.h` | 240/320 | 屏幕分辨率 |
| `APP_DISP_BUF` | 同上 | 240*320 | 整屏 RGB565 显示缓冲 |
| `PAGE_ANIM_TIME` | 同上 | 250ms | 页面转场时长（按 fbtft ~30fps 调优） |
| `LV_COLOR_DEPTH` | `lv_conf.h` | 16 | RGB565 |
| `LV_TICK_CUSTOM` | `lv_conf.h` | 1 | 使用 `custom_tick_get()` (main.c / main_sim.c) |
| `FBDEV_PATH` | `lv_drv_conf.h` | `/dev/fb0` | frame buffer 设备路径 |
| `USE_EVDEV` | `lv_drv_conf.h` | 0 | 真机按键走自研 evdev HAL，不依赖 LVGL indev |

## 新增页面

1. 在 `App/Pages/` 下新建 `page_xxx.h/.c`
2. 定义 `page_t page_xxx`（实现 `create/on_show/on_hide/on_key` 等回调）
3. 在需要跳转的地方 `page_manager_push(&page_xxx)` 即可
4. 无需改 Makefile（`App/Pages/*.c` 通配已覆盖）