// SPDX-License-Identifier: GPL-2.0
/*
 * Avalon DMA tool
 *
 * Author: Alexander Gordeev <a.gordeev.box@gmail.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/mman.h>

#include <linux/avalon-ioctl.h>

#define DEV_NAME	"/dev/"AVALON_DEVICE_NAME
#define DMA_IN		"./dma.in"
#define DMA_OUT		"./dma.out"

static int print_mem(char *buf, size_t buf_len,
		     const void *mem, size_t mem_len)
{
	int ret, i, total = 0;

	if (buf_len < 3)
		return -EINVAL;

	if (mem_len > buf_len / 3)
		mem_len = buf_len / 3;

	for (i = 0; i < mem_len; i++) {
		ret = snprintf(buf + total, buf_len - total,
			 "%02X ", ((const unsigned char*)mem)[i]);
		if (ret < 0) {
			strcpy(buf, "--");
			return ret;
		}
		total += ret;
	}

	buf[total] = 0;

	return total;
}

static void dump_mem(void *data, size_t len)
{
	char buf[64];
	int n;

	n = snprintf(buf, sizeof(buf), "%p [ ", data);

	print_mem(buf + n, sizeof(buf) - n, data, len);

	printf("%s]\n", buf);
}

int main(int argc, char **argv)
{
	struct avalon_dma_info info;
	int dev, fd_rd, fd_wr;
	void *buf_rd = NULL, *buf_wr = NULL;
	size_t len_rd = 0, len_wr = 0;
	off_t off_rd = 0, off_wr = 0;
	struct iovec iovec[2];
	struct stat stat;
	int cmd = 0;
	char opt;

	while ((opt = getopt(argc, argv, "rwsRWSHD")) != -1) {
		switch (opt) {
		case 'r':
			cmd = IOCTL_AVALON_DMA_READ;
			break;
		case 'w':
			cmd = IOCTL_AVALON_DMA_WRITE;
			break;
		case 's':
			cmd = IOCTL_AVALON_DMA_RDWR;
			break;
		case 'R':
			cmd = IOCTL_AVALON_DMA_READ_SG;
			break;
		case 'W':
			cmd = IOCTL_AVALON_DMA_WRITE_SG;
			break;
		case 'H':
			cmd = IOCTL_AVALON_DMA_READ_SG_SMP;
			break;
		case 'D':
			cmd = IOCTL_AVALON_DMA_WRITE_SG_SMP;
			break;
		case 'S':
			cmd = IOCTL_AVALON_DMA_RDWR_SG;
			break;
		default:
			return -1;
		}
	}

	dev = open(DEV_NAME, O_RDWR);
	if (dev < 0) {
		fprintf(stderr, "open %s failed\n", DEV_NAME);
		return -1;
	}

	if (ioctl(dev, IOCTL_AVALON_DMA_GET_INFO, &info) < 0) {
		fprintf(stderr, "ioctl %x failed err %d\n", cmd, errno);
		return -1;
	}

	printf("\nmem_addr\t%08lx\nmem_size\t%08lx\ndma_size\t%08lx\ndma_size_sg\t%08lx\n", info.mem_addr, info.mem_size, info.dma_size, info.dma_size_sg);

	switch (cmd) {
	case IOCTL_AVALON_DMA_READ:
	case IOCTL_AVALON_DMA_WRITE:
	case IOCTL_AVALON_DMA_RDWR:
		len_rd = info.dma_size;
		len_wr = info.dma_size;
		break;

	case IOCTL_AVALON_DMA_READ_SG:
	case IOCTL_AVALON_DMA_READ_SG_SMP:
	case IOCTL_AVALON_DMA_WRITE_SG:
	case IOCTL_AVALON_DMA_WRITE_SG_SMP:
		len_rd = info.dma_size_sg;
		len_wr = info.dma_size_sg;
		break;

	case IOCTL_AVALON_DMA_RDWR_SG:
		len_rd = info.dma_size_sg / 2;
		len_wr = info.dma_size_sg / 2;
		off_rd = 0;
		off_wr = len_rd;
		break;
	}

	memset(iovec, 0, sizeof(iovec));

	switch (cmd) {
	case IOCTL_AVALON_DMA_READ:
		buf_rd = malloc(len_rd);
		if (!buf_rd) {
malloc_err:
			fprintf(stderr, "malloc() failed %d\n", errno);
			return -1;
		}

		iovec[0].iov_base = buf_rd;
		iovec[0].iov_len = len_rd;

		break;

	case IOCTL_AVALON_DMA_WRITE:
		buf_wr = malloc(len_wr);
		if (!buf_wr)
			goto malloc_err;

		iovec[0].iov_base = buf_wr;
		iovec[0].iov_len = len_wr;

		break;

	case IOCTL_AVALON_DMA_RDWR:
		buf_rd = malloc(len_rd);
		if (!buf_rd)
			goto malloc_err;

		buf_wr = malloc(len_wr);
		if (!buf_wr)
			goto malloc_err;

		iovec[0].iov_base = buf_rd;
		iovec[0].iov_len = len_rd;

		iovec[1].iov_base = buf_wr;
		iovec[1].iov_len = len_wr;

		break;

	case IOCTL_AVALON_DMA_READ_SG:
	case IOCTL_AVALON_DMA_READ_SG_SMP:
		buf_rd = mmap(NULL, len_rd, PROT_READ, MAP_SHARED, dev, off_rd);
		if (buf_rd == MAP_FAILED) {
mmap_err:
			fprintf(stderr, "mmap %s failed %d\n", DEV_NAME, errno);
			return -1;
		}

		iovec[0].iov_base = buf_rd;
		iovec[0].iov_len = len_rd;

		break;

	case IOCTL_AVALON_DMA_WRITE_SG:
	case IOCTL_AVALON_DMA_WRITE_SG_SMP:
		buf_wr = mmap(NULL, len_wr, PROT_WRITE, MAP_SHARED, dev, off_wr);
		if (buf_wr == MAP_FAILED)
			goto mmap_err;

		iovec[0].iov_base = buf_wr;
		iovec[0].iov_len = len_wr;

		break;

	case IOCTL_AVALON_DMA_RDWR_SG:
		buf_rd = mmap(NULL, len_rd, PROT_READ, MAP_SHARED, dev, off_rd);
		if (buf_rd == MAP_FAILED)
			goto mmap_err;
		
		buf_wr = mmap(NULL, len_wr, PROT_WRITE, MAP_SHARED, dev, off_wr);
		if (buf_wr == MAP_FAILED)
			goto mmap_err;

		iovec[0].iov_base = buf_rd;
		iovec[0].iov_len = len_rd;

		iovec[1].iov_base = buf_wr;
		iovec[1].iov_len = len_wr;

		break;
	}

	printf("\niov_base[0] %p iov_len[0] %lx\niov_base[1] %p iov_len[1] %lx\n",
		iovec[0].iov_base, iovec[0].iov_len,
		iovec[1].iov_base, iovec[1].iov_len);

	switch (cmd) {
	case IOCTL_AVALON_DMA_WRITE:
	case IOCTL_AVALON_DMA_WRITE_SG:
	case IOCTL_AVALON_DMA_WRITE_SG_SMP:
	case IOCTL_AVALON_DMA_RDWR:
	case IOCTL_AVALON_DMA_RDWR_SG:
		fd_wr = open(DMA_OUT, O_RDONLY);
		if (fd_wr < 0) {
			fprintf(stderr, "open %s failed\n", DMA_OUT);
			return -1;
		}
		if(fstat(fd_wr, &stat) < 0) {
			fprintf(stderr, "fstat %s failed\n", DMA_OUT);
			return -1;
		}
		if (stat.st_size < len_wr) {
			fprintf(stderr,
				"file to read is too small %s\n", DMA_OUT);
			return -1;
		}
		if (read(fd_wr, buf_wr, len_wr) != len_wr) {
			fprintf(stderr, "failed read file %s\n", DMA_OUT);
			return -1;
		}
		close(fd_wr);
	}

	if (buf_wr)
		dump_mem(buf_wr, len_wr);

	if (ioctl(dev, cmd, iovec) < 0) {
		fprintf(stderr, "ioctl %x failed err %d\n", cmd, errno);
		return -1;
	}

	if (buf_rd)
		dump_mem(buf_rd, len_wr);

	close(dev);

	switch (cmd) {
	case IOCTL_AVALON_DMA_READ:
	case IOCTL_AVALON_DMA_READ_SG:
	case IOCTL_AVALON_DMA_READ_SG_SMP:
	case IOCTL_AVALON_DMA_RDWR:
	case IOCTL_AVALON_DMA_RDWR_SG:
		fd_rd = creat(DMA_IN, S_IRUSR | S_IWUSR);
		if (fd_rd < 0) {
			fprintf(stderr, "open %s failed\n", DMA_IN);
			return -1;
		}
		if(fstat(fd_rd, &stat) < 0) {
			fprintf(stderr, "fstat %s failed\n", DMA_IN);
			return -1;
		}
		if (stat.st_size != 0) {
			fprintf(stderr,
				"file to write is not empty %s\n", DMA_IN);
			return -1;
		}
		if (write(fd_rd, buf_rd, len_rd) != len_rd) {
			fprintf(stderr, "failed write file %s\n", DMA_IN);
			return -1;
		}
		close(fd_rd);
	}

	return 0;
}

