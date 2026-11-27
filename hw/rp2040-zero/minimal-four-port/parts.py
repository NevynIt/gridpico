"""Drawing annotations backed by the Minimal four-port paper topology and inherited catalog records."""

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

DOCUMENTS = {
    "IC1": breaker, "IC2": breaker, "IC4": breaker, "IC6": breaker,
    "IC9": ti("TPS2116DRLR", "tps2116"),
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
    "IC9": "Selects Pico power",
}
