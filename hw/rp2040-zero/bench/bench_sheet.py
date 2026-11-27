"""Render the isolated two-port paper design with the published schematic renderer."""

import csv
from pathlib import Path
import sys

import pymupdf as fitz

SOURCE = Path("/workspace/gridpico/hw/rp2040-zero/minimal")
sys.path.insert(0, str(SOURCE))
from render import BLUE, FACE, FAINT, GREY, INK, PAPER, POWER, SUB, WIRE, Canvas

DEST = Path(__file__).resolve().parent
H = 912
PINOUT = {
    "P1.5": "GP4 I2C0 SDA",
    "P1.6": "GP5 I2C0 SCL",
    "P1.11": "GP10 SPI1 SCK",
    "P1.12": "GP11 SPI1 TX",
    "P1.13": "GP12 SPI1 RX",
    "P1.14": "GP13 SPI1 CS",
    "P1.15": "GP14 PIO D+",
    "P1.16": "GP15 PIO D-",
    "P1.21": "3V3 output",
    "P1.22": "GND",
    "P1.23": "5 V VSYS",
}
NETS = {
    "U2_VBUS": ("PC.VBUS", "M1.U2.VBUS", "M1.P1.23 (internal connection only)"),
    "U2_DP": ("PC.D+", "M1.U2.A6/B6"),
    "U2_DM": ("PC.D-", "M1.U2.A7/B7"),
    "GND": ("PC.GND", "M1.U2.GND", "M1.P1.22", "J4.GND", "Hub.upstream.GND",
            "J5.GND [card 6]", "J6.GND"),
    "PIO_DP": ("M1.P1.15", "R1.1"),
    "U4_DP": ("R1.2", "J4.D+", "Hub.upstream.D+"),
    "PIO_DM": ("M1.P1.16", "R2.1"),
    "U4_DM": ("R2.2", "J4.D-", "Hub.upstream.D-"),
    "U4_VBUS_ISLAND": ("J4.VBUS (pad open on carrier)", "Hub.upstream.VBUS (cable side)"),
    "SD_CLK": ("M1.P1.11", "J5.CLK [card 5]"),
    "SD_CMD": ("M1.P1.12", "J5.DI [card 3]"),
    "SD_DAT0": ("M1.P1.13", "J5.DO [card 7]"),
    "SD_CS": ("M1.P1.14", "J5.CS [card 2]"),
    "OLED_SDA": ("M1.P1.5", "J6.SDA"),
    "OLED_SCL": ("M1.P1.6", "J6.SCL"),
    "MODULE_3V3": ("M1.P1.21", "J5.VDD [card 4]", "J6.VDD"),
}


def m1_pin(sheet, y, label, header):
    sheet.text(568, y + 5, label, 15, INK, False, "end")
    sheet.text(644, y + 5, header, 14, SUB, False, "end", True)


def pin(sheet, x, y, label, left=True, ink=INK):
    sheet.text(x + (14 if left else -14), y + 5, label, 15,
               ink, False, "start" if left else "end")


def box(sheet, x, y, w, h, title, subtitle=None):
    sheet.rect(x, y, w, h, INK, FACE, 1.5)
    band = 58 if subtitle else 39
    sheet.rect(x, y, w, band, INK, BLUE, 1.2)
    sheet.text(x + 15, y + 25, title, 17, INK, True)
    if subtitle:
        sheet.text(x + 15, y + 46, subtitle, 13, SUB)


def ground(sheet, x, y):
    sheet.line(x, y, x, y + 15)
    for offset, half_width in ((15, 8), (19, 5), (23, 2)):
        sheet.line(x - half_width, y + offset, x + half_width, y + offset)


def rail(sheet, x, y):
    sheet.line(x, y, x, y - 17, POWER)
    sheet.line(x - 8, y - 17, x + 8, y - 17, POWER)
    sheet.text(x, y - 24, "3V3", 12, POWER, True, "middle")


def series(sheet, x1, x2, y, ref, x, label_y):
    sheet.line(x1, y, x - 25, y)
    sheet.rect(x - 25, y - 7, 50, 14, INK, PAPER, 1.5)
    sheet.line(x + 25, y, x2, y)
    sheet.text(x, label_y, ref + "  22 ohms", 14, INK, True, "middle")


def open_pad(sheet, left, right, y):
    sheet.line(left, y, right, y, POWER)
    sheet.line(right - 5, y - 5, right + 5, y + 5, POWER, 1.5)
    sheet.line(right - 5, y + 5, right + 5, y - 5, POWER, 1.5)


def draw(sheet):
    sheet.rect(0, 0, 1584, 91, INK, INK)
    sheet.text(38, 30, "GRIDPICO / BENCH VARIANT", 15, "#aed3d7", True)
    sheet.text(38, 69, "RP2040-Zero two-port bench schematic", 35, PAPER, True)
    sheet.text(1546, 69, "PAPER REVIEW", 15, "#aed3d7", True, "end")
    sheet.text(38, 118, "Native USB to PC  /  PIO USB to a separately powered hub  /  SPI microSD  /  I²C OLED",
               17, SUB)

    box(sheet, 38, 198, 218, 192, "PC / laptop", "Native USB cable")
    box(sheet, 375, 172, 286, 524, "M1  Waveshare RP2040-Zero", "Stock module and native USB-C retained")
    sheet.text(390, 252, "U2  NATIVE USB-C", 14, SUB, True)
    for y, (label, native) in zip((278, 307, 336, 365), (
        ("5 V VBUS", "VBUS"),
        ("D+", "A6/B6  D+"),
        ("D-", "A7/B7  D-"),
        ("GND", "GND"),
    ), strict=True):
        ink = POWER if y == 278 else WIRE
        pin(sheet, 256, y, label, False)
        pin(sheet, 375, y, native)
        sheet.line(256, y, 375, y, ink)

    sheet.text(390, 390, "PC is the only power source for M1.", 12, SUB)

    box(sheet, 861, 172, 200, 190, "J4  U4 breakout", "Bench USB-C, verify CC")
    box(sheet, 1276, 172, 270, 190, "Self-powered USB hub", "Use its matched upstream cable")
    box(sheet, 1276, 377, 270, 39, "Hub DC adapter only")
    sheet.line(1411, 377, 1411, 362, POWER)
    for y, header, gpio, usb in (
        (258, "P1.15", "GP14", "D+"),
        (287, "P1.16", "GP15", "D-"),
        (316, "P1.22", "GND", "GND"),
        (345, None, None, "VBUS"),
    ):
        if header:
            m1_pin(sheet, y, gpio, header)
        pin(sheet, 861, y, usb, ink=POWER if y == 345 else INK)
        pin(sheet, 1276, y, usb, ink=POWER if y == 345 else INK)
        sheet.line(1061, y, 1276, y, POWER if y == 345 else WIRE)
    series(sheet, 661, 861, 258, "R1", 761, 246)
    series(sheet, 661, 861, 287, "R2", 761, 307)
    sheet.line(661, 316, 861, 316)
    ground(sheet, 702, 316)
    sheet.line(700, 316, 704, 316, WIRE, 4)
    open_pad(sheet, 861, 829, 345)
    sheet.text(819, 350, "OPEN", 13, POWER, True, "end")
    sheet.text(1168, 246, "UPSTREAM CABLE", 12, SUB, True, "middle")

    box(sheet, 861, 374, 228, 202, "J5  3.3 V microSD", "SPI breakout • card contacts shown")
    for y, gpio, header, function in (
        (449, "GP10  SPI1 SCK", "P1.11", "5  CLK"),
        (473, "GP11  SPI1 TX", "P1.12", "3  CMD / DI"),
        (497, "GP12  SPI1 RX", "P1.13", "7  DAT0 / DO"),
        (521, "GP13  SPI1 CS", "P1.14", "2  DAT3 / CS"),
    ):
        m1_pin(sheet, y, gpio, header)
        pin(sheet, 861, y, function)
        sheet.line(661, y, 861, y)
    pin(sheet, 1089, 545, "4  VDD", False)
    pin(sheet, 1089, 565, "6  VSS", False)
    sheet.line(1089, 545, 1133, 545, POWER)
    rail(sheet, 1133, 545)
    sheet.line(1089, 565, 1111, 565)
    ground(sheet, 1111, 565)

    box(sheet, 861, 598, 228, 98, "J6  3.3 V I²C OLED")
    for y, gpio, header, function in (
        (654, "GP4  I²C0 SDA", "P1.5", "SDA"),
        (680, "GP5  I²C0 SCL", "P1.6", "SCL"),
    ):
        m1_pin(sheet, y, gpio, header)
        pin(sheet, 861, y, function)
        sheet.line(661, y, 861, y)
    pin(sheet, 1089, 654, "VDD", False)
    pin(sheet, 1089, 680, "GND", False)
    sheet.line(1089, 654, 1133, 654, POWER)
    rail(sheet, 1133, 654)
    sheet.line(1089, 680, 1111, 680)
    ground(sheet, 1111, 680)

    pin(sheet, 375, 545, "P1.21   3V3 out", ink=POWER)
    sheet.line(301, 545, 375, 545, POWER)
    rail(sheet, 301, 545)
    pin(sheet, 375, 567, "P1.23   5 V pad")
    open_pad(sheet, 375, 344, 567)
    sheet.text(334, 572, "UNUSED", 13, POWER, True, "end")
    sheet.text(390, 591, "P1.23 is tied to U2 VBUS on M1.", 12, SUB)

    sheet.rect(1187, 444, 359, 252, FAINT, FACE, 1.3)
    sheet.text(1203, 472, "Fit gates", 17, INK, True)
    notes = (
        ("Hub / cable / CC", ("Use the recorded data-only hookup if the exact",
                              "combo attaches with U4 VBUS open. Hold fit if",
                              "it needs VBUS or drives an unpowered Pico.")),
        ("SD / OLED", ("Verify pad order and 3.3 V power and logic.",
                       "Require SD pulls and bypass, plus OLED I²C",
                       "pull-ups to the same module 3V3 rail.")),
    )
    for y, (title, lines) in zip((501, 598), notes, strict=True):
        sheet.text(1203, y, title, 14, POWER, True)
        for index, line in enumerate(lines, 1):
            sheet.text(1203, y + index * 20, line, 14, INK)

    sheet.rect(38, 725, 1508, 140, FAINT, FACE, 1.3)
    sheet.text(54, 752, "Build and check after hardware authorization", 17, INK, True)
    checks = (
        "Match module, hub, cable, CC and breakout revisions.",
        "Verify U4 VBUS is open and P1.23 is unused.",
        "With PC unplugged, check hub upstream VBUS and",
        "data and confirm no rise on the Pico rails.",
        "Check native and downstream attach in both power",
        "sequences. Verify full-speed PIO with short leads.",
        "Check combined USB current at attach, SD write and",
        "suspend. Verify regulator margin with both modules.",
    )
    for index in range(4):
        x = 54 + 377 * index
        sheet.text(x, 780, f"{index + 1}", 15, POWER, True)
        sheet.text(x + 22, 780, checks[2 * index], 13.0)
        sheet.text(x + 22, 802, checks[2 * index + 1], 13.0)
    sheet.text(54, 843, "Added discrete parts  •  R1 and R2, 22 ohms each     /     Matching 3V3 and ground symbols denote shared module rails.",
               13, SUB)
    sheet.text(38, 894, "Open X marks an intentionally unconnected pad. Ground is common to PC, Pico and hub.",
               12, SUB)
    sheet.text(1546, 894, "TWO-PORT BENCH VARIANT  /  1 OF 1", 12, SUB, True, "end")


def verify_and_write_pin_map():
    with (SOURCE / "pinout.csv").open(newline="", encoding="utf-8") as source:
        pinout = {row["pin"]: row["function"] for row in csv.DictReader(source)
                  if row["reference"] == "M1"}
    assert {pin: pinout[pin] for pin in PINOUT} == PINOUT
    assert NETS["U4_VBUS_ISLAND"] == (
        "J4.VBUS (pad open on carrier)", "Hub.upstream.VBUS (cable side)")
    assert set(NETS["U4_VBUS_ISLAND"]).isdisjoint(NETS["U2_VBUS"])
    all_endpoints = [pin for pins in NETS.values() for pin in pins]
    assert len(all_endpoints) == len(set(all_endpoints))
    with (DEST / "pin-map.csv").open("w", newline="", encoding="utf-8") as destination:
        writer = csv.writer(destination)
        writer.writerow(("net", "endpoint"))
        for net, endpoints in NETS.items():
            writer.writerows((net, endpoint) for endpoint in endpoints)


if __name__ == "__main__":
    verify_and_write_pin_map()
    doc = fitz.open()
    sheet = Canvas(doc, H)
    draw(sheet)
    sheet.save(DEST / "bench-variant.svg")
    doc.save(DEST / "bench-variant.pdf", garbage=4, deflate=True)
    doc[0].get_pixmap(matrix=fitz.Matrix(1.45, 1.45), alpha=False).save(DEST / "preview.png")
