# Historical RP2040-Zero draft archive

**Design review only. Fabrication, electrical qualification and powered testing are on hold.** This directory archives the exact reviewed carrier draft. Hardware Design owns the separate Rev A successor.

## Reviewed design

- [Rendered electrical schematic](Gridpico-RP2040-Zero-Schematic.pdf) and [editable SVG sheets](drawings/).
- [Electrical source and pin assignments](board.py), [renderer](render.py), [render checks](verify_render.py) and [original review README](README.md).
- [External and module pinout](pinout.csv), [all-pin netlist](pin-netlist.csv) and [named-net listing](nets.txt).
- [Original power review](POWER-REVIEW.md), [calculation source](power_budget.py), [calculation output](power-budget.txt) and [design checks](design-check.txt).
- [Derived draft BOM](BOM-DRAFT.md). The archive added this list from the reviewed netlist. Its component links provide manufacturer references.

The original [design manifest](SHA256SUMS) has SHA-256 **25dbfc048bc370898640e842a84f8a03d5716f48b2742c6a44791b48a75bb62c**. Run its checks from this directory. The input file bytes, including the generated CSV line endings, are preserved.

## Review and topology

- [Independent HAL electrical review](review/hal/RESULT.md) and [sheet-by-sheet findings](review/hal/SHEET-DELTAS.md).
- [U4 voltage and data-enable hold](ELECTRICAL-HOLD.md) and [chain authorization, inrush and fault hold](CHAIN-REVIEW-HOLD.md). The [hold manifest](CHAIN-REVIEW-HOLD.sha256) covers both.
- [Product brief board sketch](../../../PRODUCT_BRIEF.md#rp2040-zero-board-sketch) and [architecture context](../../../ARCHITECTURE.md). The reviewed draft places each upstream input on a separately protected rail. U3 draws only from U1. U4 and the optional local-peripheral rail have separately granted paths from either input. The native USB data connection uses a replacement U2 receptacle. The historical module receptacle must be removed because its VBUS lands share the module 5 V pad.

The historical product topology review's **Three units** discussion is a separate receipt, SHA-256 **fe4c4094c7fe09b8d2e7dbeb0fcd152d95d492cf08893f3e5efc58de9311e495**, based on Product Brief blob **ce218fb3c23753587b7c5b39f632b3af495729d2**. It describes PC host to Pico A and independent USB host/device hops from A to B and B to C. Each hop has its own verified roles and deliberate routes. USB or CLI access to B or C requires a separately scoped carrier. The schematic and HAL review own electrical qualification.

The [clarified product topology review](../../../design/reviews/pico-pc-web.md) records the later Cable 2 and direct PC control decisions. The draft-specific review is the locally archived HAL receipt above, SHA-256 **cc9a8538db466484301464201076db8fffca3ef358299695114c49f52e0d1653**.

Some exact original review notes cite their scratch evidence paths. The local links above locate the retained review documents and source. They carry the historical findings forward without changing their bytes.
