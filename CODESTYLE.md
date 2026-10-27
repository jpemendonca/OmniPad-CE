# CODESTYLE.md

Only rules you can check by reading a diff go here.

## Language

- Code, identifiers, comments, log lines and test names are in English.
- Text the user reads goes in Portuguese and English. The dashboard uses its `I18N` table (`pt` and `en`). Notifications go through `ui_text(pt, en)`. Docs: `README.md` and `docs/*.md` are in English, and `docs/COMECE_AQUI.md` is in Portuguese.
- Commit messages: a Conventional Commits prefix in English (`feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`), then the description in Portuguese. This is what the history already uses.

## Comments

- Only when the *why* is not obvious. Never restate the code, never leave commented-out code, never add banners or file headers.
- Short sentences, active voice, one word for one meaning. Describe the code, not the diff that produced it.
- When hardware forced a decision, say what was seen and where: `/* ... (PS5, 25 Oct) */`.

```c
/* right */
/* The DS4 drops a link after ~170 ms if authentication has not started (PS5, 25 Oct) */
#define T_SELF_AUTH 0

/* wrong */
/* Set the auth delay to 0 */   // restates the code
#define T_SELF_AUTH 0           // was 800
```

## C

- C11, built with `-Wall -Wextra`, and the payload build stays free of warnings.
- `snake_case` for functions and variables, `UPPER_CASE` for macros and constants. Public functions carry their module prefix (`bt_adapter_*`, `usb_hotplug_*`, `vpad_*`, `launcher_*`). Everything used by one file only is `static`.
- One `.h` per module, holding only what other modules call. Each declaration gets a one-line comment saying what it returns or when to call it.
- Code that needs the PS5 goes inside `#ifdef __PROSPERO__`. It gets a host stub in `#else`, so the file still builds for host tests.
- Logging: `log_line("<module>: ...")`, lower-case module prefix (`bt:`, `usb_hotplug:`, `vpad:`, `launcher:`). Log what happened and the values that explain it. Never log link keys.
- No new heap allocation in the 250 Hz loop or in reader threads. Use static or caller-owned buffers.
- Mutex scope: never hold `g_bt.lock` or `g_vpad_mutex` while doing USB or HCI I/O.
- Errors: functions return `0`/`1` (or `-1`/`0`) and log the cause where it happened. Don't push a failure on silently. A state that should never happen is logged too, never left to assert on the console.

```c
/* right */
if (ioctl(d->fd, USB_FS_INIT, &init) != 0) {
    log_line("bt: USB_FS_INIT failed on %s (%s)", d->path, strerror(errno));
    return 0;
}

/* wrong */
ioctl(d->fd, USB_FS_INIT, &init);   // result ignored
```

## Controllers

- A new USB pad, or a fix for one, is an entry in `k_drivers` or `k_xinput_class_drivers` in `pad_drivers.c`. Behaviour lives in its flags and parser, not in `if (vid == ...)` checks in shared code.
- A Bluetooth adapter quirk is set once in `bring_up` (`bt_adapter.c`) as a field of the device, and checked where it matters.
- `src/bt/` is vendored code. Keep changes small and mark them with `/* OmniPad: ... */`, and list each change in `src/bt/README.md`.

## Dashboard (`web/index.html`)

- One self-contained file, no external requests, no build step. The build embeds it into the payload.
- Every visible string goes in both `I18N.pt` and `I18N.en`.
- New elements must work in the light theme, the dark theme, at phone width, and in TV mode (`html.tv`).

## Tests

These rules apply to tests that the test policy in `SPECS.md` calls for.

- Name: `test_<function>_<scenario>_<behaviour>`.
- Body: `/* arrange */`, `/* act */` and `/* assert */` comments, in that order. This is the one exception to the comment rule.
- Every `CHECK` carries a readable message saying what it guarantees. That message is what the report prints when the check fails.

```c
static void test_rtl_fw_build_romWithoutPatch_returnsMinus2(void)
{
    /* arrange */
    uint8_t fw[256], out[256];
    size_t len = make_fw(fw);
    rtl_fw_info_t info;

    /* act */
    int n = rtl_fw_build(fw, len, NULL, 0, 5, out, sizeof(out), &info);

    /* assert */
    CHECK(n == -2, "a ROM with no patch in the file is refused, got %d", n);
}
```

- A test file is plain C with a `main` that prints `<name>: N checks, M failures` and returns non-zero on failure. Add it to the `host-test` target in `Makefile`.

## Git

- Conventional Commits, one logical change per commit. Never commit with the gate red.
- Branches are `feat/...` or `fix/...` off `main`. Merge them by fast-forward once the change is validated on the PS5, then delete the branch.
- Never add a `Co-Authored-By` or AI attribution line.
