# SID chip pinouts

The SID is a **28-pin DIP**. The pinout is **identical across all variants** -
only the **supply voltage on pin 28 (Vdd)** and the **external filter
capacitors** differ. GND (pin 14) is 0 V, Vcc (pin 25) is +5 V, and all digital
lines (`/RES`, `φ2`, `R/W`, `/CS`, `A0-A4`, `D0-D7`) use 5 V TTL logic on every
variant.

| Variant | Vdd (pin 28) | Filter caps | Process | Notes |
| --- | --- | --- | --- | --- |
| MOS 6581 | **+12 V** | 470 pF | NMOS | Rich/resonant filter, wide chip-to-chip variation, loud `$D418` digis |
| MOS 8580 | **+9 V** | 22 nF | HMOS-II | Cleaner/consistent filter, quieter digis, lower noise |
| MOS 6582 | **+9 V** | 22 nF | HMOS-II | Same die as the 8580; electrically identical |

The diagrams below are rendered from the vector sources in
[`assets/`](../assets) (`sid-<variant>-pinout.svg`).

## MOS 6581

![MOS 6581 SID pinout](sid-6581-pinout.png)

## MOS 8580

![MOS 8580 SID pinout](sid-8580-pinout.png)

## MOS 6582

![MOS 6582 SID pinout](sid-6582-pinout.png)
