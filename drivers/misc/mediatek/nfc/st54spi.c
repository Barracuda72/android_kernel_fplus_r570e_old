/*
 * Simple synchronous userspace interface to SPI devices
 *
 * Copyright (C) 2006 SWAPP
 *	Andrea Paterniani <a.paterniani@swapp-eng.it>
 * Copyright (C) 2007 David Brownell (simplification, cleanup)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*
 * Modified by ST Microelectronics.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/acpi.h>

#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <linux/uaccess.h>
#include <linux/platform_data/spi-mt65xx.h>

/*
 * Define WITH_RESET_CB for integrations
 * where VCC_SE is connected directly to SE_VCC
 */
#define WITH_RESET_CB 1

#ifdef WITH_RESET_CB
#include "st21nfc/st21nfc.h"
#endif

/*
 * This supports access to SPI devices using normal userspace I/O calls.
 * Note that while traditional UNIX/POSIX I/O semantics are half duplex,
 * and often mask message boundaries, full SPI support requires full duplex
 * transfers.  There are several kinds of internal message boundaries to
 * handle chipselect management and other protocol options.
 *
 * SPI has a character major number assigned.  We allocate minor numbers
 * dynamically using a bitmask.  You must use hotplug tools, such as udev
 * (or mdev with busybox) to create and destroy the /dev/st54spi device
 * nodes, since there is no fixed association of minor numbers with any
 * particular SPI bus or device.
 */
//#define SPIDEV_MAJOR			0	/* dynamic */
static int spidev_major;
#define N_SPI_MINORS			1	/* ... up to 256 */

static DECLARE_BITMAP(minors, N_SPI_MINORS);


#define ST54SPI_IOC_RD_POWER		_IOR(SPI_IOC_MAGIC, 99, __u32)
#define ST54SPI_IOC_WR_POWER		_IOW(SPI_IOC_MAGIC, 99, __u32)



/* Bit masks for spi_device.mode management.  Note that incorrect
 * settings for some settings can cause *lots* of trouble for other
 * devices on a shared bus:
 *
 *  - CS_HIGH ... this device will be active when it shouldn't be
 *  - 3WIRE ... when active, it won't behave as it should
 *  - NO_CS ... there will be no explicit message boundaries; this
 *	is completely incompatible with the shared bus model
 *  - READY ... transfers may proceed when they shouldn't.
 *
 * REVISIT should changing those flags be privileged?
 */
#define SPI_MODE_MASK		(SPI_CPHA | SPI_CPOL | SPI_CS_HIGH \
				| SPI_LSB_FIRST | SPI_3WIRE | SPI_LOOP \
				| SPI_NO_CS | SPI_READY | SPI_TX_DUAL \
				| SPI_TX_QUAD | SPI_RX_DUAL | SPI_RX_QUAD)

struct st54spi_data {
	dev_t			devt;
	spinlock_t		spi_lock;
	struct spi_device	*spi;
	struct spi_device   *spi_reset;
	struct list_head	device_entry;

	/* TX/RX buffers are NULL unless this device is open (users > 0) */
	struct mutex		buf_lock;
	unsigned		users;
	u8			*tx_buffer;
	u8			*rx_buffer;
	u32			speed_hz;
	
	/* GPIO for SE_POWER_REQ */
	int power_gpio;
};

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

static unsigned bufsiz = 4096;
module_param(bufsiz, uint, S_IRUGO);
MODULE_PARM_DESC(bufsiz, "data bytes in biggest supported SPI message");

static bool debug_enabled = true;
#define VERBOSE 1

/*-------------------------------------------------------------------------*/

static ssize_t
st54spi_sync(struct st54spi_data *st54spi, struct spi_message *message)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int status;
	struct spi_device *spi;

	spin_lock_irq(&st54spi->spi_lock);
	spi = st54spi->spi;
	spin_unlock_irq(&st54spi->spi_lock);

	if (spi == NULL)
		status = -ESHUTDOWN;
	else
		status = spi_sync(spi, message);

	if (status == 0)
		status = message->actual_length;

	return status;
}

static inline ssize_t
st54spi_sync_write(struct st54spi_data *st54spi, size_t len)
{
	struct spi_transfer	t = {
			.tx_buf		= st54spi->tx_buffer,
			.len		= len,
			.speed_hz	= st54spi->speed_hz,
		};
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return st54spi_sync(st54spi, &m);
}

static inline ssize_t
st54spi_sync_read(struct st54spi_data *st54spi, size_t len)
{
	struct spi_transfer	t = {
			.rx_buf		= st54spi->rx_buffer,
			.len		= len,
			.speed_hz	= st54spi->speed_hz,
		};
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return st54spi_sync(st54spi, &m);
}

/*-------------------------------------------------------------------------*/

/* Read-only message with current device setup */
static ssize_t
st54spi_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct st54spi_data	*st54spi;
	ssize_t			status = 0;

	/* chipselect only toggles at start or end of operation */
	if (count > bufsiz)
		return -EMSGSIZE;

	st54spi = filp->private_data;

        if (debug_enabled) pr_info("st54spi Read: %d bytes\n", (int)count);

	mutex_lock(&st54spi->buf_lock);
	status = st54spi_sync_read(st54spi, count);
	if (status > 0) {
		unsigned long	missing;

		missing = copy_to_user(buf, st54spi->rx_buffer, status);
		if (missing == status)
			status = -EFAULT;
		else
			status = status - missing;
	}
	mutex_unlock(&st54spi->buf_lock);

        if (debug_enabled) pr_info("st54spi Read: status: %d\n", (int)status);

	return status;
}

/* Write-only message with current device setup */
static ssize_t
st54spi_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct st54spi_data	*st54spi;
	ssize_t			status = 0;
	unsigned long		missing;

	/* chipselect only toggles at start or end of operation */
	if (count > bufsiz)
		return -EMSGSIZE;

	st54spi = filp->private_data;

        if (debug_enabled) pr_info("st54spi Write: %d bytes\n", (int)count);

	mutex_lock(&st54spi->buf_lock);
	missing = copy_from_user(st54spi->tx_buffer, buf, count);
	if (missing == 0)
		status = st54spi_sync_write(st54spi, count);
	else
		status = -EFAULT;
	mutex_unlock(&st54spi->buf_lock);

        if (debug_enabled) pr_info("st54spi Write: status: %d\n", (int)status);

	return status;
}

static int st54spi_message(struct st54spi_data *st54spi,
		struct spi_ioc_transfer *u_xfers, unsigned n_xfers)
{
	struct spi_message	msg;
	struct spi_transfer	*k_xfers;
	struct spi_transfer	*k_tmp;
	struct spi_ioc_transfer *u_tmp;
	unsigned		n, total, tx_total, rx_total;
	u8			*tx_buf, *rx_buf;
	int			status = -EFAULT;

	spi_message_init(&msg);
	k_xfers = kcalloc(n_xfers, sizeof(*k_tmp), GFP_KERNEL);
	if (k_xfers == NULL)
		return -ENOMEM;

	/* Construct spi_message, copying any tx data to bounce buffer.
	 * We walk the array of user-provided transfers, using each one
	 * to initialize a kernel version of the same transfer.
	 */
	tx_buf = st54spi->tx_buffer;
	rx_buf = st54spi->rx_buffer;
	total = 0;
	tx_total = 0;
	rx_total = 0;
	for (n = n_xfers, k_tmp = k_xfers, u_tmp = u_xfers;
			n;
			n--, k_tmp++, u_tmp++) {
		k_tmp->len = u_tmp->len;

		total += k_tmp->len;
		/* Since the function returns the total length of transfers
		 * on success, restrict the total to positive int values to
		 * avoid the return value looking like an error.  Also check
		 * each transfer length to avoid arithmetic overflow.
		 */
		if (total > INT_MAX || k_tmp->len > INT_MAX) {
			status = -EMSGSIZE;
			goto done;
		}

		if (u_tmp->rx_buf) {
			/* this transfer needs space in RX bounce buffer */
			rx_total += k_tmp->len;
			if (rx_total > bufsiz) {
				status = -EMSGSIZE;
				goto done;
			}
			k_tmp->rx_buf = rx_buf;
			if (!access_ok(VERIFY_WRITE, (u8 __user *)
						(uintptr_t) u_tmp->rx_buf,
						u_tmp->len))
				goto done;
			rx_buf += k_tmp->len;
		}
		if (u_tmp->tx_buf) {
			/* this transfer needs space in TX bounce buffer */
			tx_total += k_tmp->len;
			if (tx_total > bufsiz) {
				status = -EMSGSIZE;
				goto done;
			}
			k_tmp->tx_buf = tx_buf;
			if (copy_from_user(tx_buf, (const u8 __user *)
						(uintptr_t) u_tmp->tx_buf,
					u_tmp->len))
				goto done;
			tx_buf += k_tmp->len;
		}

		k_tmp->cs_change = !!u_tmp->cs_change;
		k_tmp->tx_nbits = u_tmp->tx_nbits;
		k_tmp->rx_nbits = u_tmp->rx_nbits;
		k_tmp->bits_per_word = u_tmp->bits_per_word;
		k_tmp->delay_usecs = u_tmp->delay_usecs;
		k_tmp->speed_hz = u_tmp->speed_hz;
		if (!k_tmp->speed_hz)
			k_tmp->speed_hz = st54spi->speed_hz;
#ifdef VERBOSE
		dev_dbg(&st54spi->spi->dev,
			"  xfer len %u %s%s%s%dbits %u usec %uHz\n",
			u_tmp->len,
			u_tmp->rx_buf ? "rx " : "",
			u_tmp->tx_buf ? "tx " : "",
			u_tmp->cs_change ? "cs " : "",
			u_tmp->bits_per_word ? : st54spi->spi->bits_per_word,
			u_tmp->delay_usecs,
			u_tmp->speed_hz ? : st54spi->spi->max_speed_hz);
#endif
		spi_message_add_tail(k_tmp, &msg);
	}

	status = st54spi_sync(st54spi, &msg);
	if (status < 0)
		goto done;

	/* copy any rx data out of bounce buffer */
	rx_buf = st54spi->rx_buffer;
	for (n = n_xfers, u_tmp = u_xfers; n; n--, u_tmp++) {
		if (u_tmp->rx_buf) {
			if (__copy_to_user((u8 __user *)
					(uintptr_t) u_tmp->rx_buf, rx_buf,
					u_tmp->len)) {
				status = -EFAULT;
				goto done;
			}
			rx_buf += u_tmp->len;
		}
	}
	status = total;

done:
	kfree(k_xfers);
	return status;
}

static struct spi_ioc_transfer *
st54spi_get_ioc_message(unsigned int cmd, struct spi_ioc_transfer __user *u_ioc,
		unsigned *n_ioc)
{
	struct spi_ioc_transfer	*ioc;
	u32	tmp;

	/* Check type, command number and direction */
	if (_IOC_TYPE(cmd) != SPI_IOC_MAGIC
			|| _IOC_NR(cmd) != _IOC_NR(SPI_IOC_MESSAGE(0))
			|| _IOC_DIR(cmd) != _IOC_WRITE)
		return ERR_PTR(-ENOTTY);

	tmp = _IOC_SIZE(cmd);
	if ((tmp % sizeof(struct spi_ioc_transfer)) != 0)
		return ERR_PTR(-EINVAL);
	*n_ioc = tmp / sizeof(struct spi_ioc_transfer);
	if (*n_ioc == 0)
		return NULL;

	/* copy into scratch area */
	ioc = kmalloc(tmp, GFP_KERNEL);
	if (!ioc)
		return ERR_PTR(-ENOMEM);
	if (__copy_from_user(ioc, u_ioc, tmp)) {
		kfree(ioc);
		return ERR_PTR(-EFAULT);
	}
	return ioc;
}

static void
st54spi_power_set(struct st54spi_data	*st54spi, int val)
{
	if (debug_enabled)
		pr_info("st54spi se_pwr_req DISABLED: %d\n", val);
	/* set gpio-power accordingly to val */
	//if (st54spi->power_gpio)
	//    gpio_set_value(st54spi->power_gpio, val ? 1 : 0);
}

static int
st54spi_power_get(struct st54spi_data	*st54spi)
{
	if (st54spi->power_gpio)
	    return gpio_get_value(st54spi->power_gpio);
	return 0;
}



static long
st54spi_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int			err = 0;
	int			retval = 0;
	struct st54spi_data	*st54spi;
	struct spi_device	*spi;
	u32			tmp;
	unsigned		n_ioc;
	struct spi_ioc_transfer	*ioc;

	/* Check type and command number */
	if (_IOC_TYPE(cmd) != SPI_IOC_MAGIC)
		return -ENOTTY;

	/* Check access direction once here; don't repeat below.
	 * IOC_DIR is from the user perspective, while access_ok is
	 * from the kernel perspective; so they look reversed.
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE,
				(void __user *)arg, _IOC_SIZE(cmd));
	if (err == 0 && _IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ,
				(void __user *)arg, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;

	/* guard against device removal before, or while,
	 * we issue this ioctl.
	 */
	st54spi = filp->private_data;
	spin_lock_irq(&st54spi->spi_lock);
	spi = spi_dev_get(st54spi->spi);
	spin_unlock_irq(&st54spi->spi_lock);

        if (debug_enabled) pr_info("st54spi ioctl cmd %d\n", cmd);

	if (spi == NULL)
		return -ESHUTDOWN;

	/* use the buffer lock here for triple duty:
	 *  - prevent I/O (from us) so calling spi_setup() is safe;
	 *  - prevent concurrent SPI_IOC_WR_* from morphing
	 *    data fields while SPI_IOC_RD_* reads them;
	 *  - SPI_IOC_MESSAGE needs the buffer locked "normally".
	 */
	mutex_lock(&st54spi->buf_lock);

	switch (cmd) {
	/* read requests */
	case SPI_IOC_RD_MODE:
		retval = __put_user(spi->mode & SPI_MODE_MASK,
					(__u8 __user *)arg);
		break;
	case SPI_IOC_RD_MODE32:
		retval = __put_user(spi->mode & SPI_MODE_MASK,
					(__u32 __user *)arg);
		break;
	case SPI_IOC_RD_LSB_FIRST:
		retval = __put_user((spi->mode & SPI_LSB_FIRST) ?  1 : 0,
					(__u8 __user *)arg);
		break;
	case SPI_IOC_RD_BITS_PER_WORD:
		retval = __put_user(spi->bits_per_word, (__u8 __user *)arg);
		break;
	case SPI_IOC_RD_MAX_SPEED_HZ:
		retval = __put_user(st54spi->speed_hz, (__u32 __user *)arg);
		break;
	case ST54SPI_IOC_RD_POWER:
		retval = __put_user(st54spi_power_get(st54spi), (__u32 __user *)arg);
		break;

	/* write requests */
	case SPI_IOC_WR_MODE:
	case SPI_IOC_WR_MODE32:
		if (cmd == SPI_IOC_WR_MODE)
			retval = __get_user(tmp, (u8 __user *)arg);
		else
			retval = __get_user(tmp, (u32 __user *)arg);
		if (retval == 0) {
			u32	save = spi->mode;

			if (tmp & ~SPI_MODE_MASK) {
				retval = -EINVAL;
				break;
			}

			tmp |= spi->mode & ~SPI_MODE_MASK;
			spi->mode = (u16)tmp;
			retval = spi_setup(spi);
			if (retval < 0)
				spi->mode = save;
			else
				dev_dbg(&spi->dev, "spi mode %x\n", tmp);
		}
		break;
	case SPI_IOC_WR_LSB_FIRST:
		retval = __get_user(tmp, (__u8 __user *)arg);
		if (retval == 0) {
			u32	save = spi->mode;

			if (tmp)
				spi->mode |= SPI_LSB_FIRST;
			else
				spi->mode &= ~SPI_LSB_FIRST;
			retval = spi_setup(spi);
			if (retval < 0)
				spi->mode = save;
			else
				dev_dbg(&spi->dev, "%csb first\n",
						tmp ? 'l' : 'm');
		}
		break;
	case SPI_IOC_WR_BITS_PER_WORD:
		retval = __get_user(tmp, (__u8 __user *)arg);
		if (retval == 0) {
			u8	save = spi->bits_per_word;

			spi->bits_per_word = tmp;
			retval = spi_setup(spi);
			if (retval < 0)
				spi->bits_per_word = save;
			else
				dev_dbg(&spi->dev, "%d bits per word\n", tmp);
		}
		break;
	case SPI_IOC_WR_MAX_SPEED_HZ:
		retval = __get_user(tmp, (__u32 __user *)arg);
		if (retval == 0) {
			u32	save = spi->max_speed_hz;

			spi->max_speed_hz = tmp;
			retval = spi_setup(spi);
			if (retval >= 0)
				st54spi->speed_hz = tmp;
			else
				dev_dbg(&spi->dev, "%d Hz (max)\n", tmp);
			spi->max_speed_hz = save;
		}
		break;
	case ST54SPI_IOC_WR_POWER:
		retval = __get_user(tmp, (__u32 __user *)arg);
		if (retval == 0) {
			st54spi_power_set(st54spi, tmp ? 1 : 0);
			dev_dbg(&spi->dev, "SE_POWER_REQ set: %d\n", tmp);
		}
		break;

	default:
		/* segmented and/or full-duplex I/O request */
		/* Check message and copy into scratch area */
		ioc = st54spi_get_ioc_message(cmd,
				(struct spi_ioc_transfer __user *)arg, &n_ioc);
		if (IS_ERR(ioc)) {
			retval = PTR_ERR(ioc);
			break;
		}
		if (!ioc)
			break;	/* n_ioc is also 0 */

		/* translate to spi_message, execute */
		retval = st54spi_message(st54spi, ioc, n_ioc);
		kfree(ioc);
		break;
	}

	mutex_unlock(&st54spi->buf_lock);
	spi_dev_put(spi);

        if (debug_enabled) pr_info("st54spi ioctl retval %d\n", retval);

	return retval;
}

#ifdef CONFIG_COMPAT
static long
st54spi_compat_ioc_message(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct spi_ioc_transfer __user	*u_ioc;
	int				retval = 0;
	struct st54spi_data		*st54spi;
	struct spi_device		*spi;
	unsigned			n_ioc, n;
	struct spi_ioc_transfer		*ioc;

	u_ioc = (struct spi_ioc_transfer __user *) compat_ptr(arg);
	if (!access_ok(VERIFY_READ, u_ioc, _IOC_SIZE(cmd)))
		return -EFAULT;

	/* guard against device removal before, or while,
	 * we issue this ioctl.
	 */
	st54spi = filp->private_data;
	spin_lock_irq(&st54spi->spi_lock);
	spi = spi_dev_get(st54spi->spi);
	spin_unlock_irq(&st54spi->spi_lock);

    if (debug_enabled) pr_info("st54spi compat_ioctl cmd %d\n", cmd);
	if (spi == NULL)
		return -ESHUTDOWN;

	/* SPI_IOC_MESSAGE needs the buffer locked "normally" */
	mutex_lock(&st54spi->buf_lock);

	/* Check message and copy into scratch area */
	ioc = st54spi_get_ioc_message(cmd, u_ioc, &n_ioc);
	if (IS_ERR(ioc)) {
		retval = PTR_ERR(ioc);
		goto done;
	}
	if (!ioc)
		goto done;	/* n_ioc is also 0 */

	/* Convert buffer pointers */
	for (n = 0; n < n_ioc; n++) {
		ioc[n].rx_buf = (uintptr_t) compat_ptr(ioc[n].rx_buf);
		ioc[n].tx_buf = (uintptr_t) compat_ptr(ioc[n].tx_buf);
	}

	/* translate to spi_message, execute */
	retval = st54spi_message(st54spi, ioc, n_ioc);
	kfree(ioc);

done:
	mutex_unlock(&st54spi->buf_lock);
	spi_dev_put(spi);
    if (debug_enabled) pr_info("st54spi compat_ioctl retval %d\n", retval);
	return retval;
}

static long
st54spi_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	if (_IOC_TYPE(cmd) == SPI_IOC_MAGIC
			&& _IOC_NR(cmd) == _IOC_NR(SPI_IOC_MESSAGE(0))
			&& _IOC_DIR(cmd) == _IOC_WRITE)
		return st54spi_compat_ioc_message(filp, cmd, arg);

	return st54spi_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define st54spi_compat_ioctl NULL
#endif /* CONFIG_COMPAT */

static int st54spi_open(struct inode *inode, struct file *filp)
{
	struct st54spi_data	*st54spi;
	int			status = -ENXIO;

	mutex_lock(&device_list_lock);

	list_for_each_entry(st54spi, &device_list, device_entry) {
		if (st54spi->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}

	if (status) {
		pr_debug("st54spi: nothing for minor %d\n", iminor(inode));
		goto err_find_dev;
	}

        if (debug_enabled) pr_info("st54spi: open\n");

	if (!st54spi->tx_buffer) {
		st54spi->tx_buffer = kmalloc(bufsiz, GFP_KERNEL);
		if (!st54spi->tx_buffer) {
			dev_dbg(&st54spi->spi->dev, "open/ENOMEM\n");
			status = -ENOMEM;
			goto err_find_dev;
		}
	}

	if (!st54spi->rx_buffer) {
		st54spi->rx_buffer = kmalloc(bufsiz, GFP_KERNEL);
		if (!st54spi->rx_buffer) {
			dev_dbg(&st54spi->spi->dev, "open/ENOMEM\n");
			status = -ENOMEM;
			goto err_alloc_rx_buf;
		}
	}

	st54spi->users++;
	filp->private_data = st54spi;
	nonseekable_open(inode, filp);

	mutex_unlock(&device_list_lock);
	return 0;

err_alloc_rx_buf:
	kfree(st54spi->tx_buffer);
	st54spi->tx_buffer = NULL;
err_find_dev:
	mutex_unlock(&device_list_lock);
	return status;
}

static int st54spi_release(struct inode *inode, struct file *filp)
{
	struct st54spi_data	*st54spi;

	mutex_lock(&device_list_lock);
	st54spi = filp->private_data;
	filp->private_data = NULL;

        if (debug_enabled) pr_info("st54spi: release\n");

	/* last close? */
	st54spi->users--;
	if (!st54spi->users) {
		int		dofree;

		kfree(st54spi->tx_buffer);
		st54spi->tx_buffer = NULL;

		kfree(st54spi->rx_buffer);
		st54spi->rx_buffer = NULL;

		spin_lock_irq(&st54spi->spi_lock);
		if (st54spi->spi)
			st54spi->speed_hz = st54spi->spi->max_speed_hz;

		/* ... after we unbound from the underlying device? */
		dofree = ((st54spi->spi == NULL)
			&& (st54spi->spi_reset == NULL));
		spin_unlock_irq(&st54spi->spi_lock);

		if (dofree)
			kfree(st54spi);
	}
	mutex_unlock(&device_list_lock);

	return 0;
}

static const struct file_operations st54spi_fops = {
	.owner =	THIS_MODULE,
	/* REVISIT switch to aio primitives, so that userspace
	 * gets more complete API coverage.  It'll simplify things
	 * too, except for the locking.
	 */
	.write =	st54spi_write,
	.read =		st54spi_read,
	.unlocked_ioctl = st54spi_ioctl,
	.compat_ioctl = st54spi_compat_ioctl,
	.open =		st54spi_open,
	.release =	st54spi_release,
	.llseek =	no_llseek,
};

/*-------------------------------------------------------------------------*/

/* The main reason to have this class is to make mdev/udev create the
 * /dev/st54spi character device nodes exposing our userspace API.
 * It also simplifies memory management.
 */

static struct class *st54spi_class;

#ifdef CONFIG_OF
static const struct of_device_id st54spi_dt_ids[] = {
	{ .compatible = "st,st54spi" },
	{},
};
MODULE_DEVICE_TABLE(of, st54spi_dt_ids);
#endif

#ifdef CONFIG_ACPI

/* Dummy SPI devices not to be used in production systems */
#define SPIDEV_ACPI_DUMMY	1

static const struct acpi_device_id st54spi_acpi_ids[] = {
	/*
	 * The ACPI SPT000* devices are only meant for development and
	 * testing. Systems used in production should have a proper ACPI
	 * description of the connected peripheral and they should also use
	 * a proper driver instead of poking directly to the SPI bus.
	 */
	{ "SPT0001", SPIDEV_ACPI_DUMMY },
	{ "SPT0002", SPIDEV_ACPI_DUMMY },
	{ "SPT0003", SPIDEV_ACPI_DUMMY },
	{},
};
MODULE_DEVICE_TABLE(acpi, st54spi_acpi_ids);

static void st54spi_probe_acpi(struct spi_device *spi)
{
	const struct acpi_device_id *id;

	if (!has_acpi_companion(&spi->dev))
		return;

	id = acpi_match_device(st54spi_acpi_ids, &spi->dev);
	if (WARN_ON(!id))
		return;

}
#else
static inline void st54spi_probe_acpi(struct spi_device *spi) {}
#endif

/*-------------------------------------------------------------------------*/
#define SPIS_DEBUG(fmt, args...) pr_info(fmt, ##args)

void spi_transfer_malloc(struct spi_transfer *trans)
{
	int i;

	trans->tx_buf = kzalloc(trans->len, GFP_KERNEL);
	trans->rx_buf = kzalloc(trans->len, GFP_KERNEL);

	for (i = 0; i < trans->len; i++)
		*((char *)trans->tx_buf + i) = i + 0x1;
}

void spi_transfer_free(struct spi_transfer *trans)
{
	kfree(trans->tx_buf);
	kfree(trans->rx_buf);
}

static void debug_packet(char *name, u8 *ptr, int len)
{
	int i;

	SPIS_DEBUG("%s: ", name);
	for (i = 0; i < len; i++)
		SPIS_DEBUG(" %02x", ptr[i]);
	 SPIS_DEBUG("\n");
}

int spi_loopback_check(struct spi_device *spi, struct spi_transfer *trans)
{
	int i, j, value, err = 0;
	struct mtk_chip_config *chip_config = spi->controller_data;

	for (i = 0; i < trans->len; i++) {
		value = 0;
		if (chip_config->tx_mlsb ^ chip_config->rx_mlsb) {
			for (j = 7; j >= 0; j--)
				value |= ((*((u8 *)trans->tx_buf + i)
					  & (1 << j)) >> j) << (7-j);
		} else {
			value = *((u8 *) trans->tx_buf + i);
		}

		if (value != *((char *) trans->rx_buf + i))
			err++;
	}

	if (err) {
		SPIS_DEBUG("spi_len:%d, err %d\n", trans->len, err);
		debug_packet("spi_tx_buf", (void *)trans->tx_buf, trans->len);
		debug_packet("spi_rx_buf", trans->rx_buf, trans->len);
		SPIS_DEBUG("spi test fail.");
	} else {
		SPIS_DEBUG("spi_len:%d, err %d\n", trans->len, err);
		SPIS_DEBUG("spi test pass.");
	}

	if (err)
		return -1;
	else
		return 0;
}

int spi_loopback_transfer(struct spi_device *spi, int len)
{
	struct spi_transfer trans;
	struct spi_message msg;
	int ret = 0;

	memset(&trans, 0, sizeof(struct spi_transfer));
	spi_message_init(&msg);

	trans.len = len;
	trans.cs_change = 0;
	spi_transfer_malloc(&trans);
	spi_message_add_tail(&trans, &msg);
	ret = spi_sync(spi, &msg);
	if (ret < 0)
		SPIS_DEBUG("Message transfer err,line(%d):%d\n", __LINE__, ret);
	spi_loopback_check(spi, &trans);
	spi_transfer_free(&trans);

	return ret;
}

static ssize_t spi_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	int len;
	struct spi_device *spi;

	spi = container_of(dev, struct spi_device, dev);

	if (!strncmp(buf, "-w", 2)) {
		struct mtk_chip_config *chip_config =
			kzalloc(sizeof(struct mtk_chip_config), GFP_KERNEL);

		buf += 3;
		chip_config->rx_mlsb = 0;
		chip_config->tx_mlsb = 0;
		spi->controller_data = (void *)chip_config;

		if (!strncmp(buf, "len=", 4) &&
		    (sscanf(buf + 4, "%d", &len) == 1)) {
			spi_loopback_transfer(spi, len);
		}
	}

	return count;
}

static DEVICE_ATTR(spi, 0200, NULL, spi_store);

static struct device_attribute *spi_attribute[] = {
	&dev_attr_spi,
};
static void spi_create_attribute(struct device *dev)
{
	int size, idx;

	size = ARRAY_SIZE(spi_attribute);
	for (idx = 0; idx < size; idx++)
		device_create_file(dev, spi_attribute[idx]);
}

#ifdef CONFIG_OF
static int st54spi_parse_dt(struct device *dev, struct st54spi_data	*pdata)
{
	int r = 0;
	struct device_node *np = dev->of_node;

	np = of_find_compatible_node(NULL, NULL, "st,st54spi");

	if (np) {
#if (!defined(CONFIG_MTK_GPIO) || defined(CONFIG_MTK_GPIOLIB_STAND))
		r = of_get_named_gpio(np, "gpio-power-std", 0);
		if (r < 0)
			pr_info("%s: get ST54 failed (%d)", __FILE__, r);
		else
			pdata->power_gpio = r;
		r = 0;
#else
		of_property_read_u32_array(np, "gpio-power", &(pdata->power_gpio),
					   1);
#endif
	} else {
		pr_info("%s : get num err.\n", __func__);
		return -1;
	}

	pr_info(
	    "[dsc]%s : get power_gpio[%d]\n",
	    __func__, pdata->power_gpio);
	return r;
}
#else
static int st54spi_parse_dt(struct device *dev, struct st21nfc_platform_data *pdata)
{
	return 0;
}
#endif

#ifdef WITH_RESET_CB
static void st54spi_st21nfc_reset_cb(int dir, void *data)
{
	struct st54spi_data	*st54spi = (struct st54spi_data	*)data;

	pr_info("%s : dir %d data %p\n", __func__, dir, st54spi);
	if (!st54spi)
		return;

	// if dir == 1, we are going to reset the CLF,
	// we need to:
	//  - stop VCC_SE
	//  - stop SPI_NSS  (if open drain ==> not needed anymore)
	// otherwise reset is complete and do the opposite.
	if (dir == 1) {
		if (st54spi->spi && st54spi->spi->controller_data) {
			struct mtk_chip_config *orig_chip_config =
				st54spi->spi->controller_data;
			struct mtk_chip_config st54spi_chip_info = {
				.rx_mlsb = orig_chip_config->rx_mlsb,
				.tx_mlsb = orig_chip_config->tx_mlsb,
				.cs_pol = 1 - orig_chip_config->cs_pol,
				.sample_sel = orig_chip_config->sample_sel,
			};
			struct spi_transfer	t = {
					.tx_buf		= NULL,
					.len		= 0,
					.speed_hz	= st54spi->speed_hz,
				};
			struct spi_message	m;

			spi_message_init(&m);
			spi_message_add_tail(&t, &m);

			pr_info("%s : change polarity to %d\n", __func__,
				st54spi_chip_info.cs_pol);
			st54spi->spi->controller_data = &st54spi_chip_info;
			st54spi_sync(st54spi, &m);
			st54spi->spi->controller_data = orig_chip_config;
		}
		if (st54spi->power_gpio)
			gpio_set_value(st54spi->power_gpio, 0);

		if (st54spi->spi) {
			spin_lock_irq(&st54spi->spi_lock);
			st54spi->spi_reset = st54spi->spi;
			st54spi->spi = NULL;
			spin_unlock_irq(&st54spi->spi_lock);
		}

		// Give time to the CLF to detect falling SE_PWR_REQ
		// and pull down the line before reset.
		usleep_range(2000, 4500);

	} else {
		// wait for the CLF to boot once nRESET is released
		usleep_range(4000, 8000);

		// set SE_PWR_REQ
		if (st54spi->power_gpio)
			gpio_set_value(st54spi->power_gpio, 1);

		// wait for the CLF to rise VCC_SE and the SE to boot
		usleep_range(1500, 2000);

		if (st54spi->spi_reset) {
			spin_lock_irq(&st54spi->spi_lock);
			st54spi->spi = st54spi->spi_reset;
			st54spi->spi_reset = NULL;
			spin_unlock_irq(&st54spi->spi_lock);
		}
		if (st54spi->spi) {
			struct spi_transfer	t = {
					.tx_buf		= NULL,
					.len		= 0,
					.speed_hz	= st54spi->speed_hz,
				};
			struct spi_message	m;

			spi_message_init(&m);
			spi_message_add_tail(&t, &m);

			pr_info("%s : restored polarity and force 1 xfer\n",
				__func__);
			st54spi_sync(st54spi, &m);
		}
	}
}
#endif

/* Change CS_TIME for ST54 */
// Unit is 1/109.2 us.
static struct mtk_chip_config st54spi_chip_info = {
	.cs_setuptime = 12012, // 110 us
};

static int st54spi_probe(struct spi_device *spi)
{
	struct st54spi_data	*st54spi;
	int			status, ret;
	unsigned long		minor;
	struct mtk_chip_config *chip_config = spi->controller_data;

	/*
	 * st54spi should never be referenced in DT without a specific
	 * compatible string, it is a Linux implementation thing
	 * rather than a description of the hardware.
	 */

	st54spi_probe_acpi(spi);

	/* Allocate driver data */
	st54spi = kzalloc(sizeof(*st54spi), GFP_KERNEL);
	if (!st54spi)
		return -ENOMEM;

	/* Initialize the driver data */
	st54spi->spi = spi;
	spin_lock_init(&st54spi->spi_lock);
	mutex_init(&st54spi->buf_lock);

	INIT_LIST_HEAD(&st54spi->device_entry);

	/* If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		struct device *dev;

		st54spi->devt = MKDEV(spidev_major, minor);
		dev = device_create(st54spi_class, &spi->dev, st54spi->devt,
				//    spidev, "spidev%d.%d",
				//    spi->master->bus_num, spi->chip_select);
				    st54spi, "st54spi");
		status = PTR_ERR_OR_ZERO(dev);
	} else {
		dev_dbg(&spi->dev, "no minor number available!\n");
		status = -ENODEV;
	}
	if (status == 0) {
		set_bit(minor, minors);
		list_add(&st54spi->device_entry, &device_list);
	}
	mutex_unlock(&device_list_lock);

	st54spi->speed_hz = spi->max_speed_hz;
	{
		/* fixed SPI clock speed: 109200000 */
		int period = DIV_ROUND_UP(109200000, st54spi->speed_hz);

		st54spi_chip_info.cs_idletime = period;
		st54spi_chip_info.cs_holdtime = period;
	}

	// set timings for ST54
	if (chip_config == NULL) {
		spi->controller_data = (void *)&st54spi_chip_info;
		dev_dbg(&spi->dev, "Replaced chip_info!\n");
	} else {
		chip_config->cs_setuptime = st54spi_chip_info.cs_setuptime;
		chip_config->cs_idletime = st54spi_chip_info.cs_idletime;
		chip_config->cs_holdtime = st54spi_chip_info.cs_holdtime;
		dev_dbg(&spi->dev, "Added into chip_info!\n");
	}

	if (status == 0)
		spi_set_drvdata(spi, st54spi);
	else
		kfree(st54spi);

	spi_create_attribute(&spi->dev);

	(void)st54spi_parse_dt(&spi->dev, st54spi);

	if (st54spi->power_gpio != 0) {
		ret = gpio_request(st54spi->power_gpio,
#if (!defined(CONFIG_MTK_GPIO) || defined(CONFIG_MTK_GPIOLIB_STAND))
				   "gpio-power-std"
#else
				   "gpio-power"
#endif
				   );
		if (ret) {
			pr_info("%s : power request failed (%d)\n", __FILE__, ret);
		}
		pr_info("%s : power GPIO = %d\n", __func__,
			st54spi->power_gpio);
		ret = gpio_direction_output(st54spi->power_gpio, 1);
		if (ret) {
			pr_info("%s : reset direction_output failed\n",
			       __FILE__);
		}
		/* active high */
		gpio_set_value(st54spi->power_gpio, 1);
	}
	
	#ifdef WITH_RESET_CB
	pr_info("%s : Register with st21nfc driver, %p\n", __func__, st54spi);
	st21nfc_register_reset_cb(st54spi_st21nfc_reset_cb, st54spi);
	#endif

	return status;
}

static int st54spi_remove(struct spi_device *spi)
{
	struct st54spi_data	*st54spi = spi_get_drvdata(spi);

	#ifdef WITH_RESET_CB
	pr_info("%s : Unregister from st21nfc driver\n", __func__);
	st21nfc_unregister_reset_cb();
	#endif

	/* make sure ops on existing fds can abort cleanly */
	spin_lock_irq(&st54spi->spi_lock);
	st54spi->spi = NULL;
	spin_unlock_irq(&st54spi->spi_lock);

	/* prevent new opens */
	mutex_lock(&device_list_lock);
	list_del(&st54spi->device_entry);
	device_destroy(st54spi_class, st54spi->devt);
	clear_bit(MINOR(st54spi->devt), minors);
	if (st54spi->users == 0)
		kfree(st54spi);
	mutex_unlock(&device_list_lock);

	return 0;
}

static struct spi_driver st54spi_spi_driver = {
	.driver = {
		.name =		"st54spi",
		.of_match_table = of_match_ptr(st54spi_dt_ids),
		.acpi_match_table = ACPI_PTR(st54spi_acpi_ids),
	},
	.probe =	st54spi_probe,
	.remove =	st54spi_remove,

	/* NOTE:  suspend/resume methods are not necessary here.
	 * We don't do anything except pass the requests to/from
	 * the underlying controller.  The refrigerator handles
	 * most issues; the controller driver handles the rest.
	 */
};

/*-------------------------------------------------------------------------*/

static int __init st54spi_init(void)
{
	int status;

	pr_info("Loading st54spi driver\n");

	/* Claim our 256 reserved device numbers.  Then register a class
	 * that will key udev/mdev to add/remove /dev nodes.  Last, register
	 * the driver which manages those device numbers.
	 */
	BUILD_BUG_ON(N_SPI_MINORS > 256);
	spidev_major = __register_chrdev(0, 0, N_SPI_MINORS, "spi",
		&st54spi_fops);
	pr_info("Loading st54spi driver, major: %d\n", spidev_major);

	st54spi_class = class_create(THIS_MODULE, "spidev");
	if (IS_ERR(st54spi_class)) {
		unregister_chrdev(spidev_major, st54spi_spi_driver.driver.name);
		return PTR_ERR(st54spi_class);
	}

	status = spi_register_driver(&st54spi_spi_driver);
	if (status < 0) {
		class_destroy(st54spi_class);
		unregister_chrdev(spidev_major, st54spi_spi_driver.driver.name);
	}
	pr_info("Loading st54spi driver: %d\n", status);
	return status;
}
module_init(st54spi_init);

static void __exit st54spi_exit(void)
{
	spi_unregister_driver(&st54spi_spi_driver);
	class_destroy(st54spi_class);
	unregister_chrdev(spidev_major, st54spi_spi_driver.driver.name);
}
module_exit(st54spi_exit);

MODULE_AUTHOR("Andrea Paterniani, <a.paterniani@swapp-eng.it>");
MODULE_DESCRIPTION("User mode SPI device interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:st54spi");
