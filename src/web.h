#ifndef WEB_H
#define WEB_H

/* Starts the dashboard server on its own thread */
int web_init(int port);
void web_cleanup(void);

#endif /* WEB_H */
