/* The notifications in every language (src/ui_text.c). A format that differs from the English one
 * would read the wrong argument on the console, so each one must ask for the same values. */
#include "ui_text.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* The conversions of a format, in order: "%s%d" for "OmniPad %s (player %d)" */
static void conversions(const char *text, char *out, size_t max)
{
    size_t n = 0;
    for (const char *p = text; *p && n + 3 < max; p++) {
        if (*p != '%') continue;
        out[n++] = '%';
        out[n++] = p[1];
        if (p[1]) p++;
    }
    out[n] = 0;
}

static void test_ui_text_in_everyLanguage_hasEveryMessage(void)
{
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int m = 0; m < UI_COUNT; m++) {
            /* act */
            const char *text = ui_text_in((ui_lang_t)l, (ui_msg_t)m);

            /* assert */
            CHECK(text && text[0], "language %d has message %d", l, m);
        }
    }
}

static void test_ui_text_in_everyLanguage_asksTheSameArguments(void)
{
    for (int m = 0; m < UI_COUNT; m++) {
        char want[16];
        conversions(ui_text_in(UI_EN, (ui_msg_t)m), want, sizeof want);
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            char got[16];

            /* act */
            conversions(ui_text_in((ui_lang_t)l, (ui_msg_t)m), got, sizeof got);

            /* assert */
            CHECK(strcmp(want, got) == 0, "language %d message %d asks '%s', English asks '%s'", l, m, got, want);
        }
    }
}

static void test_ui_text_in_everyLanguage_fitsANotification(void)
{
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int m = 0; m < UI_COUNT; m++) {
            /* act */
            size_t bytes = strlen(ui_text_in((ui_lang_t)l, (ui_msg_t)m));

            /* assert */
            CHECK(bytes < 512, "language %d message %d has %zu bytes", l, m, bytes);
        }
    }
}

static void test_ui_lang_from_system_knownValues_pickTheLanguage(void)
{
    /* arrange */
    static const struct { int system; ui_lang_t want; } table[] = {
        { 0, UI_JA }, { 1, UI_EN }, { 18, UI_EN }, { 2, UI_FR }, { 22, UI_FR }, { 3, UI_ES }, { 20, UI_ES },
        { 4, UI_DE }, { 7, UI_PT }, { 17, UI_PT }, { 8, UI_RU },
    };

    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        /* act */
        ui_lang_t got = ui_lang_from_system(table[i].system);

        /* assert */
        CHECK(got == table[i].want, "system language %d gives %d, wanted %d", table[i].system, got, table[i].want);
    }
}

static void test_ui_lang_from_system_noTranslation_isEnglish(void)
{
    /* arrange: Italian, Dutch, Korean, Chinese, Polish, an unknown value */
    static const int others[] = { 5, 6, 9, 10, 11, 16, 99, -1 };

    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) {
        /* act */
        ui_lang_t got = ui_lang_from_system(others[i]);

        /* assert */
        CHECK(got == UI_EN, "system language %d falls back to English, got %d", others[i], got);
    }
}

int main(void)
{
    test_ui_text_in_everyLanguage_hasEveryMessage();
    test_ui_text_in_everyLanguage_asksTheSameArguments();
    test_ui_text_in_everyLanguage_fitsANotification();
    test_ui_lang_from_system_knownValues_pickTheLanguage();
    test_ui_lang_from_system_noTranslation_isEnglish();
    printf("test_ui_text: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
