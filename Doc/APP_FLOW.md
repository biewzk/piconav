# App 运行流程图 (Flowchart)

## 1. 主循环 app_run()

```mermaid
flowchart TB
    S([程序启动]) --> INIT["app_init()<br/>创建 msg_center<br/>page_manager_init(订阅 MSG_KEY)<br/>hal_key_init()<br/>push page_home (根页面)"]
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

## 2. 页面导航 (主页 + App 模型)

> `app_init()` 压入 `page_home` 作为**根页面**。主页包含 3 个 Carousel 面板（时间表盘 / 应用抽屉 / 预留），通过 LEFT/RIGHT 滑动切换。从应用抽屉中选择 App 并 push 打开，BACK 键 pop 回主页。

```mermaid
flowchart TB
    START([app_init]) --> ROOT["page_manager_push(&page_home)<br/>根页面: Carousel + 时间表盘"]

    subgraph HOME["page_home (depth=1, 根页面)"]
        direction LR
        P0["面板0: 时间表盘<br/>渐变壁纸 + 日期/时间<br/>入场动画 (lv_anim_timeline)"]
        P1["面板1: 应用抽屉<br/>App列表 + 滑动高亮<br/>ENTER打开App"]
        P2["面板2: 预留"]
        P0 <-. LEFT/RIGHT .-> P1 <-. LEFT/RIGHT .-> P2
    end
    ROOT --> HOME

    subgraph APPS["App 页面 (depth=2, 从应用抽屉 push)"]
        A1["page_livemap<br/>离线地图<br/>方向键平移<br/>ENTER放大 BACK缩小/返回"]
        A2["page_dialplate<br/>行车信息<br/>BACK返回"]
        A3["page_settings<br/>设置菜单<br/>BACK/LEFT返回"]
    end
    HOME -- "ENTER (面板1)" --> APPS
    APPS -- "BACK" --> HOME

    subgraph STARTUP["启动流程"]
        S1["page_startup<br/>任意键 replace → page_home"]
    end
    STARTUP -.-> ROOT
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
    PM->>LV: lv_scr_load_anim(新页, OVER_LEFT)
    Note over PM,LV: 栈顶 = 新页, 接收 MSG_KEY

    Note over PM,LV: page_manager_pop()
    PM->>P: 旧页 ops->on_hide
    PM->>P: 新栈顶 ops->on_show
    PM->>LV: lv_scr_load_anim(上一页, OVER_RIGHT)
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
> - 页面切换用 LVGL 转场：push 覆盖滑入 (OVER_LEFT)，pop 覆盖滑出 (OVER_RIGHT)，replace 淡入 (FADE_ON)，时长 500ms（`PAGE_ANIM_TIME`）。