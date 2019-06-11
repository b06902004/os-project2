#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define PAGE_SIZE sysconf(_SC_PAGE_SIZE)
#define BUF_SIZE 512

#define slave_IOCTL_CREATESOCK 0x12345677
#define slave_IOCTL_MMAP 0x12345678
#define slave_IOCTL_EXIT 0x12345679

int main (int argc, char* argv[])
{
    /* Read the parameters */
    if (argc != 4) {
        perror("usage: ./slave.out [file_name] [method] [ip address]\n");
        return 1;
    }
    char file_name[50], method[20], ip[20];
    strcpy(file_name, argv[1]);
    strcpy(method, argv[2]);
    strcpy(ip, argv[3]);

    /* Open the slave device and start measure transmission time */
    int dev_fd;
    struct timespec start;
    // should be O_RDWR for PROT_WRITE when mmap()
    if ((dev_fd = open("/dev/slave_device", O_RDWR)) < 0) {
        perror("failed to open /dev/slave_device\n");
        return 1;
    }
    clock_gettime(CLOCK_REALTIME, &start);

    /* Open the output file */
    int file_fd;
    if ((file_fd = open(file_name, O_RDWR | O_CREAT | O_TRUNC)) < 0) {
        perror("failed to open input file\n");
        return 1;
    }

    /* Ask slave device to connect to master device */
    if (ioctl(dev_fd, slave_IOCTL_CREATESOCK, ip) == -1) {
        perror("ioctl create slave socket error\n");
        return 1;
    }
    fprintf(stderr, "ioctl success\n");

    /* Read from slave device and write to output file */
    off_t file_size = 0;
    if (strcmp(method, "fcntl") == 0) {
        int ret;
        char buf[BUF_SIZE];
        do {
            if ((ret = read(dev_fd, buf, sizeof(buf))) < 0) {
                perror("read failed\n");
                return 1;
            }
            if (write(file_fd, buf, ret) < 0) {
                perror("write failed\n");
                return 1;
            }
            file_size += ret;
        } while (ret > 0);
    }
    else if (strcmp(method, "mmap") == 0) {
    }
    else {
        perror("method undefined\n");
        return 1;
    }

    /* Ask slave device to close the connection */
    if (ioctl(dev_fd, slave_IOCTL_EXIT) == -1) {
        perror("ioctl client exits error\n");
        return 1;
    }

    /* Close the slave device and stop measuring */
    close(dev_fd);
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);

    /* Show the transmission time */
    double trans_time;
    trans_time = (end.tv_sec - start.tv_sec) * 1e3 + (end.tv_nsec - start.tv_nsec) * 1e-6;
    printf("Transmission time: %lf ms, File size: %ld bytes\n", trans_time, file_size);

    return 0;
}
