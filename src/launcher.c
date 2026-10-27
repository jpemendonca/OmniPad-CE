#include "launcher.h"
#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* The tile is an app with no executable: a param.json in the Media category
 * (applicationCategoryType 65536) whose deeplinkUri the console opens in its browser,
 * plus an icon. Registered with sceAppInstUtil, as ps5-web-file-manager (owendswang)
 * does after ftpsrv and websrv. */

#define LAUNCHER_TITLE_ID "OMNP00001"
#define LAUNCHER_DIR      "/user/app/" LAUNCHER_TITLE_ID

#ifdef __PROSPERO__
#include <ps5/kernel.h>
#include "launcher_icon.h"

int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppInstallAll(void *);

/* Writes path only when its content differs, so an unchanged tile is not re-registered */
static int write_if_changed(const char *path, const void *data, size_t len, int *changed)
{
    static char old[256 * 1024];
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(old, 1, sizeof(old), f);
        fclose(f);
        if (n == len && memcmp(old, data, len) == 0) return 0;
    }
    f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(data, 1, len, f) == len;
    if (fclose(f) != 0) ok = 0;
    if (ok) *changed = 1;
    return ok ? 0 : -1;
}

static int register_app(void)
{
    int (*install_title_dir)(const char *, const char *, void *) = NULL;
    uint32_t handle;
    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle) == 0) {
        install_title_dir = (void *)kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE");
    }
    if (install_title_dir) return install_title_dir(LAUNCHER_TITLE_ID, "/user/app/", NULL);
    return sceAppInstUtilAppInstallAll(NULL);
}

int launcher_install(int port)
{
    char param[512];
    int len = snprintf(param, sizeof(param),
        "{\n"
        "  \"applicationCategoryType\": 65536,\n"
        "  \"titleId\": \"%s\",\n"
        "  \"deeplinkUri\": \"http://127.0.0.1:%d/\",\n"
        "  \"localizedParameters\": {\n"
        "    \"defaultLanguage\": \"en-US\",\n"
        "    \"en-US\": { \"titleName\": \"OmniPad\" },\n"
        "    \"pt-BR\": { \"titleName\": \"OmniPad\" }\n"
        "  }\n"
        "}\n", LAUNCHER_TITLE_ID, port);

    if ((mkdir("/user/app", 0700) && errno != EEXIST) ||
        (mkdir(LAUNCHER_DIR, 0755) && errno != EEXIST) ||
        (mkdir(LAUNCHER_DIR "/sce_sys", 0755) && errno != EEXIST)) {
        log_line("launcher: cannot create %s (%s)", LAUNCHER_DIR, strerror(errno));
        return 0;
    }
    int changed = 0;
    if (write_if_changed(LAUNCHER_DIR "/sce_sys/param.json", param, (size_t)len, &changed) != 0 ||
        write_if_changed(LAUNCHER_DIR "/sce_sys/icon0.png", LAUNCHER_ICON, LAUNCHER_ICON_LEN, &changed) != 0) {
        log_line("launcher: cannot write the tile files (%s)", strerror(errno));
        return 0;
    }
    /* Registered on every start, as the reference does: a tile removed from the home
     * screen comes back even when its files were left in place */
    int err = sceAppInstUtilInitialize();
    if (err) {
        log_line("launcher: sceAppInstUtilInitialize failed (0x%08x)", (unsigned)err);
        return 0;
    }
    err = register_app();
    if (err) {
        log_line("launcher: registering the tile failed (0x%08x)", (unsigned)err);
        return 0;
    }
    log_line("launcher: OmniPad tile %s in the Media tab (opens http://127.0.0.1:%d/)",
             changed ? "installed" : "checked", port);
    return 1;
}

#else

int launcher_install(int port)
{
    (void)port;
    return 1;
}

#endif
