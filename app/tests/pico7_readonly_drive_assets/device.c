#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "device/dcd.h"
#include "host/usbh_pvt.h"
#include "flash_startup.h"
#include "native_event.inc"

enum { CDC_OUT = 0x02, CDC_IN = 0x82, MIDI_OUT = 0x03, MIDI_IN = 0x83 };

typedef struct {
    uint8_t *buffer;
    uint16_t size;
    bool open, pending, stalled;
} fake_endpoint;

static fake_endpoint endpoints[2][5];

static fake_endpoint *endpoint(uint8_t address)
{
    assert((address & 0x7f) < 5);
    return &endpoints[address >> 7][address & 0x7f];
}

void flash_get_unique_id(uint8_t *id)
{
    memcpy(id, (uint8_t[]){0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef}, 8);
}

uint32_t tusb_time_millis_api(void) { return 0; }
bool dcd_init(uint8_t root, const tusb_rhport_init_t *init)
{
    assert(root == 0 && init->role == TUSB_ROLE_DEVICE);
    endpoint(0)->open = endpoint(0x80)->open = true;
    return true;
}

void dcd_int_enable(uint8_t root) { assert(root == 0); }
void dcd_int_disable(uint8_t root) { assert(root == 0); }
void dcd_set_address(uint8_t root, uint8_t address) { assert(root == 0 && address > 0); }
void dcd_sof_enable(uint8_t root, bool enable) { assert(root == 0 && !enable); }

bool dcd_edpt_open(uint8_t root, const tusb_desc_endpoint_t *descriptor)
{
    fake_endpoint *ep = endpoint(descriptor->bEndpointAddress);
    assert(root == 0 && !ep->open);
    ep->open = true;
    return true;
}

void dcd_edpt_close_all(uint8_t root)
{
    assert(root == 0);
    for (unsigned direction = 0; direction < 2; ++direction)
        memset(&endpoints[direction][1], 0, 4 * sizeof(fake_endpoint));
}

bool dcd_edpt_xfer(uint8_t root, uint8_t address, uint8_t *buffer, uint16_t size, bool irq)
{
    fake_endpoint *ep = endpoint(address);
    assert(root == 0 && !irq && ep->open && !ep->pending && !ep->stalled);
    assert(buffer || size == 0);
    ep->buffer = buffer;
    ep->size = size;
    ep->pending = true;
    return true;
}

void dcd_edpt_stall(uint8_t root, uint8_t address)
{
    assert(root == 0);
    endpoint(address)->pending = false;
    endpoint(address)->stalled = true;
}

void dcd_edpt_clear_stall(uint8_t root, uint8_t address)
{
    assert(root == 0);
    endpoint(address)->stalled = false;
}

/* The shared TinyUSB stream helpers link the USB-host branch; this test
 * initializes only the device root. Any dispatch into the host is a failure. */
bool usbh_edpt_claim(uint8_t address, uint8_t ep)
{
    (void)address;
    (void)ep;
    assert(false);
    return false;
}

bool usbh_edpt_release(uint8_t address, uint8_t ep)
{
    return usbh_edpt_claim(address, ep);
}

bool usbh_edpt_xfer_with_callback(uint8_t address, uint8_t ep, uint8_t *buffer,
                                 uint16_t size, tuh_xfer_cb_t cb, uintptr_t data)
{
    (void)buffer;
    (void)size;
    (void)cb;
    (void)data;
    return usbh_edpt_claim(address, ep);
}

static void transfer(uint8_t address, const void *bytes, uint16_t size)
{
    fake_endpoint *ep = endpoint(address);
    assert(ep->pending && !ep->stalled && size <= ep->size);
    if (address & 0x80) {
        assert(size == ep->size);
        if (bytes) assert(memcmp(ep->buffer, bytes, size) == 0);
    } else if (size) {
        assert(bytes);
        memcpy(ep->buffer, bytes, size);
    }
    ep->pending = false;
    dcd_event_xfer_complete(0, address, size, XFER_RESULT_SUCCESS, false);
    tud_task_ext(0, false);
}

static void setup(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                  uint16_t length, const void *reply)
{
    tusb_control_request_t packet = {
        .bmRequestType = type, .bRequest = request, .wValue = value,
        .wIndex = index, .wLength = length,
    };
    dcd_event_setup_received(0, (const uint8_t *)&packet, false);
    tud_task_ext(0, false);
    transfer(0x80, reply, length);
    if (type & 0x80) transfer(0, NULL, 0);
}

static void dump_descriptors(const char *path)
{
    assert(gridpico_flash_init_usb_serial());
    FILE *out = fopen(path, "wb");
    assert(out);
    const uint8_t *config = tud_descriptor_configuration_cb(0);
    assert(!tud_descriptor_configuration_cb(1));
    unsigned length = config[2] + 256u * config[3];
#if CFG_TUD_MSC
    assert(length == 222 && config[4] == 5);
    static const uint8_t msc[] = {
        9, 4, 4, 0, 2, 8, 6, 0x50, 6,
        7, 5, 0x04, 2, 64, 0, 0,
        7, 5, 0x84, 2, 64, 0, 0,
    };
    assert(memcmp(config + 199, msc, sizeof(msc)) == 0);
#else
    assert(length == 199 && config[4] == 4);
#endif
    assert(fwrite(tud_descriptor_device_cb(), 1, 18, out) == 18);
    assert(fwrite(config, 1, length, out) == length);
    for (uint8_t index = 0; index < 6 + CFG_TUD_MSC; ++index) {
        const uint16_t *s = tud_descriptor_string_cb(index, 0x0409);
        assert(s && fwrite(s, 1, s[0] & 0xff, out) == (size_t)(s[0] & 0xff));
    }
    assert(!tud_descriptor_string_cb(6 + CFG_TUD_MSC, 0x0409));
    assert(fclose(out) == 0);
}

#if CFG_TUD_MSC
enum { DRIVE_OUT = 0x04, DRIVE_IN = 0x84, SECTOR = 512, SECTORS = 768 };
extern const uint8_t __gridpico_usb_drive_start[];

static uint32_t tag;

static void cbw(uint8_t op, uint8_t lun, uint32_t bytes, bool in,
                uint8_t command_length, uint32_t lba)
{
    msc_cbw_t cmd = {
        .signature = MSC_CBW_SIGNATURE, .tag = ++tag, .total_bytes = bytes,
        .dir = in ? 0x80 : 0, .lun = lun, .cmd_len = command_length, .command = {op},
    };
    if (op == SCSI_CMD_READ_10 || op == SCSI_CMD_WRITE_10) {
        cmd.command[2] = (uint8_t)(lba >> 24);
        cmd.command[3] = (uint8_t)(lba >> 16);
        cmd.command[4] = (uint8_t)(lba >> 8);
        cmd.command[5] = (uint8_t)lba;
        cmd.command[8] = 1;
    } else if (op == SCSI_CMD_START_STOP_UNIT) {
        cmd.command[4] = 2; /* Eject. */
    } else if (bytes <= UINT8_MAX) {
        cmd.command[4] = (uint8_t)bytes;
    }
    transfer(DRIVE_OUT, &cmd, sizeof(cmd));
}

static void csw(uint8_t status, uint32_t residue)
{
    const msc_csw_t expected = {
        .signature = MSC_CSW_SIGNATURE, .tag = tag, .data_residue = residue, .status = status,
    };
    transfer(DRIVE_IN, &expected, sizeof(expected));
}

static void fail_read(uint8_t op, uint8_t lun, uint16_t size, uint32_t lba)
{
    cbw(op, lun, size, true, 10, lba);
    assert(endpoint(DRIVE_IN)->stalled && !endpoint(DRIVE_IN)->pending);
    setup(0x02, TUSB_REQ_CLEAR_FEATURE, 0, DRIVE_IN, 0, NULL);
    csw(MSC_CSW_STATUS_FAILED, size);
}

static void test_drive(const char *image_after)
{
    const uint8_t *image = __gridpico_usb_drive_start;
    setup(0xa1, MSC_REQ_GET_MAX_LUN, 0, 4, 1, (uint8_t[]){0});
    cbw(SCSI_CMD_INQUIRY, 0, 36, true, 6, 0);
    assert(memcmp(endpoint(DRIVE_IN)->buffer + 8, "GRIDPICORead-only tools 1.0 ", 28) == 0);
    transfer(DRIVE_IN, NULL, 36);
    csw(MSC_CSW_STATUS_PASSED, 0);
    cbw(SCSI_CMD_TEST_UNIT_READY, 1, 0, false, 6, 0);
    csw(MSC_CSW_STATUS_FAILED, 0);

    cbw(SCSI_CMD_READ_CAPACITY_10, 0, 8, true, 10, 0);
    transfer(DRIVE_IN, (uint8_t[]){0, 0, 2, 0xff, 0, 0, 2, 0}, 8);
    csw(MSC_CSW_STATUS_PASSED, 0);
    fail_read(SCSI_CMD_READ_CAPACITY_10, 1, 8, 0);
    cbw(SCSI_CMD_MODE_SENSE_6, 0, 4, true, 6, 0);
    transfer(DRIVE_IN, (uint8_t[]){3, 0, 0x80, 0}, 4);
    csw(MSC_CSW_STATUS_PASSED, 0);
    puts("BOT one LUN, exact 768 x 512 capacity, MODE SENSE write-protect");

    for (uint32_t lba = 0; lba < SECTORS; lba += SECTORS - 1) {
        cbw(SCSI_CMD_READ_10, 0, SECTOR, true, 10, lba);
        transfer(DRIVE_IN, image + lba * SECTOR, SECTOR);
        csw(MSC_CSW_STATUS_PASSED, 0);
    }
    fail_read(SCSI_CMD_READ_10, 0, SECTOR, SECTORS);
    fail_read(SCSI_CMD_READ_10, 1, SECTOR, 0);
    uint8_t partial[5] = {0xa5, 0, 0, 0, 0xa5};
    assert(tud_msc_read10_cb(0, 0, 509, partial + 1, 3) == 3);
    assert(memcmp(partial + 1, image + 509, 3) == 0);
    assert(tud_msc_read10_cb(0, 767, 510, partial + 1, 3) == TUD_MSC_RET_ERROR);
    assert(tud_msc_read10_cb(0, UINT32_MAX, 0, partial + 1, 3) == TUD_MSC_RET_ERROR);
    assert(tud_msc_read10_cb(0, 0, UINT32_MAX, partial + 1, 3) == TUD_MSC_RET_ERROR);
    assert(partial[0] == 0xa5 && partial[4] == 0xa5 && memcmp(partial + 1, image + 509, 3) == 0);
    puts("BOT first/last exact reads, range/LUN refusals and bounded offset read");

    cbw(SCSI_CMD_WRITE_10, 0, SECTOR, false, 10, 0);
    assert(endpoint(DRIVE_OUT)->stalled && !endpoint(DRIVE_OUT)->pending);
    csw(MSC_CSW_STATUS_FAILED, SECTOR);
    setup(0x02, TUSB_REQ_CLEAR_FEATURE, 0, DRIVE_OUT, 0, NULL);
    cbw(SCSI_CMD_REQUEST_SENSE, 0, 18, true, 6, 0);
    assert((endpoint(DRIVE_IN)->buffer[2] & 0xf) == SCSI_SENSE_DATA_PROTECT);
    assert(endpoint(DRIVE_IN)->buffer[12] == 0x27 && endpoint(DRIVE_IN)->buffer[13] == 0);
    transfer(DRIVE_IN, NULL, 18);
    csw(MSC_CSW_STATUS_PASSED, 0);
    assert(tud_msc_write10_cb(0, 0, 0, partial, sizeof(partial)) == TUD_MSC_RET_ERROR);
    cbw(0x56, 0, 0, false, 10, 0);
    csw(MSC_CSW_STATUS_FAILED, 0);

    cbw(SCSI_CMD_START_STOP_UNIT, 0, 0, false, 6, 0);
    csw(MSC_CSW_STATUS_PASSED, 0);
    setup(0x21, MSC_REQ_RESET, 0, 4, 0, NULL);
    cbw(SCSI_CMD_TEST_UNIT_READY, 0, 0, false, 6, 0);
    csw(MSC_CSW_STATUS_PASSED, 0);
    FILE *out = fopen(image_after, "wb");
    assert(out && fwrite(image, SECTOR, SECTORS, out) == SECTORS && fclose(out) == 0);
    puts("BOT WRITE10 fails before data, DATA PROTECT 27/00; eject/reset storage-local");
}
#endif

int main(int argc, char **argv)
{
    assert(argc == (CFG_TUD_MSC ? 3 : 2));
    dump_descriptors(argv[1]);
    const tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    assert(tud_rhport_init(0, &init));
    dcd_event_bus_reset(0, TUSB_SPEED_FULL, false);
    tud_task_ext(0, false);
    assert(native_bus_epoch == 1);
    setup(0, TUSB_REQ_SET_CONFIGURATION, 1, 0, 0, NULL);
    setup(0x21, CDC_REQUEST_SET_CONTROL_LINE_STATE, 1, 0, 0, NULL);
    assert(tud_cdc_connected() && tud_midi_mounted());
    assert(endpoint(0x81)->open && endpoint(CDC_OUT)->open && endpoint(MIDI_OUT)->open);
    const uint8_t reply[] = "OK\r\n";
    const uint8_t note[] = {0x09, 0x90, 0x3c, 0x60};
    assert(tud_cdc_write(reply, sizeof(reply) - 1) == sizeof(reply) - 1);
    assert(tud_cdc_write_flush() == sizeof(reply) - 1);
    native_event_state = NATIVE_EVENT_ARMED;
    assert(tud_midi_packet_write(note));
#if CFG_TUD_MSC
    test_drive(argv[2]);
#endif
    assert(native_bus_epoch == 1 && native_event_state == NATIVE_EVENT_ARMED);
    assert(tud_mounted() && tud_cdc_connected() && tud_midi_mounted());
    const uint8_t request[] = "status\n";
    uint8_t readback[sizeof(request) - 1];
    transfer(CDC_OUT, request, sizeof(readback));
    assert(tud_cdc_read(readback, sizeof(readback)) == sizeof(readback));
    assert(memcmp(readback, request, sizeof(readback)) == 0);
    const uint8_t from_host[2][4] = {{0x09, 0x90, 0x3c, 0x60}, {0x19, 0x91, 0x20, 0x01}};
    transfer(MIDI_OUT, from_host, sizeof(from_host));
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t packet[4];
        assert(tud_midi_packet_read(packet) && memcmp(packet, from_host[i], sizeof(packet)) == 0);
    }
    transfer(CDC_IN, reply, sizeof(reply) - 1);
    assert(native_event_state == NATIVE_EVENT_ARMED);
    transfer(MIDI_IN, note, sizeof(note));
    assert(native_event_state == NATIVE_EVENT_SEEN && native_event_length == sizeof(note) &&
           native_event_result == XFER_RESULT_SUCCESS);
    printf("configuration %u B; exact bidirectional CDC/MIDI bytes (2 MIDI cables); only EP83 completed owner\n",
           CFG_TUD_MSC ? 222u : 199u);
    return 0;
}
