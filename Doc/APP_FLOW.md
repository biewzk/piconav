# App 运行流程图 (Flowchart)

## 1. 主循环 app_run()

```mermaid
flowchart TB
    S([程序启动]) --> INIT["app_init()<br/>创建 msg_center<br/>page_manager_init(订阅 MSG_KEY)<br/>hal_key_init()<br/>push page_livemap (根页面)"]
    INIT --> LOOP{"app_run() 主循环<br/>while !hal_should_quit"}

    LOOP --> KEY["hal_key_read()<br/>读按键事件"]
    KEY -- "有事件" --> PUBK["msg_center_publish(MSG_KEY)"]
    PUBK --> DISPATCH["page_manager 订阅者<br/>分发到栈顶页 on_key"]
    DISPATCH --> HANDLE["栈顶页 on_key 处理<br/>(平移/缩放/跳转)"]
    HANDLE --> TIMER
    KEY -- "无事件" --> TIMER

    TIMER["lv_timer_handler()<br/>刷新界面"]
    TIMER --> DELAY["hal_delay_ms(5)"]
    DELAY --> LOOP

    LOOP -- "hal_should_quit()=1<br/>(模拟器关窗口/autoquit)" --> E([退出])
```

> 注：当前 HAL 未接入 GPS，主循环只轮询按键，无 GPS 读取/发布。

## 2. 页面导航 (当前: 地图开发模式)

> 当前 `app_init()` 直接压入 `page_livemap` 作为**根页面**。表盘/开机/设置页仍在代码中，可经 `page_manager` 恢复，但当前入口不经过它们。

```mermaid
flowchart TB
    START([app_init]) --> ROOT["page_manager_push(&page_livemap)<br/>根页面: 离线地图"]

    subgraph LIVE["page_livemap (当前可见)"]
        L1["UP/DOWN/LEFT/RIGHT -> 平移 32px<br/>(按住 70ms 重复 24px 连续平移)"]
        L2["ENTER -> 放大一级"]
        L3["BACK -> 缩小一级"]
    end
    ROOT --> LIVE

    subgraph DORMANT["其余页面 (代码已实现, 当前不作为入口)"]
        D1["page_startup: 任意键 -> replace page_dialplate"]
        D2["page_dialplate: Enter -> push settings<br/>Left/Right -> push livemap"]
        D3["page_settings: Up/Down 选择<br/>Left -> pop 返回 dialplate<br/>Enter 占位"]
    end
    START -. 可恢复 .-> DORMANT
```

## 3. 页面生命周期 (page_manager)

```mermaid
sequenceDiagram
    participant PM as page_manager
    participant P as page_t
    participant LV as LVGL

    Note over PM,LV: page_manager_push(page)
    PM->>P: page_build(): 首次则 lv_obj_create + ops->create
    PM->>P: 旧页 ops->on_hide (退出前台)
    PM->>P: 新页 ops->on_show (订阅消息, 刷新)
    PM->>LV: lv_scr_load_anim(新页, MOVE_LEFT)
    Note over PM,LV: 栈顶 = 新页, 接收 MSG_KEY

    Note over PM,LV: page_manager_pop()
    PM->>P: 旧页 ops->on_hide
    PM->>P: 新栈顶 ops->on_show
    PM->>LV: lv_scr_load_anim(上一页, MOVE_RIGHT)
    PM->>P: 旧页 ops->destroy + lv_obj_del

    Note over PM,LV: page_manager_replace(page)
    PM->>P: 旧页 ops->on_hide
    PM->>P: 新页 ops->on_show
    PM->>LV: lv_scr_load_anim(新页, FADE_ON)
    PM->>P: 旧页 ops->destroy (深度不变)
```

## 4. 消息发布/订阅时序

```mermaid
sequenceDiagram
    participant APP as "app_run()"
    participant MC as "msg_center"
    participant PM as "page_manager (订阅 MSG_KEY)"

    APP->>MC: publish(MSG_KEY, key_event)
    MC->>PM: 回调 page_key_handler
    PM->>PM: 取栈顶页 -> on_key(ev)
    PM->>PM: 栈顶页处理 (平移/缩放/页面跳转)
```

> 说明：
> - 按键事件统一经 `MSG_KEY` 总线，由页面管理器在 `page_manager_init` 时订阅，回调中取**栈顶页**调用其 `on_key`；各页只需实现 `on_key`。
> - 页面切换用 LVGL 转场：push 左移进入 (MOVE_LEFT)，pop 右移返回 (MOVE_RIGHT)，replace 淡入 (FADE_ON)，时长 250ms（`PAGE_ANIM_TIME`）。