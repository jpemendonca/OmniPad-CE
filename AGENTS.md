# AGENTS.md

OmniPad CE (Community Edition) is a payload for jailbroken PS5 consoles (FW 7.00 – 13.60), written in C. It makes third-party controllers show up on the console as DualSense pads for players 2 to 4. They can come in over USB, over a 2.4 GHz dongle, or over Bluetooth through a separate USB adapter. The official DualSense stays player 1 on the console's own radio, and OmniPad never touches that radio.

This project is developed by AI agents with one human, the maintainer, who tests on a real PS5. This file is the source of truth for agents. `CLAUDE.md` and `GEMINI.md` only point here; Cursor and Codex read this file directly.

## The main path

Every change is judged against this path, as the user walks it:

1. Download `OmniPad-CE.elf` from the GitHub release.
2. Send it to the PS5 with a payload loader and **start it** (Launch in PLK). The notification "OmniPad … ativo/running" appears.
3. Connect a controller. For a USB cable or a 2.4 GHz dongle, plug it in. For Bluetooth, plug in a USB adapter and put the controller in pairing mode. The notification "… conectado/connected (jogador N)" appears.
4. Press PS on that controller, pick a profile, and play.
5. If something fails, open the dashboard: the OmniPad icon in the Media tab, or `http://<PS5_IP>:8095`. Then copy the diagnostics into a GitHub issue.

## Read before working

- `CODESTYLE.md`: always.
- `SPECS.md`: architecture, contracts, decisions. Read the section for the area you touch.
- `src/bt/README.md`: before touching `src/bt/`, the vendored Bluetooth host.
- `docs/COMECE_AQUI.md` and `README.md`: what users are told. Keep them true when behaviour changes.

## How work is done

There is no ROADMAP. Changes are small and come from the maintainer: a bug seen on the PS5, a report from the group, a new controller or adapter.

1. Understand the request. If it touches hardware behaviour, read the PS5 log first (see "Reading the PS5" below).
2. Work on a branch named `feat/...` or `fix/...` off `main`. `main` only holds what was validated on the PS5.
3. Change the code following `CODESTYLE.md`.
4. Run the quality gate.
5. Build a test ELF and hand it to the maintainer (see "Test builds" below). Say plainly what was checked on the PC and what still needs the PS5.
6. Commit in Conventional Commits, one logical change per commit.
7. When the maintainer confirms it works on the PS5, merge into `main` (fast-forward), push, and delete the branch.

Each commit says what it validated: tests, the dashboard in a browser, or the PS5. **Code that was written is not a path that was walked.** Only the maintainer's test on the PS5 counts as walked.

## Quality gate

Run it in WSL Ubuntu, with the toolchain from `tools/setup-wsl-toolchain.sh`:

```bash
make test
```

```bash
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk && make
```

Both must pass, and the payload build must have **no warnings**. If you changed `web/index.html`, also check that the extracted `<script>` blocks parse with `node --check`, and look at the page in the browser pane with `tools/mock_dashboard.py`.

What the gate does not cover: it never runs on a PS5. It does not prove that a controller connects, that the virtual pad shows up, or that an adapter works. A green gate is required, but it is not proof that the product works.

## Test builds and releases

- Test build: `make -f ps5.mk VERSION=<x.y.z-label> BUILD_NUM=<n>`. The label and number are compiled in and appear in the notification and on the dashboard. Rename the output to `dist/OmniPad-CE-b<n>-teste-<tema>.elf`, and remove copies it supersedes. `dist/` keeps the current release, the previous one, the current test build, and the two files the maintainer asked to keep: `OmniPad-PS5-1.0.4-original-do-autor.elf` (upstream) and `OmniPad-PS5-b32-estavel.elf`.
- Release: only when the maintainer asks for one explicitly in the chat. Publishing is public and needs that OK every time.
  1. Bump `src/version.h`.
  2. Run `make -f ps5.mk clean`, then the full gate.
  3. Tag `vX.Y.Z` on `main`.
  4. Run `gh release create` with `dist/OmniPad-CE.elf` and `firmware/rtl_bt/LICENCE.rtlwifi_firmware.txt`.
  5. Write the release notes in Portuguese: what's new, what was tested on the PS5, what wasn't, the sha256, and the licences.
  6. Title the release `OmniPad CE X.Y.Z: <one-line summary in Portuguese>`. OmniPad CE is the project, not an unofficial build: never call a release "não oficial".

## Reading the PS5

- The agent cannot send payloads. The maintainer sends and starts them.
- The dashboard and API are at `http://<PS5_IP>:8095`. The IP has changed before (from `.23` to `.14`). If it doesn't answer, ask the maintainer for the current one.
- `GET /api/log` returns the last 32 KB of the log. The log file is kept across restarts. To read all of it, page with `GET /api/log?since=<offset>`, following the `X-Log-Offset` header.
- `GET /api/state` returns the slots, the controller input and the Bluetooth state.
- A PS5 that answers ping while port 8095 times out usually means the IP changed, not that OmniPad crashed. Check the IP before diagnosing.

## Hard rules

- **Never send any request to the console's internal Bluetooth radio** (MediaTek, vendor `0e8d`) beyond reading its device descriptor. Sharing it locks up the DualSense until the console restarts.
- Never inject into or patch system processes other than the existing `SceShellUI`/`SceShellCore` calls in `shellui_inject.c`. Do not revive the dropped `SceRemotePlay` route.
- No warnings in the payload build. Fix the code, don't suppress the warning.
- Never bypass the gate (`--no-verify` or similar), and never commit with it red.
- No new dependency or vendored code without the maintainer asking for it. Vendored code keeps its licence and is listed in `docs/THIRD_PARTY.md`.
- **Nothing goes in without a caller.** A function, driver or route that production code never calls is not done. A test does not count as a caller.
- Never add `Co-Authored-By` or any other AI authorship line to commits, PRs or releases.
- Firmware blobs (`firmware/`) and the maintainer's notes (`anotacoes/`) are never committed.
- Do not change files you were not asked to change. If a change outside the scope looks necessary, say so first.
- Keep `README.md`, `docs/COMECE_AQUI.md` and `docs/COMPATIBILITY.md` true. A user-visible change updates them in the same branch.

## Repository map

| Path | What it is |
|---|---|
| `src/main.c` | Start-up, the 250 Hz main loop, shutdown, the start notification |
| `src/ps5_vpad.c` | Virtual DualSense slots (`vpad_*`), profile binding, connect/disconnect notifications |
| `src/usb_hotplug.c` | USB scan, one reader thread per pad, idle rules |
| `src/pad_drivers.c` | **One driver per controller model** (VID:PID, connection type, idle rule, flags, parser) |
| `src/usb_controllers.c`, `src/profiles.c` | USB report parsers used by the drivers |
| `src/bt_adapter.c` | OmniPad's own USB Bluetooth adapter: detection, firmware, HCI transport, glue to slots, background search, chip quirks |
| `src/bt_rtl.c` | Realtek firmware patch selection |
| `src/bt/` | Vendored Bluetooth host from AnyPad (see its README) |
| `src/launcher.c` | The Media tab tile (`/user/app/OMNP00001`) |
| `src/web.c`, `web/index.html` | Dashboard server, HTTP API, and the single-file page (embedded at build time) |
| `src/shellui_inject.c` | ptrace calls into SceShellUI/SceShellCore (PS button, profile bind) |
| `src/log.c` | Log file, notifications, `ui_text` (PT or EN by system language) |
| `tests/` | Host tests (`make test`) |
| `tools/` | Toolchain setup, firmware download, embedders, mock dashboard, screenshots, PC probes |
| `assets/launcher/` | Tile icon (`icon.html` renders `icon0.png`) |
| `docs/` | User and technical docs; `COMECE_AQUI.md` is the Portuguese user guide |
