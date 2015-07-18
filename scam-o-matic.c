#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>

//#define MACOSX

/*
    http://www.opensource.apple.com/source/xnu/xnu-1456.1.26/bsd/sys/disk.h
*/
#ifdef MACOSX
#include <sys/disk.h>
#define BLKGET64 DKIOCGETBLOCKCOUNT
#define BLKSECSZ DKIOCGETBLOCKSIZE
#define FLUSHCACHE DKIOCSYNCHRONIZECACHE
#else
#include <linux/fs.h>
#define BLKGET64 BLKGETSIZE64
#define BLKSECSZ BLKSSZGET
#define FLUSHCACHE BLKFLSBUF
#endif // #ifdef MACOSX

/* shamelessly ripped from linux kernel */

static int start_s1, start_s2, start_s3, seed_init;
static int s1, s2, s3;

uint32_t* writer_buf;
uint32_t* reader_buf;
int bad_pos_in_buffer;

void prandom_reset()
{
    if (!seed_init) {
        start_s1 = time(NULL);
        start_s2 = 200;
        start_s3 = 300;
        seed_init = 1;

        int fd = open("/dev/urandom", O_RDONLY);
        if (fd >= 0) {
            read(fd, &start_s2, sizeof(start_s2));
            read(fd, &start_s3, sizeof(start_s3));
            close(fd);
        }
    }

    s1 = start_s1;
    s2 = start_s2;
    s3 = start_s3;
}

uint32_t prandom32()
{
#define TAUSWORTHE(s,a,b,c,d) ((s&c)<<d) ^ (((s <<a) ^ s)>>b)

    s1 = TAUSWORTHE(s1, 13, 19, 4294967294UL, 12);
    s2 = TAUSWORTHE(s2, 2, 25, 4294967288UL, 4);
    s3 = TAUSWORTHE(s3, 3, 11, 4294967280UL, 17);

    return (s1 ^ s2 ^ s3);
}

void prand_fill_buffer(uint32_t* buffer, int size)
{
    int i=size/4;
    while (i--)
    {
        buffer[i] = prandom32();
    }
}

int check_data(uint32_t* a, uint32_t* b, size_t size)
{
    size_t i=0;

    while (i < size/4)
    {
        //     printf("0x%X vs 0x%X\r", a[i],b[i]); fflush(stdout);
        if (a[i] != b[i])
        {
            printf("\n\nOoops: 0x%X vs 0x%X\n\n", a[i],b[i]);
            return i;
        }
        i++;
    }
    return -1;
}

int do_write(int fd, uint32_t cur_step_size, uint64_t pos)
{
    prand_fill_buffer(writer_buf, cur_step_size);
    int ret = pwrite(fd, writer_buf, cur_step_size, pos);
    if (ret < 0) {
        printf("Error writing to device at pos %" PRIu64 ", errno=%d (%m)\n", pos, errno);
        return -1;
    }
    if (ret != cur_step_size) {
        printf("Failed to write the expected number of bytes at pos %" PRIu64 " tried to write %u but wrote %d\n",
                pos, cur_step_size, ret);
        return -1;
    }
    return 0;
}

int do_read_verify(int fd, uint32_t cur_step_size, uint64_t pos)
{
    int ret = pread(fd, reader_buf, cur_step_size, pos);
    if (ret < 0) {
        printf("Error reading from device at pos %" PRIu64 ", errno=%d (%m)\n", pos, errno);
        return -1;
    }
    prand_fill_buffer(writer_buf, cur_step_size);
    bad_pos_in_buffer = check_data(reader_buf, writer_buf, cur_step_size);
    if (bad_pos_in_buffer >= 0)
    {
        printf("\r\nMismatch at %" PRIu64 " detected\n",pos + bad_pos_in_buffer*4);
        return -1;
    }

    return 0;
}

int scan_device(int fd, uint32_t step_size, uint64_t bsize, const char *name, int (*cb)(int fd, uint32_t cur_step_size, uint64_t pos), uint64_t *err_pos)
{
    uint64_t pos = 0;

    while (pos < bsize)
    {
        const uint32_t cur_step_size = (bsize - pos >= step_size) ? step_size : bsize - pos;

        printf("\r%s at position: %" PRIu64 " (%" PRIu64 "%%)\t\t", name, pos, pos*100/bsize);
        fflush(stdout);
        int ret = cb(fd, cur_step_size, pos);
        if (ret < 0) {
            if (err_pos)
                *err_pos = pos;
            return -1;
        }

        pos += cur_step_size;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    uint64_t bsize;
    uint32_t blksize;
    int fd;
    char buf[64];
    int k=4096;
    char* dev;
    int ret;
    int scam = 0;

    if (argc<2)
    {
        printf("Salvage a scammed device for usable space\n");
        printf("Usage: %s device\n", argv[0]);
        printf("(c) Necromant 2011-2012\n");
        exit(1);
    }

    dev = argv[1];

    fd = open(dev, O_RDWR|O_DSYNC);
    if (fd<0)
    {
        perror("Failed to open device: ");
        exit(1);
    }

    printf("!!!WARNING!!! I will now destroy all data on the device %s\n", dev);
    printf("!!!WARNING!!! If you are ok with that - type OK & press enter\n");

    fgets(buf, sizeof(buf), stdin);
    if (strcmp("OK\n",buf)!=0)
    {
        printf("Not doing anything\n");
        return 1;
    }
    printf("Rock'n'roll, then!\n");

    ret = ioctl(fd, BLKGET64, &bsize);
    if (ret < 0) {
        printf("Error getting the block device size: %d (%m)\n", errno);
        return 1;
    }

    ret = ioctl(fd, BLKSECSZ, &blksize);
    if (ret < 0) {
        printf("Error getting the block device size: %d (%m)\n", errno);
        return 1;
    }

    printf("Device reports to be %" PRIu64 " bytes long.\n", bsize);
    printf("Sectors are presumably %u bytes each.\n", blksize);
    printf("!!!WARNING!!! Last chance to stop. Are you sure you want to go further?\n If so - type YES, anything else or ctrl+c either\n");
    fgets(buf, sizeof(buf), stdin);
    if (strcmp("YES\n",buf)!=0)
    {
        printf("Not doing anything\n");
        return 1;
    }
    printf("Starting a destructive surface test\n");
    const uint32_t step_size = k * blksize;
    writer_buf = malloc(step_size);
    reader_buf = malloc(step_size);

    prandom_reset();
    ret = scan_device(fd, step_size, bsize, "Writing", do_write, NULL);
    if (ret < 0) {
        printf("Sorry but failing to write at all means the device is doomed (or you pulled it in the middle)!\n");
        return 2;
    }

    ioctl(fd, FLUSHCACHE);

    prandom_reset();
    uint64_t pos;
    ret = scan_device(fd, step_size, bsize, "Verifying", do_read_verify, &pos);
    if (ret != 0)
        scam = 1;

    uint64_t limit;

    if (scam)
    {
        printf("Sorry, dude, but it look like you've been scammed.\n");
        printf("Or you might just have a old'n'corrupt card.\n");
        printf("In case of scam you still have %" PRIu64 " usable bytes\n",pos+(bad_pos_in_buffer-1)*4);
        printf("That we can salvage. Let me double check the area for overwrites\n");
        limit = pos + (bad_pos_in_buffer-1) * 4;
        pos =0;
        prandom_reset();
        while (pos<limit)
        {
            printf("\rDouble checking at position: %" PRIu64 "/%" PRIu64 " (%" PRIu64 "%%)\t\t", pos, limit, pos*100/bsize);
            fflush(stdout);
            prand_fill_buffer(writer_buf, step_size);
            pread(fd, reader_buf, step_size, pos);
            int i = check_data(reader_buf, writer_buf, step_size);
            if (i >= 0 && (pos+i*4 < limit))
            {
                printf("\r\nMismatch at %" PRIu64 " detected\n",pos+i*4);
                scam=2;
                break;
            }
            pos += step_size;
        }
        if (scam==2)
        {
            printf("Results somewhat unreliable, think for yourself\n");
            printf("You may run the whole thing for another loop\n");
            printf("See if it gets better.");
        }else
        {
            printf("The region looks fine. That's %d%% of reported capacity.\n", (int)(limit/bsize*100));
        }
    } else
    {
        printf("Card looks fine - have fun\n");
    }

    printf("Clearing first sector...\n");
    bzero(writer_buf, step_size);
    pwrite(fd, writer_buf, 512, 0);

    if (scam==1)
    {
        printf("Would you like me to run fdisk and\n");
        printf("Automagically create a partition, that will use only\n");
        printf("the really avaliable space? (YEP/NOPE)");
        fgets(buf, sizeof(buf), stdin);
        if (strcmp(buf,"YEP\n")==0)
        {
            int sectcount = limit/blksize-2048;
            printf("Creating partition with %d sectors. Command line below\n",sectcount);
            char fdiskbuf[1024];
            sprintf(fdiskbuf,"(echo n; echo p; echo 1; echo 2048; echo +%d; echo w)|fdisk %s\n",sectcount,dev);
            printf(fdiskbuf);
            system(fdiskbuf);
            printf("I've done all I could. Good Bye.\n");
            printf("Some rights reserved. (c) Necromant 2011\n");
        }
    }

    return 0;
}

