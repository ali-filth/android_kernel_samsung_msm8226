/* drivers/video/msm/logo.c
 *
 * Show Logo in RLE 565 format
 *
 * Copyright (C) 2008 Google Incorporated
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/unistd.h>
#include <linux/syscalls.h>

#include <linux/irq.h>
#include <asm/system.h>

#define fb_width(fb)	((fb)->var.xres)
#define fb_height(fb)	((fb)->var.yres)
#define fb_size(fb)	((fb)->var.xres * (fb)->var.yres * 2)

#ifdef CONFIG_SAMSUNG_LPM_MODE
extern int poweroff_charging;
#endif

static void memset16(void *_ptr, unsigned short val, unsigned count)
{
	unsigned short *ptr = _ptr;
	count >>= 1;
	while (count--)
		*ptr++ = val;
}


/* convert RGB565 to RBG8888 */
static int total_pixel = 1;
static int memset16_rgb8888(void *_ptr, unsigned short val, unsigned count,
				struct fb_info *fb)
{
	unsigned short *ptr = _ptr;
	unsigned short red;
	unsigned short green;
	unsigned short blue;
	int need_align = (fb->fix.line_length >> 2) - fb->var.xres;
	int align_amount = need_align << 1;
	int pad = 0;

	red = (val & 0xF800) >> 8;
	green = (val & 0x7E0) >> 3;
	blue = (val & 0x1F) << 3;

	count >>= 1;
	while (count--) {
		*ptr++ = (green << 8) | red;
		*ptr++ = blue;

		if (need_align) {
			if (!(total_pixel % fb->var.xres)) {
				ptr += align_amount;
				pad++;
			}
		}

		total_pixel++;
	}

	return pad * align_amount;
}

/* 565RLE image format: [count(2 bytes), rle(2 bytes)] */
int load_565rle_image(char *filename)
{
	struct fb_info *info;
	int fd, count, err = 0;
	unsigned max;
	unsigned short *data, *bits, *ptr;
	int pad;

	info = registered_fb[0];
	if (!info) {
		printk(KERN_WARNING "%s: Can not access framebuffer\n",
			__func__);
		return -ENODEV;
	}

	fd = sys_open(filename, O_RDONLY, 0);
	if (fd < 0) {
		printk(KERN_WARNING "%s: Can not open %s\n",
			__func__, filename);
		return -ENOENT;
	}
	count = sys_lseek(fd, (off_t)0, 2);
	if (count <= 0) {
		err = -EIO;
		goto err_logo_close_file;
	}
	sys_lseek(fd, (off_t)0, 0);
	data = kmalloc(count, GFP_KERNEL);
	if (!data) {
		printk(KERN_WARNING "%s: Can not alloc data\n", __func__);
		err = -ENOMEM;
		goto err_logo_close_file;
	}
	if (sys_read(fd, (char *)data, count) != count) {
		err = -EIO;
		goto err_logo_free_data;
	}

	max = fb_width(info) * fb_height(info);
	ptr = data;
	if ((info->node == 1 || info->node == 2)) {
		err = -EPERM;
		pr_err("%s:%d no info->creen_base on fb%d!\n",
		       __func__, __LINE__, info->node);
		goto err_logo_free_data;
	}
	bits = (unsigned short *)(info->screen_base);
	while (count > 3) {
		unsigned n = ptr[0];
		if (n > max)
			break;
		if (info->var.bits_per_pixel >= 24) {
			pad = memset16_rgb8888(bits, ptr[1], n << 1, info);
			bits += n << 1;
			bits += pad;
		} else {
			memset16(bits, ptr[1], n << 1);
			bits += n;
		}
		max -= n;
		ptr += 2;
		count -= 4;
	}

err_logo_free_data:
	kfree(data);
err_logo_close_file:
	sys_close(fd);
	return err;
}
EXPORT_SYMBOL(load_565rle_image);


int load_samsung_boot_logo(void)
{
	struct fb_info *info;

	info = registered_fb[0];
	if (!info) {
		printk(KERN_WARNING "%s: Can not access framebuffer\n",
			__func__);
		return -ENODEV;
	}

#ifdef CONFIG_SAMSUNG_LPM_MODE
	// LPM mode : no boot logo
	if(poweroff_charging)
		return 0;
#endif

	info->fbops->fb_open(registered_fb[0], 0);
	if (load_565rle_image("initlogo.rle")) {
		char *bits = info->screen_base;
		int i = 0;
		unsigned int max = fb_height(info) * fb_width(info);
		unsigned int val = 0xff;
		for (i = 0; i < 3; i++) {
			int count = max/3;
			while (count--) {
				unsigned int *addr = \
					(unsigned int *) bits;
				*addr = val;
				bits += 4;
			}
			val <<= 8;
		}
	}
	fb_pan_display(info, &info->var);
	return 0;
}
EXPORT_SYMBOL(load_samsung_boot_logo);
