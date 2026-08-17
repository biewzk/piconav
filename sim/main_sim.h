#ifndef SIM_MAIN_SIM_H
#define SIM_MAIN_SIM_H

/* 模拟器主循环退出标志 (由 SDL 事件/窗口关闭设置) */
void sim_set_quit(int q);
int  sim_is_quit(void);

#endif /* SIM_MAIN_SIM_H */