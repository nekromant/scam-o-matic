#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>

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

static int s1, s2, s3;


void prandom_reset()
{
    s1 = 100;
    s2 = 200;
    s3 = 300;
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
        //     printf("0x%X vs 0x%X\r", a[i],b[i]);
        fflush(stdout);
        if (a[i] != b[i])
        {
            printf("\n\nOoops: 0x%X vs 0x%X\n\n", a[i],b[i]);
            return i;
        }
        i++;
    }
    return -1;
}

int main(int argc, char *argv[])
{
    uint64_t bsize;
    uint32_t blksize;
    int fd;
    char buf[64];
    int k=4096;
    char* dev;
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
    ioctl(fd, BLKGET64, &bsize);
    ioctl(fd, BLKSECSZ, &blksize);
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
    prandom_reset();
    int i;
    uint64_t pos=0;
    int scam = 0;
    uint64_t limit;
    uint32_t* writer_buf = malloc(k*blksize);
    uint32_t* reader_buf = malloc(k*blksize);

    while (pos < bsize)
    {
        printf("\rTesting at position: %" PRIu64 " (%" PRIu64 "%%)\t\t", pos, pos*100/bsize);
        fflush(stdout);
        prand_fill_buffer(writer_buf, k*blksize);
        pwrite(fd, writer_buf, k*blksize, pos);
        //fsync(fd);
        ioctl(fd, FLUSHCACHE);
        pread(fd, reader_buf, k*blksize, pos);
        i = check_data(reader_buf,writer_buf,k*blksize);
        if (i >= 0)
        {
            printf("\r\nMismatch at %" PRIu64 " detected\n",pos+i*4);
            scam=1;
            break;
        }
        pos+=k*blksize;
        //write(fd, writer_buf, blksize);
    }
    if (scam)
    {
        printf("Sorry, dude, but it look like you've been scammed.\n");
        printf("Or you might just have a old'n'corrupt card.\n");
        printf("In case of scam you still have %" PRIu64 " usable bytes\n",pos+(i-1)*4);
        printf("That we can salvage. Let me double check the area for overwrites\n");
        limit = pos+(i-1)*4;
        pos =0;
        prandom_reset();
        while (pos<limit)
        {
            printf("\rDouble checking at position: %" PRIu64 "/%" PRIu64 "\t\t", pos,limit );
            fflush(stdout);
            prand_fill_buffer(writer_buf, k*blksize);
            pread(fd, reader_buf, k*blksize, pos);
            i = check_data(reader_buf, writer_buf, k*blksize);
            if (i >= 0 && (pos+i*4 < limit))
            {
                printf("\r\nMismatch at %" PRIu64 " detected\n",pos+i*4);
                scam=2;
                break;
            }
            pos += k*blksize;
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
    bzero(writer_buf, k*blksize);
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

