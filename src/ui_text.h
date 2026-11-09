#ifndef UI_TEXT_H
#define UI_TEXT_H

/* The PS5 notifications, in the console's system language. English when it has no translation. */

typedef enum {
    UI_RUNNING,             /* %s version */
    UI_RUNNING_IP,          /* %s version, %s IP, %d port */
    UI_PAD_CONNECTED,       /* %s name, %d player */
    UI_PAD_DISCONNECTED,    /* %d player */
    UI_CONTROLLER,          /* the name of a pad that has none */
    UI_BT_READY,
    UI_BT_PAIRED,
    UI_BT_LOCKED,
    UI_BT_NO_START,
    UI_JC_NEED_RIGHT,
    UI_JC_NEED_LEFT,
    UI_COUNT
} ui_msg_t;

typedef enum { UI_EN, UI_PT, UI_ES, UI_FR, UI_DE, UI_RU, UI_JA, UI_LANG_COUNT } ui_lang_t;

/* The text of msg in the console's language. */
const char *ui_text(ui_msg_t msg);

/* The console's language as a code the dashboard knows ("en", "pt", ...). */
const char *ui_lang_code(void);

/* The language for a value of the system's language setting. */
ui_lang_t ui_lang_from_system(int system_lang);

/* The text of msg in a given language. */
const char *ui_text_in(ui_lang_t lang, ui_msg_t msg);

#endif /* UI_TEXT_H */
