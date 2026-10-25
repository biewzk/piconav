#ifndef PAGES_BUILTIN_H
#define PAGES_BUILTIN_H

/*
 * Register every built-in page (page registry) and drawer app (app
 * registry). Called once by app_init() before the first navigation;
 * plugin loading happens right after, so plugin entries land below the
 * built-ins in the drawer.
 */
void pages_builtin_register(void);

#endif /* PAGES_BUILTIN_H */
