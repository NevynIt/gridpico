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

def fuse(panel, ref, vin, vout, en, ilm, dvdt, ovlo=True, dvdt_cap_net=None):
    panel.part(420, 48, f"IC{ref}", "TPS259474LRPWR", pins(
        (5, "IN", vin), (1, "EN/UVLO", en), (2, "OVLO", f"OV{ref}" if ovlo else "GND"),
        (8, "GND", "GND"),
    ), pins((6, "OUT", vout), (7, "DVDT", f"DV{ref}"), (9, "ILM", f"IL{ref}"),
            (3, "PG", None), (4, "PGTH", "GND"), (10, "ITIMER", None)))
    panel.p(240, 257, f"R{ref}1", ilm, f"IL{ref}", "GND")
    panel.p(560, 257, f"C{ref}1", dvdt, dvdt_cap_net or f"DV{ref}", "GND")
    if ovlo:
        panel.p(240, 303, f"R{ref}2", "34.8k, 0.1%", vin, f"OV{ref}")
        panel.p(560, 303, f"R{ref}3", "10.0k, 0.1%", f"OV{ref}", "GND")

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

s = Sheet("Dedicated U1 power and protected U3",
          "U1 and U2 have separate protected rails. A power-off assembly selector sets module supply. U3 remains U1-only.")
sheets.append(s)
p = s.cell(38, 126, 744, 435, "J1 / U1   Dedicated 5 V input", (
    "This four-signal interface has power and ground. The data positions are open.",
    "Known 5.10 to 5.25 V under load, 1.5 A rated adapter and retained or keyed cable. See connector conditions.",
))
p.part(345, 61, "J1", "Dedicated 4-signal U1 power interface", pins(
    (1, "5 V input", "U1_RAW"), (2, "D- unused", None), (3, "D+ unused", None), (4, "GND", "GND")))
p.p(260, 269, "C10", "1uF, 16V", "U1_RAW", "GND")
p.p(523, 269, "D10", "SMF6.0A", "U1_RAW", "GND", "d")
p = s.cell(802, 126, 744, 435, "IC1   Input cutoff and reverse blocking", (
    "The input breaker and upstream adapter cover board plus both outlets.",
    "Qualify cutoff, current, startup and input voltage with the selected adapter.",
))
fuse(p, 1, "U1_RAW", "A5", "EN1", "2.67k, 1%", "3.3nF, 5%")
p.p(240, 342, "R14", "24.9k, 1%", "U1_RAW", "EN1")
p.p(560, 342, "R15", "10.0k, 1%", "EN1", "GND")
p.p(240, 371, "C12", "4.7uF, 10V", "A5", "GND")
p = s.cell(38, 581, 744, 410, "IC4   Independently protected U3 branch", (
    "U3 comes up with valid A5. 300 mA accessory budget. Use current-limited cutoff and reverse blocking.",
))
fuse(p, 4, "A5", "U3_VBUS", "EN4", "6.57k, 0.1%", "3.3nF, 5%", ovlo=False)
p.p(240, 309, "R44", "24.9k, 1%", "A5", "EN4")
p.p(560, 309, "R45", "10.0k, 1%", "EN4", "GND")
p = s.cell(802, 581, 744, 410, "J3 / U3   USB-A power only", (
    "D+/D- are open. No USB charging signature. Confirm load and output behavior.",
))
usb_a(p, "J3", "U3_VBUS")
p.p(245, 267, "C42", "10uF, 16V", "U3_VBUS", "GND")
p.p(550, 267, "R46", "4.7k", "U3_VBUS", "GND")

p = s.cell(38, 1011, 744, 444, "IC2   U2 isolated input to B5", (
    "The B5 input breaker protects the source. It grants no USB current budget.",
    "An absent or unconfigured laptop cannot enable discretionary U4 load.",
))
fuse(p, 2, "U2_RAW", "B5", "EN2", "6.57k, 0.1%", "2.2nF, 5%")
p.p(240, 342, "R24", "24.9k, 1%", "U2_RAW", "EN2")
p.p(560, 342, "R25", "10.0k, 1%", "EN2", "GND")
p.p(240, 380, "C22", "2.2uF, 16V", "B5", "GND")
p.p(560, 380, "C20", "1uF, 16V", "U2_RAW", "GND")
p = s.cell(802, 1011, 744, 444, "SJ1   One module power link set with power off", (
    "Default fit 1-2 from U1. Alternative fit 2-3 from U2 only after USB draw and suspend qualification.",
    "Populate only one bridge. No automatic local failover. U4 has its own selectable source.",
))
p.part(361, 52, "SJ1", "3-pad exclusive assembly selector", pins(
    (1, "U1 position", "A5"), (2, "Module common", "SYS5"), (3, "U2 position", "B5")))
p.p(536, 215, "C31", "4.7uF, 10V", "SYS5", "GND")


s = Sheet("U4 PIO host power and cutoff",
          "U4 draws from A5 or a separately granted current-limited B5 branch. GP9 independently starts and vetoes the port.")
sheets.append(s)
p = s.cell(38, 126, 744, 420, "IC6   U4 downstream power protection", (
    "Current breaker, reverse blocking and controlled slew. Initial target is a 500 mA configured host port.",
    "Place series R67/C61 close to IC6. Validate the fitted ramp and actual inrush before use.",
))
fuse(p, 6, "HOST_PRE", "U4_VBUS", "EN6", "5.36k, 1%", "39nF, 5%, C0G",
     ovlo=False, dvdt_cap_net="DV6_C")
p.p(362, 309, "R67", "100R, 1%", "DV6", "DV6_C")
p = s.cell(802, 126, 744, 420, "J4 / U4   USB-A PIO host outlet", (
    "C62 is a 170 to 400uF effective-capacitance design envelope. Select and measure the actual capacitor.",
    "The passive discharge estimate alone supplies no attach or reconnect timing.",
))
usb_a(p, "J4", "U4_VBUS", True)
p.p(200, 267, "C62", "270uF polymer candidate", "U4_VBUS", "GND")
p.p(530, 267, "R66", "4.70k, 1%", "U4_VBUS", "GND")
p.p(530, 310, "C63", "1uF, 16V", "U4_VBUS", "GND")
p = s.cell(38, 566, 744, 351, "GP9 fail-off, sourced from selected HOST_PRE", (
    "Assert GP9 after valid HOST_PRE. Dropping it removes the IC6 enable and opens U4 data.",
    "A fault requires bounded, deliberate recovery and a fresh downstream attachment.",
))
gate(p, 60, "HOST_PRE", "U4_PORT_EN", "EN6")
p = s.cell(802, 566, 744, 351, "Host power limits", (
    "Software admits the configured load within the measured U4 port and source budget.",
    "The hardware breaker protects faults. Measure preconfiguration load, hot plug, current,",
    "drop/droop, forced-output backfeed and discharge before powered use.",
))

p = s.cell(38, 937, 744, 432, "IC7   U2 host branch, fail-off grant and limit", (
    "GP8 authorizes this branch only after a valid U2 aggregate budget.",
    "R75 screens 221 to 292 mA cutoff corners. 200 mA is the proposed settled B-port ceiling.",
))
p.part(385, 50, "IC7", "TPS2553DBVR-1", pins(
    (1, "IN", "B5"), (2, "GND", "GND"), (3, "EN", "EN7")),
    pins((4, "FAULT_N", "U4_B_FAULT_N"), (5, "ILIM", "IL7"), (6, "OUT", "B_HOST")))
p.p(202, 211, "R75", "105k, 1%", "IL7", "GND")
p.p(546, 211, "C71", "2.2uF, 16V", "B_HOST", "GND")
p.p(202, 265, "R76", "47k", "3V3_AON", "U4_B_FAULT_N")
p.p(546, 265, "C72", "100nF, 16V", "B5", "GND")
p = s.cell(802, 937, 744, 327, "B branch source-local enable", (
    "The GP8 pull-down denies this branch while the module is off or in reset.",
))
gate(p, 70, "B5", "U4_B_GRANT", "EN7", "10.0k")
p = s.cell(38, 1389, 744, 341, "IC8   U4 manual-mode source selector", (
    "With AON valid, GP8 low selects A5; GP8 high selects current-limited B_HOST.",
    "Drop GP9 and retire the old attach before changing source. Measure unpowered and brownout states.",
))
p.part(386, 46, "IC8", "TPS2116DRLR", pins(
    (3, "VIN1", "B_HOST"), (4, "PR1", "U4_B_GRANT"), (5, "MODE", "3V3_AON"),
    (6, "VIN2", "A5"), (1, "GND", "GND")),
    pins(("2,7", "VOUT", "HOST_PRE"), (8, "ST", None)))
p.p(190, 252, "C81", "2.2uF, 16V", "HOST_PRE", "GND")


s = Sheet("Native and PIO USB data",
          "Actual connector VBUS qualifies each independent full-speed data pair. There is no electrical U2-to-U4 data path.")
sheets.append(s)
p = s.cell(38, 126, 744, 468, "U2 upstream   Full-speed device data", (
    "J2 is a device connector. A USB-C version requires verified passive Rd and both plug orientations.",
    "J2 VBUS goes only to IC2's protected B path and separate sense. Keep M1's native series resistors.",
))
p.part(201, 53, "J2", "Upstream 4-signal device interface", pins(
    (1, "VBUS input", "U2_RAW"), (2, "D-", "C2_DM"), (3, "D+", "C2_DP"), (4, "GND", "GND"),
    ("S", "Shell", "GND")))
usb_mux(p, "IC10", "C2_DP", "C2_DM", "N_DP", "N_DM", "NUSB_OE_N", x=410, y=218)
p.p(180, 358, "C101", "100nF", "3V3_AON", "GND")
p.part(535, 55, "D101", "TPD2E2U06QDBZRQ1", pins(
    (1, "I/O1", "C2_DM"), (2, "I/O2", "C2_DP"), (3, "GND", "GND")))
p = s.cell(802, 126, 744, 468, "U4 downstream   PIO full-speed host data", (
    "GP14 D+ and GP15 D-. Use one qualified 15k pulldown per J4 signal. Check stock weak pulls and FS levels.",
))
usb_mux(p, "IC12", "H_DP", "H_DM", "HP_DP", "HP_DM", "HUSB_OE_N", x=388, y=51)
p.part(366, 238, "D121", "TPD2E2U06QDBZRQ1", pins(
    (1, "I/O1", "H_DM"), (2, "I/O2", "H_DP"), (3, "GND", "GND")))
p.p(171, 245, "R121", "22R", "PIO_DP", "HP_DP")
p.p(564, 245, "R122", "22R", "PIO_DM", "HP_DM")
p.p(171, 387, "R123", "15k, 1%", "H_DP", "GND")
p.p(560, 387, "R124", "15k, 1%", "H_DM", "GND")
p.p(560, 337, "C121", "100nF", "3V3_AON", "GND")

for x, ref, raw, sns, dwell, good, mr, resistance, timer, caption in (
    (38, 11, "U2_RAW", "U2_SNS", "U2_DWELL", "N_VBUS_GOOD", "3V3_AON",
     "26.8k, 0.1%", "4.7nF, 5%, C0G", "GP18 low reads the U2 data-switch command."),
    (802, 13, "U4_VBUS", "U4_SNS", "U4_DWELL", "H_VBUS_GOOD", "U4_PORT_EN",
     "29.1k, 0.1%", "68nF, 5%, C0G", "GP20 low reads the U4 data-switch command. GP9 low always vetoes it."),
):
    p = s.cell(x, 614, 744, 337, f"IC{ref}   {raw} separate threshold and dwell", (caption,))
    p.part(365, 57, f"IC{ref}", "TPS389001DSER", pins(
        (1, "SENSE", sns), (2, "GND", "GND"), (3, "MR_N", mr),
    ), pins((4, "VDD", "3V3_AON"), (5, "CT", dwell), (6, "RESET_N", good)))
    p.p(172, 187, f"R{ref}2", resistance, raw, sns)
    p.p(560, 187, f"R{ref}3", "10.0k, 0.1%", sns, "GND")
    p.p(172, 234, f"R{ref}1", "47k, 1%", "3V3_AON", good)
    p.p(560, 234, f"C{ref}1", "100nF", "3V3_AON", "GND")
    p.p(172, 277, f"C{ref}2", timer, dwell, "GND")

p = s.cell(38, 971, 1508, 269, "IC26   Active-low Switch Enable", (
    "U2 opens without valid J2 VBUS. U4 opens if VBUS falls invalid or GP9 drops. Both switches have power-off protection.",
))
p.part(752, 52, "IC26", "74AUP2G132DC", pins(
    (1, "1A", "N_VBUS_GOOD"), (2, "1B", "N_VBUS_GOOD"), (5, "2A", "H_VBUS_GOOD"),
    (6, "2B", "U4_PORT_EN"), (4, "GND", "GND"),
), pins((7, "1Y", "NUSB_OE_N"), (3, "2Y", "HUSB_OE_N"), (8, "VCC", "3V3_AON")))
p.p(225, 100, "R101", "47k", "3V3_AON", "NUSB_OE_N")
p.p(1225, 100, "R125", "47k", "3V3_AON", "HUSB_OE_N")
p.p(1225, 159, "C261", "100nF", "3V3_AON", "GND")

s = Sheet("RP2040-Zero and conditional 3.3 V accessories",
          "One module rail supplies qualified SD and OLED loads. Short local 3.3 V accessory wiring only.")
sheets.append(s)
p = s.cell(38, 126, 1031, 710, "M1   RP2040-Zero module, original receptacle absent", (
    "The former VBUS lands still carry SYS5. Keep them inaccessible and insulated. J2 VBUS stays separate.",
    "Only the native D+ and D- lands leave the module's connector footprint. Keep BOOTSEL and RESET accessible.",
))
p.part(510, 56, "M1", "Waveshare RP2040-Zero, exact revision pending", pins(
    ("P1.1", "GP0 UART0 TX", "UART_TX_A"), ("P1.2", "GP1 UART0 RX", "UART_RX_A"),
    ("P1.3", "GP2", None), ("P1.4", "GP3", None),
    ("P1.5", "GP4 I2C0 SDA", "OLED_SDA"), ("P1.6", "GP5 I2C0 SCL", "OLED_SCL"),
    ("P1.7", "GP6", None), ("P1.8", "GP7", None), ("P1.9", "GP8 B host grant", "U4_B_GRANT"),
    ("P1.10", "GP9 SIO", "U4_PORT_EN"), ("P1.11", "GP10 SPI1 SCK", "SD_CLK_A"),
    ("P1.12", "GP11 SPI1 TX", "SD_MOSI"), ("P1.13", "GP12 SPI1 RX", "SD_MISO"),
    ("P1.14", "GP13 SPI1 CS", "SD_CS"), ("P1.15", "GP14 PIO D+", "PIO_DP"),
    ("P1.16", "GP15 PIO D-", "PIO_DM"), ("P1.17", "GP26", None), ("P1.18", "GP27", None),
    ("P1.19", "GP28", None), ("P1.20", "GP29", None), ("P1.21", "3V3 output", "3V3_AON"),
    ("P1.22", "GND", "GND"), ("P1.23", "5 V VSYS", "SYS5"),
), pins(
    ("P2.1", "GP17 external RGB", "RGB_A"), ("P2.2", "GP18 U2 switch command", "NUSB_OE_N"),
    ("P2.3", "GP19", None), ("P2.4", "GP20 U4 switch command", "HUSB_OE_N"), ("P2.5", "GP21 B branch fault", "U4_B_FAULT_N"),
    ("P3.1", "GP22", None), ("P3.2", "GP23", None), ("P3.3", "GP24", None),
    ("P3.4", "GP25", None), ("P3.5", "GND", "GND"),
    ("T-C.A6,B6", "Former D+ lands", "N_DP"), ("T-C.A7,B7", "Former D- lands", "N_DM"),
    ("T-C.VBUS", "Insulated former VBUS lands", "SYS5"),
    ("T-C.CC1,CC2", "No external connection", None), ("INT", "GP16 onboard pixel", None),
))
p = s.cell(1089, 126, 457, 710, "Shared 3.3 V loading", (
    "The module regulator rating alone does not establish external current.",
    "Fit J5 and J6 assemblies only after the actual module, card and OLED pass the combined thermal and transient budget.",
    "Direct GPIOs are for local 3.3 V accessories. Installation and removal are power off.",
    "DNP an unqualified accessory as a complete interface. An unpowered accessory must not be driven through its GPIOs.",
))
p = s.cell(38, 856, 744, 480, "J5   SPI microSD, conditional assembly", (
    "Qualified bare 3.3 V socket or equivalent. DAT1/DAT2 pull high. Use at most the qualified card current.",
))
p.part(276, 48, "J5", "3.3 V microSD socket", pins(
    (1, "DAT2", "SD_DAT2"), (2, "DAT3 / CS", "SD_CS"), (3, "CMD / DI", "SD_MOSI"),
    (4, "VDD", "3V3_AON"), (5, "CLK", "SD_CLK"), (6, "GND", "GND"), (7, "DAT0 / DO", "SD_MISO"),
    (8, "DAT1", "SD_DAT1"), ("S", "Shell", "GND"), ("CD", "Card detect", None)))
for index, (x, y, net) in enumerate((
    (557, 71, "SD_DAT2"), (557, 121, "SD_DAT1"), (557, 171, "SD_CS"),
    (557, 221, "SD_MOSI"), (557, 271, "SD_MISO"),
), start=1):
    p.p(x, y, f"R5{index}", "47k", net, "3V3_AON")
p.p(545, 327, "R56", "22R", "SD_CLK_A", "SD_CLK")
p.p(230, 356, "C51", "10uF, 10V", "3V3_AON", "GND")
p.p(546, 385, "C52", "100nF", "3V3_AON", "GND")
p = s.cell(802, 856, 744, 480, "J6 OLED and J7/J8 local headers", (
    "Check J6 connector polarity and existing pull-ups. GP16 is the built-in pixel.",
    "J7 and J8 are 3.3 V data and common ground only. Power off the peers for connection. No 5 V signaling.",
))
p.part(201, 54, "J6", "3.3 V I2C OLED", pins(
    (1, "GND", "GND"), (2, "3V3", "3V3_AON"), (3, "SCL", "OLED_SCL"), (4, "SDA", "OLED_SDA")))
p.part(550, 54, "J7", "3.3 V UART header", pins(
    (1, "GND", "GND"), (2, "TX", "UART_TX"), (3, "RX", "UART_RX")))
p.part(200, 221, "J8", "3.3 V RGB data header", pins(
    (1, "GND", "GND"), (2, "DATA", "RGB_DATA")))
p.p(192, 204, "R64", "4.7k if needed", "3V3_AON", "OLED_SDA")
p.p(543, 204, "R65", "4.7k if needed", "3V3_AON", "OLED_SCL")
p.p(533, 245, "R71", "100R", "UART_TX_A", "UART_TX")
p.p(533, 293, "R72", "100R", "UART_RX", "UART_RX_A")
p.p(533, 341, "R81", "100R", "RGB_A", "RGB_DATA")
