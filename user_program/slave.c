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

struct dev_mmap_arg {
	size_t count;
	size_t offset;
};

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
        char *kernel_address = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_PRIVATE, dev_fd, 0);

        while(1) {
            size_t data_size = 0;
            struct dev_mmap_arg dm_arg;

            dm_arg.count = PAGE_SIZE;
            dm_arg.offset = 0;
            while (data_size < PAGE_SIZE) {
                int ret;
                if ((ret = ioctl(dev_fd, slave_IOCTL_MMAP, &dm_arg)) < 0) {
                    perror("option slave_IOCTL_MMAP failed");
                    return 1;
                }
                if (ret == 0)
                    break;

                data_size += ret;
                dm_arg.count -= ret;
                dm_arg.offset += ret;
            }
            if(data_size == 0)
                break;

            posix_fallocate(file_fd, file_size, data_size);
            char *file_address = NULL;
            file_address = mmap(NULL, data_size, PROT_WRITE, MAP_SHARED, file_fd, file_size);
            if (file_address == MAP_FAILED) {
                perror("file_address = MAP_FAILED");
                return 1;
            }

            memcpy(file_address, kernel_address, data_size);
            munmap(file_address, data_size);
            file_size += data_size;
        }
        ioctl(dev_fd, 0, kernel_address); // default option
        munmap(kernel_address, PAGE_SIZE);
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
