# U4 data-enable and loaded VBUS hold

Applies to the immutable RP2040-Zero review draft, manifest SHA-256 **25dbfc048bc370898640e842a84f8a03d5716f48b2742c6a44791b48a75bb62c**. This companion records Linux HAL's early electrical finding. Pause qualification, fabrication and powered evaluation of this revision pending an electrical correction and owner review.

## U4 data enable

On schematic sheet 5, IC13 (TLV809EA46DCKR) is powered from U4_VBUS through R132 (4.7 kΩ), with C131 and D131 on U4_SENSE_5. Its push-pull reset output drives Q12 and R131 (100 kΩ to ground). Q12 and the separate port-enable Q13 must both conduct to connect the U4 D pair.

- At the data-sheet +2% threshold and +1.5% hysteresis corners, the 4.63 V nominal falling threshold can be **4.723 V** and the rising threshold **4.793 V at IC13**. A nominally valid **4.750 V at J4** therefore cannot guarantee reset release, even with zero loss across R132.
- Before release, R131 is driven low. After release, a high reset output draws current through R131, which increases the drop on R132. Taking the high output near its local supply gives U4_SENSE_5 approximately U4_VBUS ÷ 1.047, before supervisor current, clamp leakage and resistor tolerance. The worst-corner falling/hold requirement screens at about **4.945 V raw**. Multiplying the rising thought-experiment threshold by the same load factor gives **5.019 V** as a conservative loaded-rise screening value, while initial release itself occurs with the output low. The draft budget's **4.861 V** middle-port example can clear the unloaded release threshold yet fall below the required hold voltage after the output changes state.
- A successor must prove stable attach and hold at the chosen minimum allowed U4 receptacle voltage, with ripple, clamp leakage, tolerances, temperature and gate loading accounted for. It must also keep data open on absent or invalid VBUS or a withdrawn port enable. Threshold and output loading both need review. [TI TLV809E thresholds and hysteresis](https://www.ti.com/lit/ds/symlink/tlv809e.pdf)

## Loaded U4 VBUS

A 4.75 V input cannot produce 4.75 V at the loaded downstream receptacle through the draft's passive series elements. This voltage shortfall is independent of IC13. The three-unit screening calculation puts the middle U4 at **4.511 V** with 4.75 V measured at the head U1. Its qualified test concept assumes at least **5.10 V at the head connector under load**, limited cable resistance and named load budgets. Those assumptions still do not close the IC13 hold above. The successor must state the minimum qualified input at load, or qualify a different power path that maintains the required receptacle voltage. [Original power calculation](power-budget.txt) · [USB-IF drop and droop](https://www.usb.org/sites/default/files/USB20_32_BC12_Drop_Droop_1_4_1.pdf)

The Waveshare stock USB-C VBUS-to-P1.23/VSYS tie is independently confirmed. The carrier concept retains the mandatory removal of the stock receptacle and insulation of former VBUS contacts. [Waveshare drawing](https://files.waveshare.com/upload/4/4c/RP2040_Zero.pdf)
