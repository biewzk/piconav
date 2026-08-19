#ifndef SIM_MAIN_SIM_H
#define SIM_MAIN_SIM_H

/* Simulator main-loop quit flag (set by SDL events / window close). */
void sim_set_quit(int q);
int  sim_is_quit(void);

#endif /* SIM_MAIN_SIM_H */