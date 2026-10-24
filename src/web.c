#include "web.h"
#include "ps5_vpad.h"
#include "usb_hotplug.h"
#include "shellui_inject.h"
#include "version.h"
#include "log.h"
#include "util.h"
#include "web_index.h"  /* generated from web/index.html by tools/embed_web.py */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

static int g_server_fd = -1;
static pthread_t g_server_thread;
static volatile int g_server_running = 0;

static void *web_server_worker(void *arg);

static long g_started_at = 0;

/* Copies s into a JSON string body, escaping quotes, backslashes and control chars */
static void json_escape(char *out, size_t cap, const char *s)
{
    size_t o = 0;
    for (; s && *s && o + 7 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c < 0x20) o += (size_t)snprintf(out + o, cap - o, "\\u%04x", c);
        else out[o++] = (char)c;
    }
    out[o] = '\0';
}

/* write() may send only part of a large body (the page is tens of KB) */
static void write_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t w = write(fd, p, len);
        if (w <= 0) return;
        p += w;
        len -= (size_t)w;
    }
}

#ifndef SO_REUSEPORT
#define SO_REUSEPORT 0x0200
#endif

int web_init(int port)
{
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) return 0;

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_addr.s_addr = INADDR_ANY;

    int bound = 0;
    for (int retry = 0; retry < 10; retry++) {
        if (bind(g_server_fd, (struct sockaddr *)&sin, sizeof(sin)) == 0) {
            bound = 1;
            break;
        }
        if (retry == 0) {
            log_line("web: port %d busy, waiting for release...", port);
        }
        usleep(250000); /* 250ms */
    }

    if (!bound) {
        log_line("web: bind error on port %d (%s)", port, strerror(errno));
        close(g_server_fd);
        g_server_fd = -1;
        return 0;
    }

    if (listen(g_server_fd, 8) != 0) {
        close(g_server_fd);
        g_server_fd = -1;
        return 0;
    }

    g_started_at = now_ms();
    g_server_running = 1;
    if (pthread_create(&g_server_thread, NULL, web_server_worker, NULL) != 0) {
        log_line("web: failed to start server thread");
        g_server_running = 0;
        close(g_server_fd);
        g_server_fd = -1;
        return 0;
    }

    log_line("web: Dashboard server listening at http://0.0.0.0:%d/", port);
    return 1;
}

static void handle_client(int cfd)
{
    /* Bound how long one slow client can hold the server thread */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; /* 100 ms */
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct pollfd pfd;
    pfd.fd = cfd;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, 100);
    if (pr <= 0) { close(cfd); return; }

    char req[2048];
    ssize_t n = read(cfd, req, sizeof(req) - 1);
    if (n <= 0) { close(cfd); return; }
    req[n] = '\0';

    char res[4096];
    int res_len = 0;

    if (strncmp(req, "OPTIONS", 7) == 0) {
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n");
        write(cfd, res, (size_t)res_len);
        close(cfd);
        return;
    }

    if (strncmp(req, "GET /api/state", 14) == 0) {
        /* Live view for the dashboard, polled ~30 times per second: what each controller
         * is sending right now, plus counters the page turns into rates. */
        long now = now_ms();
        char json[3072];
        int jlen = snprintf(json, sizeof(json),
            "{\"version\":\"%s\",\"build\":%d,\"uptime_ms\":%ld,\"now_ms\":%ld,\"slots\":[",
            ANYPAD_VERSION, ANYPAD_BUILD, now - g_started_at, now);

        for (int i = 0; i < MAX_SLOTS; i++) {
            vpad_slot_info_t info;
            vpad_get_slot_info(i, &info);
            const pad_state_t *in = &info.last_input;
            uint16_t vid = in->vid, pid = in->pid;
            if (!vid && !pid) usb_hotplug_slot_ids(i, &vid, &pid);  /* before the first report */
            char name[96];
            json_escape(name, sizeof(name), info.name);
            jlen += snprintf(json + jlen, sizeof(json) - (size_t)jlen,
                "%s{\"status\":%d,\"name\":\"%s\",\"conn\":%d,\"vid\":%u,\"pid\":%u,\"user_id\":%d,"
                "\"battery\":%d,\"charging\":%d,\"injected\":%u,\"inputs\":%u,\"input_age_ms\":%ld,"
                "\"connected_ms\":%ld,\"idle_release_ms\":%ld,"
                "\"buttons\":%u,\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d,\"l2\":%d,\"r2\":%d}",
                i > 0 ? "," : "",
                info.status, name, info.conn_type, vid, pid, info.user_id,
                info.battery_level, info.battery_charging,
                (unsigned)info.packets_injected, (unsigned)info.inputs_received,
                info.last_input_time ? now - info.last_input_time : -1L,
                info.connected_time ? now - info.connected_time : -1L,
                usb_hotplug_idle_release_ms(i),
                (unsigned)in->buttons, in->lx, in->ly, in->rx, in->ry, in->l2, in->r2);
        }
        jlen += snprintf(json + jlen, sizeof(json) - (size_t)jlen, "]}");
        if (jlen >= (int)sizeof(json)) jlen = (int)sizeof(json) - 1;

        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Cache-Control: no-store\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n\r\n", jlen);
        write(cfd, res, (size_t)res_len);
        write_all(cfd, json, (size_t)jlen);
    }
    else if (strncmp(req, "GET /api/status", 15) == 0) {
        char json[2048];
        int jlen = snprintf(json, sizeof(json),
            "{\"version\":\"%s\",\"build\":%d,\"slots\":[",
            ANYPAD_VERSION, ANYPAD_BUILD);

        for (int i = 0; i < MAX_SLOTS; i++) {
            vpad_slot_info_t info;
            vpad_get_slot_info(i, &info);
            jlen += snprintf(json + jlen, sizeof(json) - (size_t)jlen,
                "%s{\"status\":%d,\"name\":\"%s\",\"conn\":%d,\"handle\":%d,\"alt_handle\":%d,\"user_id\":%u,\"battery\":%d,\"charging\":%d,\"injected\":%u}",
                i > 0 ? "," : "",
                info.status, info.name, info.conn_type, info.handle, info.alt_handle,
                (unsigned)info.user_id, info.battery_level, info.battery_charging,
                (unsigned)info.packets_injected);
        }
        snprintf(json + jlen, sizeof(json) - (size_t)jlen, "]}");

        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n%s",
            strlen(json), json);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/pair", 14) == 0) {
        const char *ok = "{\"status\":\"usb_plug_and_play_active\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(ok), ok);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/press_ps", 18) == 0) {
        char *p = strstr(req, "slot=");
        int slot = p ? atoi(p + 5) : 0;
        if (slot < 0) {
            for (int i = 0; i < MAX_SLOTS; i++) {
                if (vpad_is_live(i)) {
                    vpad_slot_info_t info;
                    vpad_get_slot_info(i, &info);
                    shellui_press_ps_button(info.handle);
                    vpad_press_ps_button(i);
                }
            }
        } else if (slot >= 0 && slot < MAX_SLOTS) {
            vpad_slot_info_t info;
            vpad_get_slot_info(slot, &info);
            shellui_press_ps_button(info.handle);
            vpad_press_ps_button(slot);
        }
        const char *ok = "{\"status\":\"ps_pressed\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(ok), ok);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/disconnect", 20) == 0) {
        char *p = strstr(req, "slot=");
        int slot = p ? atoi(p + 5) : 0;
        if (slot < 0) {
            for (int i = 0; i < MAX_SLOTS; i++) vpad_remove(i);
        } else {
            vpad_remove(slot);
        }
        const char *ok = "{\"status\":\"disconnected\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(ok), ok);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/rebind", 16) == 0) {
        char *p = strstr(req, "slot=");
        int slot = p ? atoi(p + 5) : 0;
        char *pu = strstr(req, "user=");
        int32_t user = pu ? (int32_t)strtol(pu + 5, NULL, 0) : -1;
        vpad_rebind_user(slot, user);
        const char *ok = "{\"status\":\"rebound\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(ok), ok);
        write(cfd, res, (size_t)res_len);
    }
    else if (strncmp(req, "POST /api/exit", 14) == 0) {
        const char *ok = "{\"status\":\"shutting_down\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(ok), ok);
        write(cfd, res, (size_t)res_len);
        close(cfd);
        int sfd = open("/data/anypad/stop", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (sfd >= 0) close(sfd);
        return;
    }
    else if (strncmp(req, "GET /api/log", 12) == 0) {
        int lfd = open("/data/anypad/anypad.log", O_RDONLY);
        if (lfd < 0) lfd = open("anypad.log", O_RDONLY);
        if (lfd >= 0) {
            off_t sz = lseek(lfd, 0, SEEK_END);
            off_t start = 0;
            if (sz > 32768) start = sz - 32768;
            /* ?since=<offset> returns only what was appended after that byte, so the
             * dashboard can tail the log; X-Log-Offset is where the next call starts. */
            char *ps = strstr(req, "since=");
            char *eol = strstr(req, "\r\n");
            if (ps && (!eol || ps < eol)) {
                off_t since = (off_t)strtoll(ps + 6, NULL, 10);
                if (since >= 0 && since <= sz) start = since;
            }
            lseek(lfd, start, SEEK_SET);
            static char logbuf[32768];  /* only the server thread uses it */
            ssize_t rb = read(lfd, logbuf, sizeof(logbuf));
            close(lfd);
            if (rb < 0) rb = 0;
            res_len = snprintf(res, sizeof(res),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain; charset=utf-8\r\n"
                "Cache-Control: no-store\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Expose-Headers: X-Log-Offset\r\n"
                "X-Log-Offset: %lld\r\n"
                "Content-Length: %zd\r\n"
                "Connection: close\r\n\r\n", (long long)(start + rb), rb);
            write(cfd, res, (size_t)res_len);
            if (rb > 0) write_all(cfd, logbuf, (size_t)rb);
        } else {
            const char *err = "Log file not found";
            res_len = snprintf(res, sizeof(res),
                "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                strlen(err), err);
            write(cfd, res, (size_t)res_len);
        }
    }
    else if (strncmp(req, "POST /api/send_btn", 18) == 0) {
        char *p_slot = strstr(req, "slot=");
        int slot = p_slot ? atoi(p_slot + 5) : 0;
        char *p_code = strstr(req, "code=");
        char *p_btn = strstr(req, "btn=");
        uint32_t btn_mask = PAD_BTN_CROSS;
        if (p_code) {
            int c = atoi(p_code + 5);
            switch (c) {
            case 1: btn_mask = PAD_BTN_CROSS; break;
            case 2: btn_mask = PAD_BTN_CIRCLE; break;
            case 3: btn_mask = PAD_DPAD_UP; break;
            case 4: btn_mask = PAD_DPAD_DOWN; break;
            case 5: btn_mask = PAD_DPAD_LEFT; break;
            case 6: btn_mask = PAD_DPAD_RIGHT; break;
            case 7: btn_mask = PAD_BTN_PS; break;
            case 8: btn_mask = PAD_BTN_SQUARE; break;
            case 9: btn_mask = PAD_BTN_TRIANGLE; break;
            case 10: btn_mask = PAD_BTN_OPTIONS; break;
            case 11: btn_mask = PAD_BTN_SHARE; break;
            case 12: btn_mask = PAD_BTN_L1; break;
            case 13: btn_mask = PAD_BTN_R1; break;
            default: btn_mask = PAD_BTN_CROSS; break;
            }
        } else if (p_btn) {
            if (strncmp(p_btn + 4, "dpad_up", 7) == 0) btn_mask = PAD_DPAD_UP;
            else if (strncmp(p_btn + 4, "dpad_down", 9) == 0) btn_mask = PAD_DPAD_DOWN;
            else if (strncmp(p_btn + 4, "dpad_left", 9) == 0) btn_mask = PAD_DPAD_LEFT;
            else if (strncmp(p_btn + 4, "dpad_right", 10) == 0) btn_mask = PAD_DPAD_RIGHT;
            else if (strncmp(p_btn + 4, "circle", 6) == 0) btn_mask = PAD_BTN_CIRCLE;
            else if (strncmp(p_btn + 4, "cross", 5) == 0) btn_mask = PAD_BTN_CROSS;
            else if (strncmp(p_btn + 4, "square", 6) == 0) btn_mask = PAD_BTN_SQUARE;
            else if (strncmp(p_btn + 4, "triangle", 8) == 0) btn_mask = PAD_BTN_TRIANGLE;
            else if (strncmp(p_btn + 4, "options", 7) == 0) btn_mask = PAD_BTN_OPTIONS;
            else if (strncmp(p_btn + 4, "share", 5) == 0) btn_mask = PAD_BTN_SHARE;
            else if (strncmp(p_btn + 4, "ps", 2) == 0) btn_mask = PAD_BTN_PS;
        }

        /* If PS button is requested, also synthesize inside SceShellUI */
        if (btn_mask & PAD_BTN_PS) {
            if (slot < 0) {
                for (int i = 0; i < MAX_SLOTS; i++) {
                    if (vpad_is_live(i)) {
                        vpad_slot_info_t info;
                        vpad_get_slot_info(i, &info);
                        shellui_press_ps_button(info.handle);
                    }
                }
            } else if (slot >= 0 && slot < MAX_SLOTS && vpad_is_live(slot)) {
                vpad_slot_info_t info;
                vpad_get_slot_info(slot, &info);
                shellui_press_ps_button(info.handle);
            }
        }

        if (slot < 0) {
            for (int i = 0; i < MAX_SLOTS; i++) {
                if (vpad_is_live(i)) {
                    pad_state_t st;
                    pad_state_neutral(&st);
                    st.buttons = btn_mask;
                    vpad_update(i, &st);
                }
            }
            usleep(150000);
            for (int i = 0; i < MAX_SLOTS; i++) {
                if (vpad_is_live(i)) {
                    pad_state_t st;
                    pad_state_neutral(&st);
                    vpad_update(i, &st);
                }
            }
        } else if (slot >= 0 && slot < MAX_SLOTS && vpad_is_live(slot)) {
            pad_state_t st;
            pad_state_neutral(&st);
            st.buttons = btn_mask;
            vpad_update(slot, &st);
            usleep(150000);
            pad_state_neutral(&st);
            vpad_update(slot, &st);
        }
        const char *ok = "{\"status\":\"button_sent\"}";
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(ok), ok);
        write(cfd, res, (size_t)res_len);
    }
    else {
        /* Serve Web Dashboard HTML */
        size_t hlen = HTML_INDEX_LEN;
        res_len = snprintf(res, sizeof(res),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=UTF-8\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Expires: 0\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n", hlen);
        write(cfd, res, (size_t)res_len);
        write_all(cfd, HTML_INDEX, hlen);
    }
    close(cfd);
}

/* Requests are served on their own thread so a slow client (or the 100-150ms waits in the
 * PS/button endpoints) never delays the 250Hz injection loop in main(). */
static void *web_server_worker(void *arg)
{
    (void)arg;
    while (g_server_running) {
        struct pollfd pfd;
        pfd.fd = g_server_fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 200) <= 0) continue;

        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int cfd = accept(g_server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (cfd >= 0) handle_client(cfd);
    }
    return NULL;
}

void web_cleanup(void)
{
    if (g_server_running) {
        g_server_running = 0;
        pthread_join(g_server_thread, NULL);
    }
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }
}
