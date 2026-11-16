#ifndef WEB_H
#define WEB_H

/* Starts the dashboard server on its own thread. Besides the dashboard and its API (the table
 * in SPECS.md), it serves the Media tab icon's page:
 *   GET /start             web/start.html, kept by the browser through /omnipad.appcache: it
 *                          opens the dashboard when OmniPad answers /ping or /ping.gif, and says
 *                          how to start OmniPad when it does not
 *   GET /omnipad.appcache  the AppCache manifest of /start, changed with the page and the version
 *   GET /ping, /ping.gif   "omnipad-ok" and a 1x1 GIF, readable from any origin */
int web_init(int port);
void web_cleanup(void);

#endif /* WEB_H */
