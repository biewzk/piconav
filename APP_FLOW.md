# App 运行流程图 (Flowchart)

## 1. 主循环 app_run()

```mermaid
flowchart TB
    S([程序启动]) --> INIT["app_init()<br/>创建 msg_center<br/>page_manager_init(订阅 MSG_KEY)<br/>hal_key_init / hal_gps_init<br/>push page_startup"]
    INIT -->     LOOP{"app_run() 主循环<br/>while !hal_should_quit"}

    LOOP --> KEY["hal_key_read()<br/>读按键事件"]
    KEY -- "有事件" --> PUBK["msg_center_publish(MSG_KEY)"]
    PUBK --> DISPATCH["page_manager 订阅者<br/>分发到栈顶页 on_key"]
    DISPATCH --> NAV{"按键跳转?"}
    KEY -- "无事件" --> GPS

    NAV -- "Startup 任意键" --> R["page_manager_replace<br/>-> page_dialplate"]
    NAV -- "Dialplate Enter" --> P1["page_manager_push<br/>-> page_settings"]
    NAV -- "Dialplate Left/Right" --> P2["page_manager_push<br/>-> page_livemap"]
    NAV -- "Settings Left" --> POP["page_manager_pop<br/>-> 返回 dialplate"]
    NAV -- "LiveMap Left/Right/Enter" --> POP

    GPS["hal_gps_read()<br/>读 GPS 数据"]
    GPS -- "新数据" --> PUBG["msg_center_publish(MSG_GPS)"]
    PUBG --> UPD["订阅者回调更新 UI<br/>如 dialplate 速度/距离标签"]
    GPS -- "无数据" --> TIMER

    R --> TIMER
    P1 --> TIMER
    P2 --> TIMER
    POP --> TIMER
    UPD --> TIMER

    TIMER["lv_timer_handler()<br/>刷新界面"]
    TIMER --> DELAY["hal_delay_ms(5)"]
    DELAY --> LOOP

    LOOP -- "hal_should_quit()=1<br/>(模拟器关窗口/autoquit)" --> E([退出])
```

## 2. 页面生命周期 (page_manager)

```mermaid
sequenceDiagram
    participant PM as page_manager
    participant P as page_t
    participant MC as msg_center
    participant LV as LVGL

    Note over PM,LV: page_manager_push(page)
    PM->>P: page_build(): 首次则 lv_obj_create + ops->create
    PM->>P: 旧页 ops->on_hide (退出前台)
    PM->>P: 新页 ops->on_show (订阅消息, 刷新)
    PM->>LV: lv_scr_load_anim(新页, MOVE_LEFT)
    Note over PM,LV: 栈顶 = 新页, 接收 MSG_KEY/MSG_GPS

    Note over PM,LV: page_manager_pop()
    PM->>P: 旧页 ops->on_hide
    PM->>P: 新栈顶 ops->on_show
    PM->>LV: lv_scr_load_anim(上一页, MOVE_RIGHT)
    PM->>P: 旧页 ops->destroy + lv_obj_del
```

## 3. 消息发布/订阅时序

```mermaid
sequenceDiagram
    participant APP as "app_run()"
    participant MC as "msg_center"
    participant PM as "page_manager (订阅 MSG_KEY)"
    participant PG as "dialplate (订阅 MSG_GPS)"

    APP->>MC: publish(MSG_KEY, key_event)
    MC->>PM: 回调 page_key_handler
    PM->>PM: 取栈顶页 -> on_key(ev)

    APP->>MC: publish(MSG_GPS, gps_data)
    MC->>PG: 回调 dial_on_gps
    PG->>PG: 更新速度/距离 label
```

> 说明：
> - 按键事件统一经 `MSG_KEY` 总线，由页面管理器**按栈顶分发**，各页只需实现 `on_key`。
> - GPS 数据经 `MSG_GPS` 总线广播，表盘页在 `on_show` 订阅、`on_hide` 退订，页面隐藏时不消费。
> - 页面切换用 LVGL 转场：push 左移进入 (MOVE_LEFT)，pop 右移返回 (MOVE_RIGHT)，replace 用淡入 (FADE_ON)。
