#ifndef LAUNCHER_H
#define LAUNCHER_H

/* Installs (or refreshes) the OmniPad tile in the PS5's Media tab. Opening the tile shows the
 * dashboard on http://127.0.0.1:<port>/ in the console's own browser, while the payload runs.
 * Returns 1 on success. */
int launcher_install(int port);

#endif /* LAUNCHER_H */
