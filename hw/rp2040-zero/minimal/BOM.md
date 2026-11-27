# Reduced BOM notes

[The full generated BOM](bom.csv) has **102 references** including conditional SD/OLED interfaces and connector placeholders. The historical published draft counts 180. The reduction comes from removing active CC scanning in the qualified breakout case, using one physical module source bridge instead of automatic module failover, removing the separate A host grant stage, and wiring local accessories directly to the qualified module rail. U4 still has its protected B branch and source selection.

| Block | Proposed component basis |
| --- | --- |
| IC1/2/4/6 | TI TPS259474LRPWR breaker with reverse blocking. R11 2.67 kΩ, R21/R41 6.57 kΩ ±0.1%, R61 5.36 kΩ. Derive and measure fitted current corners, startup and fault behavior. The protected thresholds are separate from USB allocations. |
| IC7/8 | TPS2553DBVR-1 B branch with 105 kΩ ±1%, TPS2116DRLR manual selection. |
| IC10/12, IC11/13, IC26 | TMUXHS221FRSWR, TPS389001DSER, Nexperia 74AUP2G132DC for full-speed gating, actual VBUS sense and hard GP9 veto. |
| D101/D121 | TI TPD2E2U06QDBZRQ1 two-channel ESD, one at each data connector. |
| C61/R67 | 39 nF ±5% C0G in series with 100 Ω per TPS25947 dV/dt guidance. Murata GRM2195C2A393JE01 is the 0805 catalog basis. |
| C62/C63 | Panasonic 16SVPF270M E7 footprint candidate plus 1 µF 16 V ceramic close to IC6. Fit only when capacitance, ESR, polarity, temperature, aging, actual droop and placement support the 170–400 µF envelope. |
| SJ1, J1–J8, M1 | The published module family and functional connector maps are specified. Identify Jill's exact hardware, ratings and footprints before substitution. Use one exclusive module bridge. Check the OLED's actual pull-ups before fitting R64/R65. |

[TI TPS25947](https://www.ti.com/lit/ds/symlink/tps25947.pdf) · [TI ESD part](https://www.ti.com/product/TPD2E2U06-Q1/part-details/TPD2E2U06QDBZRQ1) · [Panasonic C62 candidate](https://industrial.panasonic.com/ww/products/pt/os-con/models/16SVPF270M)
