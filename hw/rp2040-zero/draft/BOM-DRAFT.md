# Historical draft bill of materials

**Design review only. Fabrication is on hold.** This list was derived from the distinct references and exact values in the reviewed [pin netlist](pin-netlist.csv). It covers 180 references and does not select footprints, qualify substitutions, or resolve any electrical hold.

Component links identify the parts or families named in the draft. Generic connectors and passives retain their recorded descriptions. The OLED, card, external strip, cables, assembly materials and other items outside the netlist have no entries.

## Module and connectors

| Draft part or value | Qty | References | Component link |
| --- | ---: | --- | --- |
| USB-C USB2 receptacle | 2 | J1, J2 | — |
| USB-A receptacle | 2 | J3, J4 | — |
| microSD socket | 1 | J5 | — |
| OLED keyed 1x4 | 1 | J6 | — |
| UART keyed 1x3 | 1 | J7 | — |
| RGB keyed 1x2 | 1 | J8 | — |
| Waveshare RP2040-Zero | 1 | M1 | [Manufacturer](https://www.waveshare.com/wiki/RP2040-Zero) |

## Active devices and protection

| Draft part or value | Qty | References | Component link |
| --- | ---: | --- | --- |
| TPD1E05U06DPYR | 7 | D101, D102, D121, D122, D171, D172, D181 | [Manufacturer](https://www.ti.com/product/TPD1E05U06) |
| BZT52C5V6 | 2 | D111, D131 | [Manufacturer](https://www.nexperia.com/products/diodes/zener-diodes/serie/bzt52-c-series/) |
| SMF6.0A TVS | 2 | D201, D211 | [Manufacturer](https://www.littelfuse.com/assetdocs/tvs-diodes-smf-datasheet?assetguid=7eb8a5b6-bdd0-4561-8f19-0c3cc6f9b2af) |
| PESD5V0S1UL | 4 | D202, D203, D212, D213 | [Manufacturer](https://www.nexperia.com/product/PESD5V0S1UL) |
| TPS259474LRPWR | 7 | IC1, IC2, IC4, IC5, IC6, IC23, IC24 | [Manufacturer](https://www.ti.com/product/TPS25947) |
| TPS2116DRLR | 3 | IC3, IC8, IC25 | [Manufacturer](https://www.ti.com/product/TPS2116) |
| TPS2553DBVR-1 | 1 | IC7 | [Manufacturer](https://www.ti.com/product/TPS2553-1) |
| TMUXHS221FRSWR | 2 | IC10, IC12 | [Manufacturer](https://www.ti.com/product/TMUXHS221F) |
| TLV809EA43DBZR | 1 | IC11 | [Manufacturer](https://www.ti.com/product/TLV809E) |
| TLV809EA46DCKR | 1 | IC13 | [Manufacturer](https://www.ti.com/product/TLV809E) |
| TLV75533PDBVR | 1 | IC14 | [Manufacturer](https://www.ti.com/product/TLV755P) |
| TXU0304PWR | 1 | IC15 | [Manufacturer](https://www.ti.com/product/TXU0304) |
| TCA9511ADGKR | 1 | IC16 | [Manufacturer](https://www.ti.com/product/TCA9511A) |
| SN74LVC2G125DCUR | 1 | IC17 | [Manufacturer](https://www.ti.com/product/SN74LVC2G125) |
| SN74LVC1G125DBVR | 1 | IC18 | [Manufacturer](https://www.ti.com/product/SN74LVC1G125) |
| TUSB320LAIRWBR | 2 | IC20, IC21 | [Manufacturer](https://www.ti.com/product/TUSB320LAI) |
| 2N7002 | 9 | Q10, Q12, Q13, Q40A, Q50A, Q60A, Q70A, Q230A, Q240A | [Manufacturer](https://www.nexperia.com/product/2N7002) |
| BSS84 | 6 | Q40B, Q50B, Q60B, Q70B, Q230B, Q240B | [Manufacturer](https://www.nexperia.com/product/BSS84) |

## Capacitors

| Draft part or value | Qty | References | Component link |
| --- | ---: | --- | --- |
| 2.2nF, 16V | 2 | C11, C21 | — |
| 2.2uF, 16V | 6 | C12, C22, C52, C71, C81, C141 | — |
| 4.7uF, 16V | 1 | C31 | — |
| 3.3nF, 16V | 4 | C41, C51, C231, C241 | — |
| 10uF, 16V | 1 | C42 | — |
| 10nF, 16V | 1 | C61 | — |
| 150uF, 10V | 1 | C62 | — |
| 100nF, 16V | 3 | C72, C111, C131 | — |
| 100nF | 9 | C101, C121, C151, C152, C154, C161, C162, C171, C181 | — |
| 4.7uF, 10V | 1 | C142 | — |
| 10uF, 10V | 1 | C153 | — |
| 100nF, 10V | 2 | C201, C211 | — |
| 1uF, 16V | 5 | C202, C212, C232, C242, C251 | — |

## Resistors

| Draft part or value | Qty | References | Component link |
| --- | ---: | --- | --- |
| 2.67k, 1% | 1 | R11 | — |
| 34.8k, 0.1% | 2 | R12, R22 | — |
| 10.0k, 0.1% | 2 | R13, R23 | — |
| 24.9k, 1% | 2 | R14, R24 | — |
| 10.0k, 1% | 2 | R15, R25 | — |
| 6.65k, 1% | 4 | R21, R41, R231, R241 | — |
| 301k, 1% | 3 | R31, R81, R251 | — |
| 100k, 1% | 3 | R32, R82, R252 | — |
| 4.7k | 10 | R46, R66, R112, R132, R161, R162, R163, R164, R221, R222 | — |
| 5.36k, 1% | 2 | R51, R61 | — |
| 105k, 1% | 1 | R75 | — |
| 47k | 8 | R101, R125, R151, R153, R154, R155, R156, R157 | — |
| 100k | 20 | R111, R131, R141, R158, R159, R173, R182, R223, R401, R402, R501, R502, R601, R602, R701, R702, R2301, R2302, R2401, R2402 | — |
| 22R | 3 | R121, R122, R152 | — |
| 15k, 1% | 2 | R123, R124 | — |
| 100R | 3 | R171, R172, R181 | — |
| 900k, 1% | 2 | R201, R211 | — |
| 24.9k | 5 | R403, R503, R603, R2303, R2403 | — |
| 10.0k | 7 | R404, R504, R604, R703, R704, R2304, R2404 | — |
