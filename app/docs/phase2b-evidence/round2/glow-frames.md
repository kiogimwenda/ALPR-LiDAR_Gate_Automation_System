# Accent glow fade: frames while the glow fades in (battery power, 5 runs)

The glow is a pre-rendered image (accent_glow.png) behind the accent card; it fades in by opacity alone once the blob clip has settled (last `blob.clip` event). Window: blob settle → +500 ms.

| trace | blob settled (ms) | frames in window | longest frame (ms) | frames > 25 ms | longest frame in the 500 ms before (ms) |
|---|---|---|---|---|---|
| A1-F-1 | 476 | 25 | 24.6 | 0 | 62.5 |
| A1-L-1 | 464 | 24 | 27.7 | 2 | 37.0 |
| A1-L-0 | 469 | 47 | 25.7 | 1 | 39.6 |
| A2-F-1 | 475 | 28 | 22.4 | 0 | 60.6 |
| A2-L-1 | 458 | 26 | 21.3 | 0 | 40.1 |
| A2-L-0 | 474 | 46 | 26.2 | 2 | 32.7 |
| A3-F-1 | 459 | 25 | 23.5 | 0 | 67.4 |
| A3-L-1 | 466 | 29 | 22.1 | 0 | 23.6 |
| A3-L-0 | 453 | 46 | 26.9 | 2 | 34.0 |
| A4-F-1 | 480 | 25 | 22.9 | 0 | 62.9 |
| A4-L-1 | 469 | 24 | 26.0 | 1 | 36.9 |
| A4-L-0 | 461 | 47 | 22.6 | 0 | 32.2 |
| A5-F-1 | 475 | 21 | 94.7 | 1 | 42.0 |
| A5-L-1 | 457 | 23 | 29.9 | 4 | 36.2 |
| A5-L-0 | 476 | 48 | 22.3 | 0 | 36.3 |
