#include "tusb.h"

#include <stdint.h>
#include <string.h>

enum { DRIVE_SECTOR_BYTES = 512, DRIVE_SECTORS = 768 };

extern const uint8_t __gridpico_usb_drive_start[];
extern const uint8_t __gridpico_usb_drive_end[];

static bool valid_lun(uint8_t lun)
{
    if (lun == 0) return true;
    (void)tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x25, 0);
    return false;
}

static bool drive_ready(uint8_t lun)
{
    const uintptr_t start = (uintptr_t)__gridpico_usb_drive_start;
    const uintptr_t end = (uintptr_t)__gridpico_usb_drive_end;
    return valid_lun(lun) && end >= start && end - start == DRIVE_SECTOR_BYTES * DRIVE_SECTORS;
}

uint8_t tud_msc_get_maxlun_cb(void)
{
    return 1; /* TinyUSB takes a LUN count and returns the highest index. */
}

uint32_t tud_msc_inquiry2_cb(uint8_t lun, scsi_inquiry_resp_t *inquiry, uint32_t bufsize)
{
    if (bufsize < sizeof(*inquiry)) return 0;
    if (lun != 0) {
        inquiry->peripheral_qualifier = 3;
        inquiry->peripheral_device_type = 0x1f;
    } else {
        memcpy(inquiry->vendor_id, "GRIDPICO", sizeof(inquiry->vendor_id));
        memcpy(inquiry->product_id, "Read-only tools ", sizeof(inquiry->product_id));
        memcpy(inquiry->product_rev, "1.0 ", sizeof(inquiry->product_rev));
    }
    return sizeof(*inquiry);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    return drive_ready(lun);
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    const bool ready = drive_ready(lun);
    *block_count = ready ? DRIVE_SECTORS : 0;
    *block_size = ready ? DRIVE_SECTOR_BYTES : 0;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return false;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    if (!drive_ready(lun) || lba >= DRIVE_SECTORS || offset >= DRIVE_SECTOR_BYTES ||
        size == 0 || size > DRIVE_SECTOR_BYTES || buffer == NULL) return TUD_MSC_RET_ERROR;
    const uint32_t start = lba * DRIVE_SECTOR_BYTES + offset;
    if (size > DRIVE_SECTORS * DRIVE_SECTOR_BYTES - start) return TUD_MSC_RET_ERROR;
    memcpy(buffer, __gridpico_usb_drive_start + start, size);
    return (int32_t)size;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    (void)lba;
    (void)offset;
    (void)buffer;
    (void)size;
    (void)tud_msc_set_sense(lun, SCSI_SENSE_DATA_PROTECT, 0x27, 0);
    return TUD_MSC_RET_ERROR;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)power_condition;
    (void)start;
    (void)load_eject;
    /* A host may unmount the volume. There is no local media state to change. */
    return drive_ready(lun);
}

bool tud_msc_prevent_allow_medium_removal_cb(uint8_t lun, uint8_t prohibit_removal, uint8_t control)
{
    (void)prohibit_removal;
    (void)control;
    return drive_ready(lun);
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const command[16], void *buffer, uint16_t size)
{
    (void)command;
    (void)buffer;
    (void)size;
    if (valid_lun(lun)) (void)tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0);
    return TUD_MSC_RET_ERROR;
}
