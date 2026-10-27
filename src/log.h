#ifndef LOG_H
#define LOG_H

#include <stddef.h>

void log_init(const char *path);
void log_line(const char *fmt, ...);
void log_close(void);
void notify_ps5(const char *fmt, ...);

/* pt when the console's system language is Portuguese, en otherwise (for notifications) */
const char *ui_text(const char *pt, const char *en);

#endif /* LOG_H */
