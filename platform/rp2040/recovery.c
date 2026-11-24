#include "bootsel_line.h"

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "tusb.h"

#if !PICO_NO_FLASH || TUSB_VERSION_NUMBER != 2100
#error "Build the recovery probe as no_flash with TinyUSB 0.21.0"
#endif

enum {
    DEVICE_ROOT = 0,
    CDC_CONTROL = 0,
    INTERFACES = 2,
    CONFIG_SIZE = TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN,
    WATCHDOG_MS = 2000,
    BOOTSEL_DRAIN_US = 100000,
    RX_BATCH = 32,
};

static const char banner[] = "Gridpico RP2040 recovery\n";
static const char boot_ack[] = "OK BOOTSEL\n";
static gridpico_bootsel_line command;
static bool banner_pending;
#ifdef GRIDPICO_RP2040_WATCHDOG_PROBE
static uint64_t stop_feeding_at;
#endif

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
    .bcdDevice = 0x0102,
    .iManufacturer = 1,
    .iProduct = 2,
    .bNumConfigurations = 1,
};
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, INTERFACES, 0, CONFIG_SIZE, 0, 100),
    TUD_CDC_DESCRIPTOR(CDC_CONTROL, 0, 0x81, 8, 0x02, 0x82, 64),
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
    static const char *const strings[] = {"Gridpico", "Gridpico Recovery Probe"};
    static uint16_t descriptor[32] = {0};
    if (index == 0) {
        descriptor[0] = (TUSB_DESC_STRING << 8) | 4;
        descriptor[1] = 0x0409;
        return descriptor;
    }
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

void tud_cdc_line_state_cb(uint8_t index, bool dtr, bool rts)
{
    (void)rts;
    if (index == 0 && gridpico_bootsel_set_dtr(&command, dtr)) {
        tud_cdc_n_read_flush(index);
        banner_pending = dtr;
    }
}

void tud_umount_cb(void)
{
    gridpico_bootsel_set_dtr(&command, false);
    banner_pending = false;
}

static bool write_cdc(const char *message, uint32_t length)
{
    if (!tud_cdc_connected() || tud_cdc_write_available() < length) return false;
    tud_cdc_write(message, length);
    tud_cdc_write_flush();
    return true;
}

static _Noreturn void request_bootsel(void)
{
    const uint64_t deadline = time_us_64() + BOOTSEL_DRAIN_US;
    bool replied = false;
    do {
        if (!replied) replied = write_cdc(boot_ack, sizeof(boot_ack) - 1);
        tud_task_ext(0, false);
    } while (time_us_64() < deadline);
    reset_usb_boot(0, 0);
}

static void service_cdc(void)
{
    if (!tud_cdc_connected()) return;
    if (banner_pending && write_cdc(banner, sizeof(banner) - 1)) {
        banner_pending = false;
#ifdef GRIDPICO_RP2040_WATCHDOG_PROBE
        if (stop_feeding_at == 0) stop_feeding_at = time_us_64() + 250000;
#endif
    }
    uint8_t bytes[RX_BATCH];
    const uint32_t count = tud_cdc_read(bytes, sizeof(bytes));
    for (uint32_t i = 0; i < count; ++i) {
        if (gridpico_bootsel_feed(&command, bytes[i])) request_bootsel();
    }
}

int main(void)
{
    watchdog_enable(WATCHDOG_MS, false);
    const tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    if (!tusb_init(DEVICE_ROOT, &init)) reset_usb_boot(0, 0);

    for (;;) {
        tud_task_ext(0, false);
        service_cdc();
#ifdef GRIDPICO_RP2040_WATCHDOG_PROBE
        if (stop_feeding_at != 0 && time_us_64() >= stop_feeding_at) {
            for (;;) tight_loop_contents();
        }
#endif
        watchdog_update();
    }
}
