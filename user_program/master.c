#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h> /* clock_gettime */

#define PAGE_SIZE sysconf(_SC_PAGE_SIZE)
#define BUF_SIZE 512

off_t get_filesize(const char* filename); // get the size of the input file

int main (int argc, char* argv[])
{
    /* unused variables */
//    size_t offset = 0, tmp;
//    char *kernel_address = NULL, *file_address = NULL;

    /* Read the parameters */
    if (argc != 3) {
        perror("usage: ./master.out [file_name] [method]\n");
        return 1;
    }
    char file_name[50], method[20];
    strcpy(file_name, argv[1]);
    strcpy(method, argv[2]);

    /* Open the master device and start measure transmission time */
    int dev_fd;
    struct timespec start;
    if ((dev_fd = open("/dev/master_device", O_RDWR)) < 0) {
        perror("failed to open /dev/master_device\n");
        return 1;
    }
    clock_gettime(CLOCK_REALTIME, &start);

    /* Open the input file */
    int file_fd;
    if ((file_fd = open(file_name, O_RDWR)) < 0) {
        perror("failed to open input file\n");
        return 1;
    }

    /* Get the size of the input file */
    off_t file_size;
    if ((file_size = get_filesize(file_name)) < 0) {
        perror("failed to get file size\n");
        return 1;
    }

    /* Ask the master device to create a socket and listen */
    if (ioctl(dev_fd, 0x12345677) == -1) {
        perror("ioctl server create socket error\n");
        return 1;
    }

    /* Read from input file and write to master device */
    if (strcmp(method, "fcntl") == 0) {
        int ret;
        char buf[BUF_SIZE];
        do {
            if ((ret = read(file_fd, buf, sizeof(buf))) < 0) {
                perror("read failed\n");
                return 1;
            }
            if (write(dev_fd, buf, ret) < 0) {
                perror("write failed\n");
                return 1;
            }
        } while (ret > 0);
    }
    else if (strcmp(method, "mmap") == 0) {
        // TODO
    }
    else {
        perror("method undefined\n");
        return 1;
    }

    /* Ask the master device to close the connection */
    if (ioctl(dev_fd, 0x12345679) == -1) {
        perror("ioctl server exits error\n");
        return 1;
    }

    /* Close the master device and stop measuring */
    close(dev_fd);
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);

    /* Show the transmission time */
    double trans_time;
    trans_time = (end.tv_sec - start.tv_sec) * 1e3 + (end.tv_nsec - start.tv_nsec) * 1e-6;
    printf("Transmission time: %lf ms, File size: %ld bytes\n", trans_time, file_size);

    return 0;
}

off_t get_filesize(const char* filename)
{
    struct stat st;
    stat(filename, &st);
    return st.st_size;
}
