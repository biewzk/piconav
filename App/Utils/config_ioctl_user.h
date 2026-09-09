#ifndef CONFIG_IOCTL_USER_H
#define CONFIG_IOCTL_USER_H

#include <linux/ioctl.h>
#include <stdint.h>

/*
 * Userspace mirror of the config driver ioctl definitions.
 * Must stay in sync with the kernel include/uapi/linux/config_ioctl.h.
 */

#define CONFIG_MAGIC 'C'

#define CONFIG_MAX_DATA (64 * 1024)

struct config_status {
	uint32_t active_block;
	uint32_t total_blocks;
	uint32_t erasesize;
	uint32_t write_count;
	uint32_t data_len;
	uint32_t seq;
	uint32_t last_error;
	uint32_t staged_len;	/* bytes staged in RAM waiting for commit */
	uint32_t staged_dirty;	/* 1 = staged data differs from flash */
};

#define CONFIG_IOC_GETSTATUS _IOR(CONFIG_MAGIC, 1, struct config_status)
#define CONFIG_IOC_FORMAT    _IO(CONFIG_MAGIC, 2)
#define CONFIG_IOC_ROLLBACK  _IO(CONFIG_MAGIC, 3)
#define CONFIG_IOC_COMMIT    _IO(CONFIG_MAGIC, 4)

#endif /* CONFIG_IOCTL_USER_H */
