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
            PL["page_livemap<br/>地图占位"]
            PSE["page_settings<br/>设置页"]
        end
        CFG["Config/app_config.h<br/>分辨率/动画时长/字号"]
    end

    subgraph HAL["硬件抽象层 (hal.h 接口)"]
        HK["hal_keys_evdev.c<br/>真机按键 /dev/input"]
        HG["hal_gps_stub.c<br/>真机 GPS 占位"]
        SK["hal_keys_sdl.c<br/>模拟按键 WASD/方向键"]
        SG["hal_gps_nmea.c<br/>模拟 GPS NMEA 回放"]
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
    PM --> PAGES
    PAGES --> CFG
    PM --> MC
    PM --> DISP

    APPINIT -- 读按键/读GPS --> HK
    APPINIT -- 读按键/读GPS --> SK
    APPINIT -- 读GPS --> HG
    APPINIT -- 读GPS --> SG

    TIMER --> DISP
    SCRLOAD --> DISP

    classDef layer fill:#1a1a2e,stroke:#00c2ff,color:#fff
    class MAIN,SIM,HK,HG,SK,SG layer
    classDef core fill:#0f0f1a,stroke:#00c2ff,color:#eee
    class APPINIT,MC,PM core
```

> 说明：
> - 真机与模拟器通过 **HAL 接口**（`hal.h`）隔离，由各 Makefile 选择编译不同实现，App 层代码完全复用。
> - **消息总线**（msg_center）负责按键/GPS 事件分发；**页面管理器**（page_manager）负责页面栈与转场。
> - 页面私有 UI 数据放在 `page_t.user`，由 `create/destroy` 管理，与页面栈生命周期绑定。
