"""Drawing annotations backed by the published minimum-carrier part records."""

from dataclasses import dataclass


@dataclass(frozen=True)
class Document:
    part: str
    maker: str
    url: str
    status: str = "recorded_order_code"


def ti(part, document):
    return Document(part, "Texas Instruments", f"https://www.ti.com/lit/ds/symlink/{document}.pdf")


breaker = ti("TPS259474LRPWR", "tps25947")
usb_switch = ti("TMUXHS221FRSWR", "tmuxhs221f")
voltage_check = ti("TPS389001DSER", "tps3890")
esd = ti("TPD2E2U06QDBZRQ1", "tpd2e2u06-q1")

DOCUMENTS = {
    "IC1": breaker, "IC2": breaker, "IC4": breaker, "IC6": breaker,
    "IC7": ti("TPS2553DBVR-1", "tps2553"),
    "IC8": ti("TPS2116DRLR", "tps2116"),
    "IC10": usb_switch, "IC12": usb_switch,
    "IC11": voltage_check, "IC13": voltage_check,
    "IC26": Document("74AUP2G132DC", "Nexperia", "https://assets.nexperia.com/documents/data-sheet/74AUP2G132.pdf"),
    "D101": esd, "D121": esd,
    "C61": Document("GRM2195C2A393JE01", "Murata",
                    "https://pim.murata.com/asset/pim4/ceramicCapacitorSMD/GRM2195C2A393JE01-01A-EN_PDF_CERAMICCAPACITORSMD",
                    "catalog_basis"),
    "C62": Document("16SVPF270M", "Panasonic",
                    "https://industrial.panasonic.com/cdbs/www-data/pdf/AAB8000/AAB8000C177.pdf",
                    "conditional_candidate"),
    "M1": Document("RP2040-Zero", "Waveshare", "https://files.waveshare.com/upload/4/4c/RP2040_Zero.pdf",
                   "model_schematic_revision_open"),
}

IC_ROLES = {
    "IC1": "Protects U1 input",
    "IC2": "Protects U2 input",
    "IC4": "Protects U3 output",
    "IC6": "Protects U4 output",
    "IC7": "Limits U2 branch current",
    "IC8": "Selects U4 power",
    "IC10": "Connects U2 data",
    "IC11": "Checks U2 VBUS",
    "IC12": "Connects U4 data",
    "IC13": "Checks U4 VBUS",
    "IC26": "Enables USB data switches",
}
