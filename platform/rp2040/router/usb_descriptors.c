#include "tusb.h"

#ifdef GRIDPICO_RP2040_FLASH_ROUTER
#include "flash_startup.h"
#include "hardware/flash.h"

enum { FLASH_SERIAL_INDEX = 5, FLASH_SERIAL_LENGTH = 2 * FLASH_UNIQUE_ID_SIZE_BYTES };
static uint16_t serial_descriptor[FLASH_SERIAL_LENGTH + 1];

bool gridpico_flash_init_usb_serial(void)
{
    uint8_t id[FLASH_UNIQUE_ID_SIZE_BYTES];
    static const char hex[] = "0123456789ABCDEF";
    flash_get_unique_id(id);
    uint8_t any_set = 0, all_set = UINT8_MAX;
    for (size_t i = 0; i < sizeof(id); ++i) {
        any_set |= id[i];
        all_set &= id[i];
        serial_descriptor[2 * i + 1] = (uint16_t)hex[id[i] >> 4];
        serial_descriptor[2 * i + 2] = (uint16_t)hex[id[i] & 15u];
    }
    serial_descriptor[0] = (TUSB_DESC_STRING << 8) | (2 * (FLASH_SERIAL_LENGTH + 1));
    return any_set != 0 && all_set != UINT8_MAX;
}
#endif

enum {
    CABLES = 2,
    CONFIG_SIZE = TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MIDI_DESC_HEAD_LEN +
                  CABLES * TUD_MIDI_DESC_JACK_LEN + 2 * TUD_MIDI_DESC_EP_LEN(CABLES),
};

static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x1209,
    .idProduct = 0x0001,
#ifdef GRIDPICO_RP2040_FLASH_ROUTER
    .bcdDevice = 0x0104,
    .iSerialNumber = FLASH_SERIAL_INDEX,
#else
    .bcdDevice = 0x0103,
#endif
    .iManufacturer = 1,
    .iProduct = 2,
    .bNumConfigurations = 1,
};
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, CONFIG_SIZE, 0, 100),
    TUD_CDC_DESCRIPTOR(0, 0, 0x81, 8, 0x02, 0x82, 64),
    TUD_MIDI_DESC_HEAD(2, 0, CABLES),
    TUD_MIDI_DESC_JACK_DESC(1, 3),
    TUD_MIDI_DESC_JACK_DESC(2, 4),
    TUD_MIDI_DESC_EP(0x03, 64, CABLES),
    TUD_MIDI_JACKID_IN_EMB(1), TUD_MIDI_JACKID_IN_EMB(2),
    TUD_MIDI_DESC_EP(GRIDPICO_NATIVE_MIDI_IN_EP, 64, CABLES),
    TUD_MIDI_JACKID_OUT_EMB(1), TUD_MIDI_JACKID_OUT_EMB(2),
};
_Static_assert(sizeof(configuration) == CONFIG_SIZE, "Invalid USB descriptor length");

const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    return index == 0 ? configuration : NULL;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language)
{
    static const char *const strings[] = {"Gridpico", "Gridpico RP2040 Router", "Keybed", "Panel"};
    static uint16_t descriptor[32];
    if (index == 0) {
        descriptor[0] = (TUSB_DESC_STRING << 8) | 4;
        descriptor[1] = 0x0409;
        return descriptor;
    }
#ifdef GRIDPICO_RP2040_FLASH_ROUTER
    if (index == FLASH_SERIAL_INDEX && language == 0x0409) return serial_descriptor;
#endif
    if (language != 0x0409 || index > sizeof(strings) / sizeof(strings[0])) return NULL;
    const char *text = strings[index - 1];
    uint8_t length = 0;
    while (length < 31 && text[length]) {
        descriptor[length + 1] = (uint8_t)text[length];
        ++length;
    }
    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * length + 2));
    return descriptor;
}
