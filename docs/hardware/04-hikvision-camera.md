# Component Guide 04: Hikvision DS-2CD2043G2-I — ALPR Camera

## What This Part Does

This camera takes clear pictures of vehicle license plates, day and night. It has built-in infrared LEDs that illuminate plates in complete darkness (the light is invisible to your eyes but shows up clearly in the camera image). The camera sends a continuous video stream over an Ethernet cable to our system.

## Exact Part

- **Manufacturer:** Hikvision
- **Model:** DS-2CD2043G2-I (2.8mm lens)
- **Resolution:** 4 MP (2688 × 1520 at 25 fps)
- **IR range:** 40m (built-in IR LEDs)
- **WDR:** 120 dB (handles headlights and shadows)
- **Lens:** 2.8mm fixed (wide angle — covers a full gate lane at 5–10m distance)
- **Network:** RJ45 Ethernet with PoE (Power over Ethernet)
- **Weatherproof:** IP67 (fully dustproof, can be submerged in 1m of water for 30 minutes)
- **Operating temp:** -30°C to 60°C

**Why 2.8mm lens:** At a gate, vehicles pass at 3–10m distance. The 2.8mm lens gives a wide enough field of view to capture the plate without needing to aim precisely. If your gate lane is longer (plates at 10–20m), get the **4mm** variant instead.

## Where to Buy

| Source | Approximate Price | Notes |
|--------|-------------------|-------|
| **Luthuli Avenue, Nairobi** — Every CCTV shop carries Hikvision | KES 8,000–12,000 | Very widely available. Ask for "Hikvision 4MP bullet camera with IR, the 2043 G2." |
| **Hubtech Kenya** (hubtech.co.ke) | KES 8,500–11,000 | Online order with Nairobi delivery. |
| **Glantix** (glantix.co.ke, BIHI Towers, Moi Avenue) | KES 8,000–10,500 | Walk-in shop in Nairobi CBD. |
| **AliExpress** — search "Hikvision DS-2CD2043G2-I" | USD 45–70 (~KES 5,900–9,100) | Cheaper but 3–4 week shipping. Ensure "Hikvision Authorized" seller. |

**Lead time:** Nairobi shops: same day. AliExpress: 2–4 weeks.

**This is the easiest part to buy in Nairobi** — Hikvision is the #1 CCTV brand in Kenya.

## What's in the Box

- 1× Hikvision DS-2CD2043G2-I camera
- 1× Mounting bracket (wall/ceiling mount) with screws and anchors
- 1× Waterproof Ethernet connector cap
- 1× Torx wrench (for adjusting the camera angle after mounting)
- 1× Quick start guide
- 1× Regulatory sticker

### What You Need to Buy Separately

- 1× Ethernet cable, Cat5e/Cat6, outdoor-rated if running outside (length as needed — up to 100m)
- 1× PoE switch or PoE injector (to power the camera through the Ethernet cable — see Guide 08)
- Optionally: 1× 12V/1A DC power supply (if you don't want to use PoE)

## Connections

```
                                    ┌─────────────┐
  ┌────────────────┐    Ethernet    │ PoE Switch  │    Ethernet    ┌─────────────┐
  │   Hikvision    ├────(Cat5e)────→│ (port with  ├────(Cat5e)────→│ GPU Server  │
  │   Camera       │    carries     │  PoE power) │                │             │
  │                │    power +     └─────────────┘                └─────────────┘
  │  (IP67 sealed) │    data
  └────────────────┘

  ONE cable does everything:
  - Power (48V PoE from the switch, converted to 12V inside the camera)
  - Video data (RTSP stream over TCP/IP)
  - Camera configuration (web interface over HTTP)

  The camera has only ONE port — the Ethernet port with the
  waterproof cap. That's it. One cable in, and you're done.
```

### Default Network Settings (from factory)

| Parameter | Value |
|-----------|-------|
| IP Address | 192.168.1.64 (or DHCP if a DHCP server is present) |
| Web interface | http://192.168.1.64 |
| RTSP URL | rtsp://admin:password@192.168.1.64:554/Streaming/Channels/101 |
| Default username | admin |
| Default password | (none — you set it on first login) |

## Step-by-Step Assembly

### What You Need

- [ ] Hikvision DS-2CD2043G2-I camera
- [ ] Ethernet cable (Cat5e or Cat6)
- [ ] PoE switch or PoE injector (already connected to your network)
- [ ] Computer on the same network (for initial configuration)
- [ ] Web browser

### Steps

**Step 1 — Unbox and inspect.**
Remove the camera from the box. It's a cylindrical metal tube (the "bullet" shape) with a glass lens on the front and a short Ethernet pigtail cable on the back. Check that the glass lens is clean and undamaged.

**Step 2 — Connect the Ethernet cable.**
Find the short Ethernet pigtail coming from the back of the camera. It ends in an RJ45 connector with a rubber waterproof boot. Plug your Ethernet cable into this connector. Screw on the waterproof cap if you're installing outdoors (hand-tight is enough).

**Step 3 — Connect to a PoE switch.**
Plug the other end of the Ethernet cable into a PoE-enabled port on your network switch. The switch will send power to the camera through the cable.

**Step 4 — Wait for boot.**
The camera takes 30–60 seconds to boot. You'll see the IR LEDs glow faintly red (visible in a dark room) when it's on. Some cameras emit a small click sound when the IR filter engages.

**Step 5 — Find the camera on your network.**
Open a web browser on a computer connected to the same switch. Try:
- http://192.168.1.64 (default static IP)
- If that doesn't work, the camera may have gotten a DHCP address. Use a network scanner:
  ```bash
  # On Linux:
  sudo nmap -sn 192.168.1.0/24 | grep -B2 "Hikvision"
  # Or use Hikvision's free SADP tool (Windows) to find the camera
  ```

**Step 6 — Set the admin password.**
On first login, the camera will ask you to create an admin password. Choose a strong password and write it down — you'll need it for the RTSP URL in the firmware.

**Step 7 — Configure settings for ALPR.**
In the camera web interface, set:
- **Video → Main Stream:** Resolution 2688×1520, Frame Rate 25, Bitrate Type VBR, Max Bitrate 6144 Kbps
- **Image → Day/Night:** Auto switch (the camera will use IR at night automatically)
- **Image → Exposure:** Set to 1/500 or faster if vehicles pass quickly (reduces motion blur)
- **Network → TCP/IP:** Set a static IP that matches your gate VLAN (e.g., 192.168.1.10)

**Step 8 — Test the RTSP stream.**
On your GPU server or development machine:
```bash
# Test with ffplay (from ffmpeg)
ffplay "rtsp://admin:YOUR_PASSWORD@192.168.1.10:554/Streaming/Channels/101"

# Or test with OpenCV Python
python3 -c "
import cv2
cap = cv2.VideoCapture('rtsp://admin:YOUR_PASSWORD@192.168.1.10:554/Streaming/Channels/101')
ret, frame = cap.read()
print(f'Frame captured: {ret}, shape: {frame.shape if ret else \"N/A\"}')
cap.release()
"
```

## First Power-On

**What you should see:**
1. After plugging into PoE switch: nothing visible for 30 seconds (booting).
2. After ~30 seconds: a faint red glow from the front if the room is dark (IR LEDs).
3. Ethernet link LED on the switch port should be green.

**If the camera doesn't power on:**
- Verify the switch port supports PoE (not all ports on a PoE switch are PoE-enabled — check the switch manual).
- Try a different Ethernet cable.
- If you don't have a PoE switch, connect a 12V/1A DC power supply to the camera's DC power input (if present — some models have a separate DC barrel jack).

## Bring-Up Test Program

```cpp
// test_camera.cpp — Hikvision RTSP capture test (runs on GPU server)
#include <cstdio>
#include <opencv2/opencv.hpp>

int main() {
    printf("Hikvision Camera Bring-Up Test\n");

    // Replace with your camera's IP and password
    const char* rtsp_url =
        "rtsp://admin:YOUR_PASSWORD@192.168.1.10:554/Streaming/Channels/101";

    cv::VideoCapture cap(rtsp_url, cv::CAP_FFMPEG);
    if (!cap.isOpened()) {
        printf("ERROR: Cannot open RTSP stream at %s\n", rtsp_url);
        printf("  - Check camera IP and password\n");
        printf("  - Check network connectivity (ping the camera)\n");
        return 1;
    }

    printf("Connected! Resolution: %.0fx%.0f @ %.0f fps\n",
           cap.get(cv::CAP_PROP_FRAME_WIDTH),
           cap.get(cv::CAP_PROP_FRAME_HEIGHT),
           cap.get(cv::CAP_PROP_FPS));

    cv::Mat frame;
    for (int i = 0; i < 5; ++i) {
        if (cap.read(frame)) {
            printf("Frame %d: %dx%d, channels=%d\n",
                   i, frame.cols, frame.rows, frame.channels());
        } else {
            printf("Frame %d: FAILED to read\n", i);
        }
    }

    cap.release();
    printf("\nCamera test PASSED!\n");
    return 0;
}
```

**Compile and run:**
```bash
g++ -std=c++20 test_camera.cpp -o test_camera $(pkg-config --cflags --libs opencv4)
./test_camera
```

**Expected output:**
```
Hikvision Camera Bring-Up Test
Connected! Resolution: 2688x1520 @ 25 fps
Frame 0: 2688x1520, channels=3
Frame 1: 2688x1520, channels=3
...
Camera test PASSED!
```

## Failure Signatures

| Symptom | Cause | Fix |
|---------|-------|-----|
| Camera doesn't power on (no IR glow) | No PoE / wrong switch port | Verify PoE port. Try 12V DC power supply as backup. |
| Can't find camera on network | Wrong subnet or camera IP changed | Use SADP tool or nmap scan. Factory reset: hold RESET button 15 seconds. |
| RTSP stream timeout | Firewall blocking, wrong URL, or wrong password | Ping camera first. Check password. Try `/Streaming/Channels/101` and `/101`. |
| Blurry plate images at night | IR LEDs not activating, or lens dirty | Check Day/Night mode is "Auto". Clean lens with microfiber cloth. |
| Motion blur on moving vehicles | Exposure too slow | Set exposure to 1/500s or faster in camera web interface. |

## Datasheet Links

- [DS-2CD2043G2-I Datasheet (Hikvision official)](https://www.hikvision.com/en/products/IP-Products/Network-Cameras/Pro-Series-EasyIP-/ds-2cd2043g2-i/)

## Safety Notes

- **PoE voltage is 48V DC** — not lethal but can give an unpleasant shock. Do not touch bare Ethernet wire ends while the PoE switch is on.
- **IR LEDs** — the infrared light is invisible but present. Do not stare directly into the camera lens at close range (<30cm) for extended periods, especially in a dark room when IR is at full power.
- **Mounting height** — install the camera at 2–3m height, angled down 15–30° to capture plates. Use the provided wall anchors for secure mounting. A falling camera could cause injury.
- **Waterproofing** — always screw on the waterproof Ethernet cap for outdoor installations. If water enters the camera, it will be permanently damaged.
