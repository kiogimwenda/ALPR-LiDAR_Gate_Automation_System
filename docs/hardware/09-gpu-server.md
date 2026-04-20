# Component Guide 09: GPU Server — The Inference Brain

## What This Part Does

This is the most powerful computer in the system. It runs the AI models that detect license plates (YOLOv9), read plate text (PaddleOCR), and classify vehicles from LiDAR data. The field PCB (ESP32-S3) sends camera frames and LiDAR data to this server, which processes them in milliseconds using the GPU and sends back the results. It also runs the dashboard web interface.

## Exact Parts

### Recommended Build

| Part | Model | Price (approx) |
|------|-------|-----------------|
| **Mini PC / SFF PC** | Any mini PC with PCIe x16 slot — e.g., MINISFORUM or BeeLink with desktop CPU | KES 30,000–50,000 / USD 230–385 |
| **GPU** | NVIDIA GeForce RTX 4060 8GB | KES 40,000–55,000 / USD 310–425 |
| **RAM** | 16–32 GB DDR4/DDR5 (depends on mini PC) | Included in mini PC or KES 5,000–10,000 |
| **Storage** | 256 GB NVMe SSD minimum (for OS + models) | Included in mini PC or KES 4,000–8,000 |
| **OS** | Debian 12 (Bookworm) or Ubuntu 24.04 LTS | Free |

### Alternative: Pre-Built Desktop

If you can't find a mini PC with a PCIe slot, use any desktop PC with:
- Intel Core i5 12th gen or newer (or AMD Ryzen 5 5600 or newer)
- 16 GB RAM
- PCIe x16 slot for the GPU
- 256 GB SSD
- Total budget: KES 60,000–80,000 / USD 460–615

### Why RTX 4060?

- Supports TensorRT 10.x with sm_89 architecture
- 8 GB VRAM — enough for YOLOv9 + PaddleOCR + LiDAR model simultaneously
- Low power (115W TDP) — suitable for 24/7 operation
- Widely available in Nairobi and online
- Best performance-per-KES for this workload

## Where to Buy

| Part | Source | Approximate Price |
|------|--------|-------------------|
| RTX 4060 | **Luthuli Avenue** — computer shops, Game stores | KES 40,000–55,000 |
| RTX 4060 | **AliExpress** — search "RTX 4060 8GB" | USD 250–350 (~KES 32,500–45,500) |
| Mini PC | **Amazon / AliExpress** — search "mini PC i5 PCIe" | USD 200–400 |
| Complete desktop | **Luthuli Avenue** — assembled to order | KES 50,000–70,000 (without GPU) |

## Step-by-Step Setup

**Step 1 — Assemble the PC.**
If using a mini PC or desktop, install the RTX 4060 GPU into the PCIe x16 slot. Connect the GPU's power cable (1× 8-pin PCIe power from the PSU). This is standard PC assembly — many YouTube tutorials cover it.

**Step 2 — Install Debian or Ubuntu.**
Boot from a USB installer. Install the OS with a user account. Connect to the network.

**Step 3 — Install NVIDIA drivers.**
```bash
# Add NVIDIA repo and install the driver
# Follow the official NVIDIA CUDA installation guide for your distro
sudo apt update
sudo apt install nvidia-driver-555  # or latest version
sudo reboot
nvidia-smi  # Verify GPU is visible
```

**Step 4 — Install the project stack.**
Run the bootstrap scripts from this repository:
```bash
git clone git@github.com:kiogimwenda/ALPR-LiDAR_Gate_Automation_System.git
cd ALPR-LiDAR_Gate_Automation_System
bash scripts/bootstrap/01-toolchain.sh
bash scripts/bootstrap/02-gpu-stack.sh
bash scripts/bootstrap/03-ml-tools.sh
bash scripts/bootstrap/04-vcpkg.sh
```

**Step 5 — Run the GPU smoke test.**
```bash
cd scripts/bootstrap
nvcc -std=c++20 -arch=sm_89 smoke_test.cu -o smoke_test \
    $(pkg-config --cflags --libs opencv4) \
    -lnvinfer -lnvonnxparser -lcudart
./smoke_test
```

**Step 6 — Connect to the PoE switch.**
Plug an Ethernet cable from the server's Ethernet port to Port 5 on the PoE switch. Set a static IP on the server (e.g., 192.168.1.50).

## Network Configuration

```bash
# Set static IP on the server's Ethernet interface
# Edit /etc/network/interfaces or use NetworkManager
sudo nmcli con mod "Wired connection 1" \
    ipv4.method manual \
    ipv4.addresses 192.168.1.50/24 \
    ipv4.gateway 192.168.1.1
sudo nmcli con up "Wired connection 1"
```

## Safety Notes

- **The GPU draws up to 115W** — ensure the PC's PSU is rated for the total system draw (CPU + GPU + disks). A 450W PSU is the minimum for an RTX 4060 build.
- **Keep the server in a ventilated location** — GPU temperature should stay below 80°C under load. Monitor with `nvidia-smi`.
- **For production:** Mount in a locked network cabinet. Use a UPS (Uninterruptible Power Supply) to handle brief power outages without losing gate state.
- **Ensure the server auto-boots after power loss** — set "Restore on AC Power Loss = Power On" in the BIOS.

## Datasheet Links

- [RTX 4060 Specifications (NVIDIA official)](https://www.nvidia.com/en-us/geforce/graphics-cards/40-series/rtx-4060/)
- [CUDA Toolkit Download](https://developer.nvidia.com/cuda-downloads)
- [TensorRT Download](https://developer.nvidia.com/tensorrt)
