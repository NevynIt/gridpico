"""Editable electrical design. Pin triples are (package pin, function, net).
None identifies a deliberate no-connect. Equal net labels are electrical joins.
Coordinates use the review canvas defined in render.py.
"""
from dataclasses import dataclass, field

@dataclass
class Part:
    ref: str
    value: str
    pins: list
    kind: str = "ic"

@dataclass
class Sheet:
    title: str
    subtitle: str
    cells: list = field(default_factory=list)

    def cell(self, x, y, w, h, title, notes=()):
        panel = Panel(x, y, w, h, title, notes)
        self.cells.append(panel)
        return panel

@dataclass
class Panel:
    x: int
    y: int
    w: int
    h: int
    title: str
    notes: tuple
    placed: list = field(default_factory=list)

    def part(self, x, y, ref, value, left, right=()):
        self.placed.append((x, y, len(left), Part(ref, value, list(left) + list(right))))

    def p(self, x, y, ref, value, a, b, kind=None):
        self.placed.append((x, y, 1, Part(ref, value, [("1", "", a), ("2", "", b)], kind or ref[0].lower())))

def pins(*items):
    return [(str(num), name, net) for num, name, net in items]

def fuse(panel, ref, vin, vout, en, ilm, dvdt, ovlo=True):
    panel.part(420, 48, f"IC{ref}", "TPS259474LRPWR", pins(
        (5, "IN", vin), (1, "EN/UVLO", en), (2, "OVLO", f"OV{ref}" if ovlo else "GND"),
        (8, "GND", "GND"),
    ), pins((6, "OUT", vout), (7, "DVDT", f"DV{ref}"), (9, "ILM", f"IL{ref}"),
            (3, "PG", None), (4, "PGTH", "GND"), (10, "ITIMER", None)))
    panel.p(240, 257, f"R{ref}1", ilm, f"IL{ref}", "GND")
    panel.p(560, 257, f"C{ref}1", dvdt, f"DV{ref}", "GND")
    if ovlo:
        panel.p(240, 303, f"R{ref}2", "34.8k, 0.1%", vin, f"OV{ref}")
        panel.p(560, 303, f"R{ref}3", "10.0k, 0.1%", f"OV{ref}", "GND")

def mux(panel, ref, a, b, out):
    panel.part(340, 75, f"IC{ref}", "TPS2116DRLR", pins(
        (3, "VIN1", a), (6, "VIN2", b), (5, "MODE", a), (4, "PR1", f"PR{ref}"),
        (1, "GND", "GND"),
    ), pins(("2,7", "VOUT", out), (8, "ST", None)))
    panel.p(330, 262, f"R{ref}1", "301k, 1%", a, f"PR{ref}")
    panel.p(730, 262, f"R{ref}2", "100k, 1%", f"PR{ref}", "GND")

def gate(panel, ref, power, grant, en, top="24.9k"):
    # An isolated source-local enable. The GPIO goes only to a MOS gate.
    panel.part(216, 58, f"Q{ref}A", "2N7002", pins(
        (1, "Gate", grant), (2, "Source", "GND"),
    ), pins((3, "Drain", f"PG_{ref}")))
    panel.part(545, 58, f"Q{ref}B", "BSS84", pins(
        (1, "Gate", f"PG_{ref}"), (2, "Source", power),
    ), pins((3, "Drain", f"EN_SRC_{ref}")))
    panel.p(167, 190, f"R{ref}1", "100k", grant, "GND")
    panel.p(520, 190, f"R{ref}2", "100k", power, f"PG_{ref}")
    panel.p(167, 240, f"R{ref}3", top, f"EN_SRC_{ref}", en)
    panel.p(520, 240, f"R{ref}4", "10.0k", en, "GND")

def typec(panel, j, ic, raw, prefix, usedata):
    panel.part(195, 55, j, "USB-C USB2 receptacle", pins(
        ("A4,A9,B4,B9", "VBUS", raw), ("A1,A12,B1,B12", "GND", "GND"),
        ("S", "Shell", "GND"), ("A5", "CC1", f"{prefix}_CC1"), ("B5", "CC2", f"{prefix}_CC2"),
        ("A6,B6", "D+", f"{prefix}_DP" if usedata else None),
        ("A7,B7", "D-", f"{prefix}_DM" if usedata else None),
        ("A8,B8", "SBU", None),
    ))
    panel.part(475, 55, f"IC{ic}", "TUSB320LAIRWBR", pins(
        (1, "CC1", f"{prefix}_CC1"), (2, "CC2", f"{prefix}_CC2"),
        (4, "VBUS_DET", f"{prefix}_DET"), (3, "PORT (UFP)", "GND"),
        (5, "ADDR", "3V3_AON" if usedata else "GND"), (11, "EN_N", "GND"),
        (10, "GND", "GND"),
    ), pins((12, "VDD", "3V3_AON"), (7, "SDA", "CC_SDA"), (8, "SCL", "CC_SCL"),
            (6, "INT_N", None), (9, "ID", None)))
    panel.p(190, 339, f"R{ic}1", "900k, 1%", raw, f"{prefix}_DET")
    panel.p(525, 339, f"C{ic}1", "100nF, 10V", "3V3_AON", "GND")
    panel.p(190, 389, f"C{ic}2", "1uF, 16V", raw, "GND")
    panel.p(525, 389, f"D{ic}1", "SMF6.0A TVS", raw, "GND", "d")
    panel.p(190, 439, f"D{ic}2", "PESD5V0S1UL", f"{prefix}_CC1", "GND", "d")
    panel.p(525, 439, f"D{ic}3", "PESD5V0S1UL", f"{prefix}_CC2", "GND", "d")

def usb_a(panel, ref, vbus, data=False):
    panel.part(285, 58, ref, "USB-A receptacle", pins(
        (1, "VBUS", vbus), (2, "D-", "H_DM" if data else None),
        (3, "D+", "H_DP" if data else None), (4, "GND", "GND"), ("S", "Shell", "GND"),
    ))

def usb_mux(panel, ref, cplus, cminus, pplus, pminus, oe, x=395, y=48):
    panel.part(x, y, ref, "TMUXHS221FRSWR", pins(
        (1, "D+ common", cplus), (2, "D- common", cminus), (8, "OE_N", oe),
        (10, "SEL", "GND"), (3, "GND", "GND"),
    ), pins((5, "D1+", pplus), (4, "D1-", pminus), (7, "D2+", None),
            (6, "D2-", None), (9, "VCC", "3V3_AON")))

sheets = []

s = Sheet("USB-C inputs and current detection",
          "U1 is power-only. U2 is the native USB Device. Both Type-C connectors are 5 V sink/UFP, with separate CC state.")
sheets.append(s)
p = s.cell(38, 126, 744, 578, "U1 / J1   USB-C power input", (
    "TUSB320 provides the sink Rd, including dead-battery mode. Address 0x47.",
    "No DP, DM, SBU, USB-PD, VCONN or source/Rp wiring. U1_RAW is an input only.",
))
typec(p, "J1", 20, "U1_RAW", "C1", False)
p = s.cell(802, 126, 744, 578, "U2 / J2   Board-mounted USB-C Device", (
    "Address 0x67. Tie two D+ contacts and two D- contacts locally. Gated path to native PHY on sheet 5.",
    "This is the only externally accessible native connector. The module receptacle is removed.",
))
typec(p, "J2", 21, "U2_RAW", "C2", True)
p = s.cell(38, 724, 744, 242, "Always-on CC control bus", ("Use attached sink state plus current advertisement. Unknown, detach and reset clear the relevant grant.",))
p.p(195, 74, "R221", "4.7k", "3V3_AON", "CC_SDA")
p.p(520, 74, "R222", "4.7k", "3V3_AON", "CC_SCL")
p.p(195, 133, "R223", "100k", "U2_RAW", "GND")
p = s.cell(802, 724, 744, 242, "Input capacitance and shield", (
    "C202/C212 are raw VBUS capacitors. The eFuses isolate the larger managed load.",
    "Shells and ESD returns bond to board ground near the connectors. Review chassis/ESD in layout.",
    "These grounds are common. The power design isolates VBUS paths, with no galvanic isolation.",
))

s = Sheet("Protected inputs and module supply",
          "Input eFuses block reverse current. A5 and B5 feed only the mux and their own authorized output branches.")
sheets.append(s)
for x, ref, name, raw, rail, resistance in [
    (38, 1, "U1 input and reverse block", "U1_RAW", "A5", "2.67k, 1%"),
    (802, 2, "U2 input and reverse block", "U2_RAW", "B5", "6.65k, 1%"),
]:
    p = s.cell(x, 126, 744, 478, name, ("OVLO nominal 5.376 V. UVLO nominal 4.188 V rising. ITIMER open for minimum delay.",))
    fuse(p, ref, raw, rail, f"EN{ref}", resistance, "2.2nF, 16V")
    p.p(240, 338, f"R{ref}4", "24.9k, 1%", raw, f"EN{ref}")
    p.p(560, 338, f"R{ref}5", "10.0k, 1%", f"EN{ref}", "GND")
    p.p(240, 389, f"C{ref}2", "2.2uF, 16V", rail, "GND")
p = s.cell(38, 624, 1040, 339, "Module and local supply priority", (
    "Prioritize valid A5. B5 sustains SYS5 if A5 falls. Feed SYS5 exclusively to M1 P1.23 (5V/VSYS).",
))
mux(p, 3, "A5", "B5", "SYS5")
p.p(730, 145, "C31", "4.7uF, 16V", "SYS5", "GND")
p = s.cell(1098, 624, 448, 339, "Power-domain names", (
    "U1_RAW and U2_RAW are upstream pins.",
    "A5 and B5 remain independent protected rails.",
    "SYS5 is local power. It never feeds either receptacle input.",
    "3V3_AON is the module regulator output.",
    "3V3_PER is an independently enabled local rail.",
))

s = Sheet("Protected U3 and U4 VBUS outputs",
          "U3 has its own A-only branch. U4 arbitrates independently gated A and B branches before its final reverse-blocking switch.")
sheets.append(s)
p = s.cell(38, 126, 744, 381, "U3 / J3   A5 only, USB-A power", (
    "No data or charging signature. Qualified 300 mA load envelope. U3 has no path from B5.",
))
fuse(p, 4, "A5", "U3_VBUS", "EN4", "6.65k, 1%", "3.3nF, 16V", False)
p.p(545, 289, "C42", "10uF, 16V", "U3_VBUS", "GND")
p.p(225, 289, "R46", "4.7k", "U3_VBUS", "GND")
p = s.cell(802, 126, 744, 381, "U4 source A   A5 after explicit grant", (
    "A5 to A_HOST only through IC5 while U4_A_GRANT is high. Continuous port load target 500 mA.",
))
fuse(p, 5, "A5", "A_HOST", "EN5", "5.36k, 1%", "3.3nF, 16V", False)
p.p(376, 289, "C52", "2.2uF, 16V", "A_HOST", "GND")
p = s.cell(38, 527, 744, 432, "U4 source B   B5 after explicit grant and limited current", (
    "B grant requires documented upstream power permission. B-powered U4 design envelope 200 mA continuous.",
    "IC7 limits the B branch and latches on fault. A and B grants are independently asserted, never tied together.",
))
p.part(386, 55, "IC7", "TPS2553DBVR-1", pins((1, "IN", "B5"), (3, "EN", "EN7"), (2, "GND", "GND")),
       pins((6, "OUT", "B_HOST"), (5, "ILIM", "IL7"), (4, "FAULT_N", None)))
p.p(213, 222, "R75", "105k, 1%", "IL7", "GND")
p.p(535, 222, "C71", "2.2uF, 16V", "B_HOST", "GND")
p.p(535, 274, "C72", "100nF, 16V", "B5", "GND")
p = s.cell(802, 527, 744, 432, "Host source selection   Only permitted inputs reach HOST_PRE", (
    "IC8 prefers A_HOST. Per-source enable is upstream of IC8, so a loss of A cannot acquire an ungranted B source.",
))
p.part(395, 55, "IC8", "TPS2116DRLR", pins(
    (3, "VIN1", "A_HOST"), (6, "VIN2", "B_HOST"), (5, "MODE", "A_HOST"), (4, "PR1", "PR8"),
    (1, "GND", "GND")), pins(("2,7", "VOUT", "HOST_PRE"), (8, "ST", None)))
p.p(205, 247, "R81", "301k, 1%", "A_HOST", "PR8")
p.p(530, 247, "R82", "100k, 1%", "PR8", "GND")
p.p(362, 296, "C81", "2.2uF, 16V", "HOST_PRE", "GND")

s = Sheet("U4 output stage and fail-off enables",
          "Each grant MOS gate drives a source-local enable. An absent source cannot be powered through an RP2040 GPIO.")
sheets.append(s)
p = s.cell(38, 126, 744, 381, "U4 / J4   Protected PIO host VBUS", (
    "C62 must provide at least 120uF effective at the port. Data connect on sheet 5. IC6 soft-starts the bulk.",
))
fuse(p, 6, "HOST_PRE", "U4_VBUS", "EN6", "5.36k, 1%", "10nF, 16V", False)
p.p(544, 289, "C62", "150uF, 10V", "U4_VBUS", "GND")
p.p(218, 289, "R66", "4.7k", "U4_VBUS", "GND")
p = s.cell(802, 126, 744, 381, "U3 enable   GP6 grants only the A branch", ("The gate pull-down holds the output off through reset.",))
gate(p, 40, "A5", "U3_A_GRANT", "EN4")
p = s.cell(38, 527, 744, 320, "U4 A enable   GP7 enables A_HOST", ("Use only with an available U1 budget.",))
gate(p, 50, "A5", "U4_A_GRANT", "EN5")
p = s.cell(802, 527, 744, 320, "U4 B enable   GP8 enables B_HOST", ("Use only with a valid independent U2 budget. IC7 EN divider is scaled for logic threshold.",))
gate(p, 70, "B5", "U4_B_GRANT", "EN7", "10.0k")
p = s.cell(38, 867, 744, 320, "U4 final enable   GP9 starts the port after source settles", ("The same request also gates the host data switch. GP9 low wins even when VBUS is externally forced.",))
gate(p, 60, "HOST_PRE", "U4_PORT_EN", "EN6")
p = s.cell(802, 867, 744, 320, "Sequencing", (
    "1. Establish one source grant and let HOST_PRE settle.",
    "2. Assert U4_PORT_EN. IC6 charges C62. Data remains open until the VBUS supervisor is good.",
    "3. On detach, suspend, lost allowance or source change, drop U4_PORT_EN before reevaluation.",
    "All GPIOs return low on reset. A source change may drop USB and require normal re-enumeration.",
))

s = Sheet("Native and PIO USB data paths",
          "Separate switch channels and VBUS-qualified hardware enables. Grounds are common. No DP/DM path links U2 to U4.")
sheets.append(s)
p = s.cell(38, 126, 744, 471, "U2 native full-speed data", (
    "Remove M1's onboard receptacle. Connect only its former A/B data lands to the D1 side.",
    "Existing Waveshare 27R series parts remain. U2 DP and DM never touch module VSYS.",
))
usb_mux(p, "IC10", "C2_DP", "C2_DM", "N_DP", "N_DM", "NUSB_OE_N")
p.p(203, 246, "C101", "100nF", "3V3_AON", "GND")
p.p(523, 246, "R101", "47k", "3V3_AON", "NUSB_OE_N")
p.p(203, 298, "D101", "TPD1E05U06DPYR", "C2_DP", "GND", "d")
p.p(523, 298, "D102", "TPD1E05U06DPYR", "C2_DM", "GND", "d")
p = s.cell(802, 126, 744, 471, "U4 PIO USB full-speed host data", (
    "D+ GP14 and D- GP15, matching the installed DPDM pin assignment. Series resistors at M1.",
    "Fit 15k pulldowns at J4. Qualify the analog switch and disable parallel weak PIO pulls in the board profile.",
))
usb_mux(p, "IC12", "H_DP", "H_DM", "HP_DP", "HP_DM", "HUSB_OE_N")
p.p(170, 240, "R121", "22R", "PIO_DP", "HP_DP")
p.p(540, 240, "R122", "22R", "PIO_DM", "HP_DM")
p.p(170, 289, "R123", "15k, 1%", "H_DP", "GND")
p.p(540, 289, "R124", "15k, 1%", "H_DM", "GND")
p.p(170, 339, "D121", "TPD1E05U06DPYR", "H_DP", "GND", "d")
p.p(540, 339, "D122", "TPD1E05U06DPYR", "H_DM", "GND", "d")
p = s.cell(38, 617, 744, 368, "U2 VBUS qualifies the native data connection", (
    "GP18 reads active-low NUSB_OE_N. Supervisor is powered by U2_RAW only, and cannot be powered by SYS5.",
    "When U2_RAW is absent, Q10 gate falls low. AON pull-up holds the USB switch open.",
))
p.part(188, 56, "IC11", "TLV809EA43DBZR", pins((3, "VDD", "U2_SENSE_5"), (1, "GND", "GND")),
       pins((2, "RESET_N", "N_VBUS_GOOD")))
p.part(570, 72, "Q10", "2N7002", pins((1, "Gate", "N_VBUS_GOOD"), (2, "Source", "GND")),
       pins((3, "Drain", "NUSB_OE_N")))
p.p(190, 191, "C111", "100nF, 16V", "U2_SENSE_5", "GND")
p.p(190, 244, "R112", "4.7k", "U2_RAW", "U2_SENSE_5")
p.p(555, 244, "D111", "BZT52C5V6", "U2_SENSE_5", "GND", "d")
p.p(555, 191, "R111", "100k", "N_VBUS_GOOD", "GND")
p = s.cell(802, 617, 744, 426, "U4 data connect needs VBUS good and local port enable", (
    "Q12 and Q13 in series. If the port VBUS is externally driven while U4_PORT_EN is low, data still stays open.",
))
p.part(203, 55, "IC13", "TLV809EA46DCKR", pins((3, "VDD", "U4_SENSE_5"), (1, "GND", "GND")),
       pins((2, "RESET_N", "H_VBUS_GOOD")))
p.part(567, 72, "Q12", "2N7002", pins((1, "Gate", "H_VBUS_GOOD"), (2, "Source", "H_OE_MID")),
       pins((3, "Drain", "HUSB_OE_N")))
p.part(567, 170, "Q13", "2N7002", pins((1, "Gate", "U4_PORT_EN"), (2, "Source", "GND")),
       pins((3, "Drain", "H_OE_MID")))
p.p(170, 179, "R125", "47k", "3V3_AON", "HUSB_OE_N")
p.p(170, 209, "C121", "100nF", "3V3_AON", "GND")
p.p(170, 260, "C131", "100nF, 16V", "U4_SENSE_5", "GND")
p.p(560, 292, "R131", "100k", "H_VBUS_GOOD", "GND")
p.p(170, 334, "R132", "4.7k", "U4_VBUS", "U4_SENSE_5")
p.p(560, 334, "D131", "BZT52C5V6", "U4_SENSE_5", "GND", "d")

s = Sheet("Waveshare RP2040-Zero module pinout",
          "Waveshare drawing P1/P2/P3 numbering. GP16 is the built-in pixel DIN. Physical footprint must match the purchased revision.")
sheets.append(s)
p = s.cell(38, 126, 1031, 836, "M1   RP2040-Zero module, original receptacle removed", (
    "No external power input at 3V3_AON. Keep built-in RESET and BOOTSEL accessible.",
))
p.part(510, 60, "M1", "Waveshare RP2040-Zero", pins(
    ("P1.1", "GP0 UART0 TX", "UART0_TX"), ("P1.2", "GP1 UART0 RX", "UART0_RX"),
    ("P1.3", "GP2 I2C1 SDA", "CC_SDA"), ("P1.4", "GP3 I2C1 SCL", "CC_SCL"),
    ("P1.5", "GP4 I2C0 SDA", "OLED_SDA_A"), ("P1.6", "GP5 I2C0 SCL", "OLED_SCL_A"),
    ("P1.7", "GP6 SIO", "U3_A_GRANT"), ("P1.8", "GP7 SIO", "U4_A_GRANT"),
    ("P1.9", "GP8 SIO", "U4_B_GRANT"), ("P1.10", "GP9 SIO", "U4_PORT_EN"),
    ("P1.11", "GP10 SPI1 SCK", "SD_SCK_A"), ("P1.12", "GP11 SPI1 TX", "SD_MOSI_A"),
    ("P1.13", "GP12 SPI1 RX", "SD_MISO_A"), ("P1.14", "GP13 SPI1 CS", "SD_CS_A"),
    ("P1.15", "GP14 PIO D+", "PIO_DP"), ("P1.16", "GP15 PIO D-", "PIO_DM"),
    ("P1.17", "GP26", None), ("P1.18", "GP27", None), ("P1.19", "GP28", None), ("P1.20", "GP29", None),
    ("P1.21", "3V3 output", "3V3_AON"), ("P1.22", "GND", "GND"), ("P1.23", "5V / VSYS", "SYS5"),
), pins(
    ("P2.1", "GP17 RGB data", "RGB_A"), ("P2.2", "GP18 native VBUS_N", "NUSB_OE_N"),
    ("P2.3", "GP19 peripheral EN", "PER_EN"), ("P2.4", "GP20 host VBUS_N", "HUSB_OE_N"),
    ("P2.5", "GP21", None), ("P3.1", "GP22 A peripheral grant", "PER_A_GRANT"),
    ("P3.2", "GP23 B peripheral grant", "PER_B_GRANT"), ("P3.3", "GP24", None), ("P3.4", "GP25", None), ("P3.5", "GND", "GND"),
    ("T-C.A6,B6", "Onboard D_P lands", "N_DP"), ("T-C.A7,B7", "Onboard D_N lands", "N_DM"),
    ("T-C.VBUS", "Lands remain tied to P1.23", "SYS5"),
    ("T-C.CC1,CC2", "Old Rd nodes, no mating receptacle", None),
    ("INT", "GP16 onboard WS2812B", None),
))
p = s.cell(1089, 126, 457, 836, "Native USB module interface", (
    "Vendor schematic connects the stock VBUS lands directly to VSYS and P1.23.",
    "Remove the stock connector. Insulate its former VBUS lands after verification.",
    "Use short, controlled-impedance D_P/D_N land connections. Keep native series resistors on M1.",
    "Leave old CC lands isolated externally. The two stock Rd can remain on those unused lands.",
    "Externally replace the whole connector function at J2, including the Type-C sink controller.",
    "Never connect J2 VBUS directly to the stock VBUS lands or stack a USB-C cable on the module.",
    "This module modification requires a verified assembly process and D+/D- continuity inspection.",
))

s = Sheet("SPI microSD and I2C portrait OLED",
          "3V3_PER is switched separately from the module rail. Card and display signals go high-Z when their rail is absent.")
sheets.append(s)
p = s.cell(38, 126, 744, 327, "Local peripheral supply", ("RP2040 holds PER_EN low through attach/reset. This LDO is independent of the module LDO. Its input is on sheet 9.",))
p.part(361, 51, "IC14", "TLV75533PDBVR", pins((1, "IN", "PER_PRE"), (3, "EN", "PER_EN"), (2, "GND", "GND"), (4, "NC", None)),
       pins((5, "OUT", "3V3_PER")))
p.p(171, 217, "C141", "2.2uF, 16V", "PER_PRE", "GND")
p.p(553, 217, "C142", "4.7uF, 10V", "3V3_PER", "GND")
p.p(355, 264, "R141", "100k", "PER_EN", "GND")
p = s.cell(802, 126, 744, 327, "SPI1 buffer and default CS high", (
    "TXU0304 OE follows 3V3_PER. Both supplies must be present for I/O.",
))
p.part(370, 48, "IC15", "TXU0304PWR", pins(
    (1, "VCCA", "3V3_AON"), (2, "A1 SCK", "SD_SCK_A"), (3, "A2 MOSI", "SD_MOSI_A"), (4, "A3 CS", "SD_CS_A"),
    (5, "A4Y MISO", "SD_MISO_A"), (7, "GND", "GND"), ("6,9", "NC", None),
), pins((14, "VCCB", "3V3_PER"), (13, "B1Y SCK", "SD_SCK_B"), (12, "B2Y MOSI", "SD_MOSI_B"),
        (11, "B3Y CS", "SD_CS_B"), (10, "B4 MISO", "SD_MISO_B"), (8, "OE", "3V3_PER")))
p.p(159, 279, "R151", "47k", "SD_CS_A", "3V3_AON")
p.p(429, 279, "C151", "100nF", "3V3_AON", "GND")
p.p(620, 279, "C152", "100nF", "3V3_PER", "GND")
p = s.cell(38, 473, 744, 575, "J5   microSD socket, SPI mode", (
    "Card SPI pins only. Local peak allowance assumes a qualified card. No 5 V socket or external power.",
))
p.part(280, 48, "J5", "microSD socket", pins(
    (1, "DAT2", "SD_DAT2"), (2, "DAT3 / CS", "SD_CS_B"), (3, "CMD / DI", "SD_MOSI_B"),
    (4, "VDD", "3V3_PER"), (5, "CLK", "SD_CLK"), (6, "VSS", "GND"),
    (7, "DAT0 / DO", "SD_MISO_B"), (8, "DAT1", "SD_DAT1"), ("S", "Shell", "GND"),
    ("CD", "Optional card detect", None),
))
p.p(540, 126, "R152", "22R", "SD_SCK_B", "SD_CLK")
p.p(540, 181, "C153", "10uF, 10V", "3V3_PER", "GND")
p.p(540, 235, "C154", "100nF", "3V3_PER", "GND")
p.p(550, 279, "R158", "100k", "SD_SCK_A", "GND")
p.p(550, 312, "R159", "100k", "SD_MOSI_A", "GND")
for i, (x,y,net) in enumerate([(180,349,"SD_DAT2"),(550,349,"SD_DAT1"),(180,398,"SD_CS_B"),
                              (550,398,"SD_MISO_B"),(365,447,"SD_MOSI_B")], 3):
    p.p(x, y, f"R15{i}", "47k", net, "3V3_PER")
p = s.cell(802, 473, 744, 575, "J6   Monochrome OLED, 64 x 128 portrait", (
    "Use a 3.3 V SSD1306-class 128 x 64 panel in portrait rotation, I2C address 0x3C assumed.",
    "Check purchased OLED pin order, orientation, onboard pull-ups and peak current.",
))
p.part(405, 49, "IC16", "TCA9511ADGKR", pins(
    (6, "SDAIN", "OLED_SDA_A"), (3, "SCLIN", "OLED_SCL_A"), (4, "GND", "GND"), (5, "READY", None),
), pins((8, "VCC", "3V3_PER"), (7, "SDAOUT", "OLED_SDA"), (2, "SCLOUT", "OLED_SCL"), (1, "EN", "3V3_PER")))
p.part(365, 215, "J6", "OLED keyed 1x4", pins(
    (1, "GND", "GND"), (2, "3V3 out", "3V3_PER"), (3, "SCL", "OLED_SCL"), (4, "SDA", "OLED_SDA")))
for i,(x,y,net,rail) in enumerate([(165,378,"OLED_SDA_A","3V3_AON"),(510,378,"OLED_SCL_A","3V3_AON"),
                                  (165,428,"OLED_SDA","3V3_PER"),(510,428,"OLED_SCL","3V3_PER")],1):
    p.p(x,y,f"R16{i}","4.7k",net,rail)
p.p(165, 475, "C161", "100nF", "3V3_AON", "GND")
p.p(526, 475, "C162", "100nF", "3V3_PER", "GND")

s = Sheet("RGB data and 3.3 V UART",
          "External headers export signals and ground. Designated supply rails stay on the board. UART is CMOS, not RS-232.")
sheets.append(s)
p = s.cell(38, 126, 744, 587, "J7   3.3 V UART0, crossed peer wiring", (
    "TX to peer RX, RX to peer TX, and common ground. Require an Ioff-safe peer or keep both ends powered.",
    "Ioff buffers prevent a powered peer from sourcing the unpowered module through UART pins.",
    "No 5 V, RS-232, or supply pin.",
))
p.part(399, 53, "IC17", "SN74LVC2G125DCUR", pins(
    (2, "1A TX", "UART0_TX"), (5, "2A RX", "UART_RX_B"), (1, "1OE_N", "GND"),
    (7, "2OE_N", "GND"), (4, "GND", "GND"),
), pins((6, "1Y TX", "UART_TX_B"), (3, "2Y RX", "UART0_RX"), (8, "VCC", "3V3_AON")))
p.part(345, 228, "J7", "UART keyed 1x3", pins((1,"GND","GND"),(2,"TX out","UART_TX"),(3,"RX in","UART_RX")))
p.p(190, 370, "R171", "100R", "UART_TX_B", "UART_TX")
p.p(532, 370, "R172", "100R", "UART_RX", "UART_RX_B")
p.p(190, 420, "R173", "100k", "UART_RX_B", "GND")
p.p(532, 420, "C171", "100nF", "3V3_AON", "GND")
p.p(190, 470, "D171", "TPD1E05U06DPYR", "UART_TX", "GND", "d")
p.p(532, 470, "D172", "TPD1E05U06DPYR", "UART_RX", "GND", "d")
p = s.cell(802, 126, 744, 587, "J8   Addressable pixel protocol, data only", (
    "GP16 remains the onboard pixel. GP17 provides a separate external data output using the same protocol.",
    "A 5 V strip needs its own 5 V supply and a power-off-safe level shifter at the strip end. Connect ground.",
    "J8 is keyed data + ground. No strip power is drawn through this board or the USB chain.",
))
p.part(383, 53, "IC18", "SN74LVC1G125DBVR", pins(
    (2, "A", "RGB_A"), (1, "OE_N", "GND"), (3, "GND", "GND")
), pins((4, "Y", "RGB_B"), (5, "VCC", "3V3_AON")))
p.part(345, 208, "J8", "RGB keyed 1x2", pins((1,"GND","GND"),(2,"DATA out","RGB_DATA")))
p.p(195, 321, "R181", "100R", "RGB_B", "RGB_DATA")
p.p(525, 321, "R182", "100k", "RGB_A", "GND")
p.p(195, 373, "C181", "100nF", "3V3_AON", "GND")
p.p(525, 373, "D181", "TPD1E05U06DPYR", "RGB_DATA", "GND", "d")
p = s.cell(38, 733, 744, 277, "U3 / J3   USB-A power-only pinout",
           ("D+ and D- are open. Power comes exclusively from the A5 path on sheet 3.",))
usb_a(p, "J3", "U3_VBUS")
p = s.cell(802, 733, 744, 277, "U4 / J4   USB-A host pinout",
           ("VBUS path on sheet 4. Data and host termination on sheet 5.",))
usb_a(p, "J4", "U4_VBUS", True)

s = Sheet("Independent peripheral power grants",
          "The always-on mux may fall back to U2. The optional 3.3 V regulator has separately authorized inputs.")
sheets.append(s)
for x, ref, gate_ref, vin, vout, grant, title in [
    (38,23,230,"A5","A_PER","PER_A_GRANT","U1 peripheral feed"),
    (802,24,240,"B5","B_PER","PER_B_GRANT","U2 peripheral feed"),
]:
    p = s.cell(x,126,744,349,title,(
        "Reverse-blocking, slew-controlled branch. Set this grant only within the corresponding source budget.",
    ))
    fuse(p,ref,vin,vout,f"EN{ref}","6.65k, 1%","3.3nF, 16V",False)
    p.p(410,289,f"C{ref}2","1uF, 16V",vout,"GND")
    p = s.cell(x,495,744,320,f"{title} enable",(
        "Default is off. The gate's enable current comes only from its own source.",
    ))
    gate(p,gate_ref,vin,grant,f"EN{ref}")

p = s.cell(38,835,1508,341,"Peripheral source selection",(
    "Only A_PER and B_PER may feed IC14. The board losing A5 cannot turn a live SD or OLED load onto U2 without a prior U2 grant.",
    "PER_EN separately enables IC14 after its source has settled. A port grant does not implicitly authorize the peripheral branch.",
))
mux(p,25,"A_PER","B_PER","PER_PRE")
p.p(1130,145,"C251","1uF, 16V","PER_PRE","GND")
