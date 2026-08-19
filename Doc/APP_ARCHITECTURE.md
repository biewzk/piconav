# App 框架框图 (Block Diagram)

```mermaid
flowchart TB
    subgraph PLATFORM["平台入口"]
        MAIN["main.c (真机)<br/>LVGL + fbdev (/dev/fb0)<br/>custom_tick_get"]
        SIM["main_sim.c (模拟器)<br/>LVGL + SDL2 窗口 480x640<br/>custom_tick_get"]
    end

    subgraph APP["应用层 App/"]
        APPINIT["app.c<br/>app_init(): 组装框架<br/>app_run(): 主循环"]
        MC["Utils/msg_center<br/>消息总线<br/>topic 表 + 订阅者链表"]
        PM["Utils/page_manager<br/>页面栈(PAGE_STACK_MAX=8)<br/>生命周期 + LVGL 转场"]
        subgraph PAGES["Pages/ 页面"]
            PS["page_startup<br/>开机页"]
            PD["page_dialplate<br/>表盘页"]
            PL["page_livemap<br/>离线地图页(Mapsforge)<br/>根页面"]
            PSE["page_settings<br/>设置页"]
        end
        CFG["Config/app_config.h<br/>分辨率/动画时长/字号"]
    end

    subgraph MF["地图引擎 Mapsforge/"]
        MFM["mf_map.c<br/>.map 解析 + 帧查询"]
        MFR["mf_render.c<br/>投影/填充/道路/海岸线/标注"]
        MFT["mf_theme.c<br/>OSM 标签 -> 样式"]
    end

    subgraph HAL["硬件抽象层 (hal.h 接口)"]
        HK["hal_keys_evdev.c<br/>真机按键 /dev/input"]
        SK["hal_keys_sdl.c<br/>模拟按键 WASD/方向键/滚轮"]
    end

    subgraph LVGL["LVGL v8.1.0"]
        TIMER["lv_timer_handler()"]
        SCRLOAD["lv_scr_load_anim()<br/>屏幕切换"]
        DISP["lv_disp 240x320"]
    end

    MAIN --> APPINIT
    SIM --> APPINIT
    APPINIT --> MC
    APPINIT --> PM
    APPINIT --> HK
    APPINIT --> SK
    PM --> PAGES
    PL --> MFM
    PL --> MFR
    MFR --> MFT
    PAGES --> CFG
    PM --> MC
    PM --> DISP

    TIMER --> DISP
    SCRLOAD --> DISP

    classDef layer fill:#1a1a2e,stroke:#00c2ff,color:#fff
    class MAIN,SIM,HK,SK layer
    classDef core fill:#0f0f1a,stroke:#00c2ff,color:#eee
    class APPINIT,MC,PM,MFR core
```

> 说明：
> - 真机与模拟器通过 **HAL 接口**（`hal.h`）隔离，由各 Makefile 选择编译不同实现，App 层代码完全复用。HAL 当前只抽象按键（`hal_key_init/read`）与时间（`hal_delay_ms/tick_get`），GPS 尚未接入。
> - **消息总线**（msg_center）负责按键事件分发（当前仅 `MSG_KEY` 一个主题）；**页面管理器**（page_manager）负责页面栈与转场。
> - **地图引擎**（Mapsforge/）为自包含库：`mf_map` 解析 `.map` 文件并按视口 bbox 查询帧对象，`mf_theme` 将 OSM 标签映射为样式，`mf_render` 执行多遍绘制（海/陆、绿地、建筑、道路两遍、海岸线洪泛、标注）。`page_livemap` 是当前应用的根页面，直接消费地图引擎输出。
> - 页面私有 UI 数据放在 `page_t.user`，由 `create/destroy` 管理，与页面栈生命周期绑定。