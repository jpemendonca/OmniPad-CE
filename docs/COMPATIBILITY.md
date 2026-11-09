# Controller Compatibility Guide 🎮

> **OmniPad CE v1.1.0** is an open-source controller subsystem for jailbroken PlayStation 5 consoles (FW 7.00 – 13.60).
>
> It supports USB cables and 2.4GHz USB dongles at 250Hz. It also supports Bluetooth controllers through a **separate USB Bluetooth adapter** plugged into the PS5 (see section 3). The console's internal radio stays with the DualSense and is never used.

---

## 🔬 Developer Hardware Testing Disclosure

OmniPad CE is developed and maintained by a solo developer.
- **Currently on the developer's physical test bench:** **Machenike G5 Pro Max SE** (tested with both its 2.4GHz USB Dongle and direct USB-C cable).
- **Verified by community testers on FW 13.60:** DualShock 4 (USB cable, `054c:05c4`), 8BitDo Ultimate 2C Wireless (2.4GHz dongle and USB-C cable, `2dc8:3106`).
- **Not yet physically tested on bench:** Xbox One, Xbox 360, DualShock 3, Switch Pro.
- **How support was built:** Decoders and handshakes were engineered from official USB HID/XInput/GIP documentation, open-source references, and verified via automated test suites (`tests/test_parsers.c`).
- **Community Call for Help:** Because the developer is actively working to acquire real Xbox and PS4 controllers for physical verification, these profiles are currently classified as **EXPERIMENTAL**. If you test with physical hardware, your feedback, bug reports, and logs are deeply valued!

---

## 🔌 1. USB Compatibility Matrix

All peripherals in this table connect directly to any USB-A or USB-C port on the front or rear of the PS5:

| Controller / Device | Connection Type | Hardware Testing Status | Real-World Status & Technical Notes |
|---|---|:---:|---|
| **Machenike G5 Pro / G5 Pro Max SE** | 2.4GHz USB Dongle or USB-C Cable | 🟢 **Verified on PS5 Hardware** | Fully tested & verified in real 3-player gameplay on FW 13.60. Instant plug & play. |
| **ShanWan 2.4G Gamepad** | 2.4GHz USB Dongle (`1a34:f517`) | 🟢 **Verified on PS5 Hardware** | Dongle shared with Machenike; dispatches via native XInput report parser. |
| **Microsoft Xbox Series X\|S** | USB-C Cable (`045e:0b12`) | 🟢 **Verified on PS5 Hardware (community)** | Decodes GIP input reports (`0x20`). On this cable, Share acts as the touchpad and can be changed with Map buttons. Also works over Bluetooth, see section 3. |
| **Microsoft Xbox One** | Micro-USB Cable | 🟡 **Experimental (Spec-Implemented)** | Same GIP decoder as the Series pad. |
| **Microsoft Xbox 360 Controller** | Wired USB Cable (`045e:028e`) | 🟡 **Experimental (Spec-Implemented)** | Standard XInput report parser and periodic LED keepalive implemented. |
| **Sony DualShock 4** | Micro-USB Cable (`054c:05c4`/`09cc`) | 🟢 **Verified on PS5 Hardware (community)** | Verified with a CUH-ZCT1 (`054c:05c4`): 64-byte HID reports at 250Hz, buttons and sticks mapped correctly. `09cc` (CUH-ZCT2) not yet tested. Also works over Bluetooth, see section 3. |
| **Sony DualShock 3 (PS3)** | Mini-USB Cable (`054c:0268`) | 🟡 **Experimental (Spec-Implemented)** | Magic wake packet (`0x03f4`) implemented. *(No Bluetooth)*. |
| **Nintendo Switch Pro Controller** | USB-C Cable (`057e:2009`) | 🟡 **Experimental (Spec-Implemented)** | 3-step USB activation handshake implemented. |
| **8BitDo USB Wireless Adapter 2 with a DualShock 4** | USB adapter (`2dc8:3107`, then `054c:05c4`) | 🟢 **Verified on PS5 Hardware** | Idle it shows as `2dc8:3107`, which OmniPad leaves alone. When the DS4 connects it turns into a USB DS4. |
| **8BitDo Wireless USB Adapter (v1, or v2 with other controllers)** | USB 2.4G Dongle (`2dc8:*`) | 🟡 **Experimental (Spec-Implemented)** | Bridges controllers into standard USB HID/XInput. |
| **8BitDo Ultimate 2** | 2.4GHz USB Dongle (`2dc8:310b`) | 🟢 **Verified on PS5 Hardware (community)** | Gets the same player LED command as the Ultimate 2C. It has no idle rule, because it only reports on change. |
| **Mad Catz FightStick TE (PS3)** | Wired USB (`0738:8838`) | 🟡 **Experimental (Spec-Implemented)** | Reads the usual PS3 arcade stick layout. |
| **8BitDo Ultimate 2C Wireless** | 2.4GHz USB Dongle (`2dc8:310a`) | 🟡 **Experimental (Spec-Implemented)** | Same handling as the `2dc8:3106` below. Its receiver shows as `2dc8:301c` until the pad is on. |
| **8BitDo Ultimate 2C Wireless** | 2.4GHz USB Dongle or USB-C Cable (`2dc8:3106`) | 🟢 **Verified on PS5 Hardware (community)** | Briefly enumerates as a vendor HID device (`2dc8:3016`) and then as XInput (`2dc8:3106`). Sends no input until it gets the XInput player-LED command (`01 03 02`) on interrupt OUT endpoint `0x05`, and re-enumerates after ~10s without it. |
| **GameSir G7 Pro 8K** | 2.4GHz USB Dongle | 🟢 **Verified on PS5 Hardware (community)** | A user on r/ps5homebrew reported it working well. Its USB ID and firmware were not reported. A G7 Pro was also reported working over Bluetooth in issue #1. |
| **Logitech Gamepads (F310, F710)** | USB Cable / 2.4G Receiver | 🟡 **Experimental (Spec-Implemented)** | Operates in either XInput or standard HID mode. |
| **Generic PC USB Gamepads** | Standard USB Cable | 🟡 **Experimental (Spec-Implemented)** | 10-byte standard HID descriptor normalization. |

---

## 🔍 2. Deep Dive: Microsoft Xbox Controllers (Honest Reality Check)

Because Xbox controllers are among the most popular third-party controllers in the community, here is an unambiguous, honest explanation of what works, what does not work, and why:

### ❌ What Does NOT Work:
1. **Direct Wireless Bluetooth to the PS5:**
   - Modern Xbox One (Model 1708) and Xbox Series X\|S (Model 1914) controllers transmit over **Bluetooth Low Energy (BLE)** using Microsoft's implementation of the **Security Manager Protocol (SMP)** with AES-128 cryptographic pairing.
   - OmniPad never uses the console's internal radio. Over Bluetooth, an Xbox pad reaches OmniPad through a separate USB Bluetooth adapter (section 3). This is verified with a Series pad.
2. **Official "Xbox Wireless Adapter for Windows" (Microsoft USB Dongle):**
   - The official Microsoft wireless dongle uses a proprietary 2.4GHz Wi-Fi Direct radio protocol requiring complex kernel drivers (such as `xone` / `mt7612u` on Linux).
   - This dongle is **NOT supported** by OmniPad or FreeBSD/ProsperoOS.

### ✅ What DOES Work:
1. **Direct Wired USB-C / Micro-USB Cable:**
   - Plug the controller directly into any PS5 USB port using a **data-capable** USB cable.
   - The engine claims the GIP endpoints (`0x82` IN, `0x02` OUT), issues the initialization sequence (`0x05 0x20 0x00 0x01 0x00`), and begins decoding standard `0x20` input packets.
   - *Requirement:* The cable **must** have physical data lines. Charging-only cables (frequently shipped with cheap power banks or battery packs) will only power the LED and will not transmit input packets.
2. **Wireless via 8BitDo Wireless USB Adapter 2:**
   - Plug the 8BitDo Wireless USB Adapter 2 into the PS5.
   - Pair your Xbox controller directly to the 8BitDo adapter by holding the pairing button on both devices.
   - The 8BitDo adapter acts as an active hardware bridge, translating Xbox inputs into USB signals that OmniPad recognizes instantly.

---

## 📡 3. Bluetooth Through a USB Adapter (v1.1.0)

The PS5 has one Bluetooth radio, and the DualSense needs it. Sharing it with the system (as AnyPad and upstream pull request 18 do) leaves the DualSense stuck until a restart. So OmniPad drives a **second, ordinary USB Bluetooth adapter** plugged into the console. It runs the AnyPad Bluetooth host (`src/bt/`) on that adapter (`src/bt_adapter.c`).

### Adapters

| Adapter | Chip | Status |
|---|---|---|
| TP-Link UB500 (`2357:0604`) | Realtek RTL8761BU | 🟢 **Verified on PS5 Hardware**: OmniPad uploads the linux-firmware patch on its own (~0.3 s) |
| Other RTL8761B / RTL8761BU adapters | Realtek RTL8761B | 🟡 Same chip and firmware: expected to work |
| Generic "CSR 4.0" dongles, Baseus included (`0a12:0001`, manufacturer not CSR) | CSR8510 clone | 🟠 **Verified, with limits**: one pad works, and a DS4 and a USB 8BitDo dongle work together. They get no background search or LE scan with a pad connected. An HCI command sent while a pad is reporting can hang the chip for 5 s and lock it up: the Pair button with a pad playing can do that. A locked-up adapter gets a USB reset (`USB_DEVICEENUMERATE`), and if it does not come back the user is asked to replug it. |
| Other adapters that need no firmware (genuine CSR8510, some Broadcom) | various | 🟡 Expected to work, not tested |
| Other Realtek chips (RTL8821C, RTL8822B, ...) | Realtek | 🔴 Not yet: each needs its own firmware |

Any USB device with a Bluetooth HCI interface (class `E0/01/01`) is used, except MediaTek (`0e8d`) and Marvell (`1286`), the vendors of the console's own radio.

### Controllers

| Controller | Status |
|---|---|
| **Sony DualShock 4** (CUH-ZCT1, Bluetooth id `054c:09cc`) | 🟢 **Verified on PS5 Hardware**: pairing, play, and reconnecting by pressing PS |
| **Xbox Series X\|S** (Bluetooth LE, `045e:0b13`) | 🟢 **Verified on PS5 Hardware**: pairing, play, and reconnecting when switched on. Its battery is not read yet |
| Nintendo Joy-Con (`057e:2006`, `057e:2007`) | 🟡 One alone, held sideways, was recorded on a PS5. Joining two into one controller is not yet tested on a console |
| Switch Pro, 8BitDo, generic Bluetooth HID | 🟡 Profiles come from AnyPad, not tested through the adapter |

### Using it

- **Two Joy-Cons as one controller:** the second Joy-Con to connect waits, with no controller of its own on the console. Within 60 s, hold L on the left one and R on the right one for 1 s. They become one controller on the slot of the one that connected first. For two players, press any other button on the second one, or wait the 60 s: it becomes a separate controller, which still can join until the 60 s end. Turn one off to separate joined Joy-Cons: the other goes back to working alone, sideways. Map buttons treats the joined pair as its own model, `057e:2008`. Motion is not sent yet.
- **New controller:** put it in pairing mode (DS4: hold Share + PS). While no Bluetooth pad is connected, OmniPad searches all the time. With one connected, it looks for 3.8 s every 20 s. The dashboard's **Pair controller** button opens a 2-minute search as well.
- **Paired controller:** press PS. It calls the adapter and reconnects. The keys are stored in `/data/anypad/omnipad_bt.db`.
- **Forget paired pads:** dashboard, **Forget paired**.

---

## 👥 4. Multi-Player Architecture (Up to 4 Players)

OmniPad creates and manages **3 independent virtual DualSense slots** in the ProsperoOS kernel (`libScePad`):

| Player | Controller Hardware | Managed By |
|---|---|---|
| **Player 1** | Official Physical DualSense | Native PS5 Operating System |
| **Player 2** | Supported USB Controller / Dongle (Slot 1) | OmniPad CE Virtual Device Pool |
| **Player 3** | Supported USB Controller / Dongle (Slot 2) | OmniPad CE Virtual Device Pool |
| **Player 4** | Supported USB Controller / Dongle (Slot 3) | OmniPad CE Virtual Device Pool |

**Example 4-Player Local Setup:**
- **Player 1:** Official DualSense (Primary User)
- **Player 2:** Machenike G5 Pro (via bundled 2.4G USB Dongle)
- **Player 3:** Xbox Series X controller (connected via wired USB-C cable or 8BitDo Adapter)
- **Player 4:** Nintendo Switch Pro controller (connected via wired USB-C cable)

---

## ⚙️ 5. DualSense Feature Parity & Limitations

When non-DualSense controllers are emulated as virtual DualSense devices, the following capabilities apply:

| Feature | Support | Notes |
|---|---|---|
| **Digital Buttons & D-Pad** | ✅ Fully Supported | 100% mapped (Cross, Circle, Square, Triangle, L1, R1, L3, R3, Options, Share). |
| **Analog Joysticks** | ✅ Fully Supported | Normalized to 0–255 with absolute center at 128 (zero drift). |
| **Analog Triggers (L2 / R2)** | ✅ Fully Supported | Full 0–255 analog depth with digital threshold click. |
| **PS / Home Button** | ✅ Fully Supported | Opens the native profile selector and control center. |
| **Touchpad Click** | ✅ Fully Supported | Mapped to open maps and in-game menu functions. A pad with no touchpad button can get it from the dashboard, with Map buttons. |
| **Multi-touch Touchpad Surface** | ❌ Not Supported | Non-Sony controllers lack a capacitive touch surface. |
| **Adaptive Triggers (Motor Force)** | ❌ Not Supported | Non-DualSense controllers do not have internal resistance motors. |
| **Advanced DualSense Haptics** | ❌ Not Supported | Standard rumble is passed through where hardware permits. |
| **Gyroscope / Accelerometer (IMU)** | ❌ Neutral Only | Fixed to identity quaternion. (Roadmap for motion-capable controllers). |
