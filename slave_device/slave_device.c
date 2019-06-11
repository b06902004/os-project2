#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/net.h>
#include <net/sock.h>
#include <asm/processor.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/mm.h>
#include <asm/page.h>


#ifndef VM_RESERVED
#define VM_RESERVED   (VM_DONTEXPAND | VM_DONTDUMP)
#endif

#define slave_IOCTL_CREATESOCK 0x12345677
#define slave_IOCTL_MMAP 0x12345678
#define slave_IOCTL_EXIT 0x12345679

#define BUF_SIZE 512

struct dentry  *file1;//debug file

typedef struct socket * ksocket_t;

//functions about kscoket are exported,and thus we use extern here
extern ksocket_t ksocket(int domain, int type, int protocol);
extern int kconnect(ksocket_t socket, struct sockaddr *address, int address_len);
extern ssize_t krecv(ksocket_t socket, void *buffer, size_t length, int flags);
extern int kclose(ksocket_t socket);
extern unsigned int inet_addr(char* ip);
extern char *inet_ntoa(struct in_addr *in); //DO NOT forget to kfree the return pointer

static int __init slave_init(void);
static void __exit slave_exit(void);

int slave_close(struct inode *inode, struct file *filp);
int slave_open(struct inode *inode, struct file *filp);
static long slave_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param);
ssize_t receive_msg(struct file *filp, char *buf, size_t count, loff_t *offp );

void mmap_open(struct vm_area_struct *vma);
void mmap_close(struct vm_area_struct *vma);
static int mmap_fault(struct vm_fault *vmf);
int slave_mmap(struct file *filp, struct vm_area_struct *vma);

static mm_segment_t old_fs;
static ksocket_t sockfd_cli;//socket to the master server
static struct sockaddr_in addr_srv; //address of the master server

struct dev_mmap_arg {
	size_t count;
	size_t offset;
};
static struct dev_mmap_arg ioctl_arg;

struct mmap_info {
    char *data;
    int reference;
};
static struct mmap_info *mmap_buf; // pointer to the mapped data in this device

// file operations
static struct file_operations slave_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = slave_ioctl,
	.open = slave_open,
	.read = receive_msg,
	.release = slave_close,
    .mmap = slave_mmap
};

static struct vm_operations_struct mmap_vm_ops = {
    .open = mmap_open,
    .close = mmap_close,
    .fault = mmap_fault,    
};

// device info
static struct miscdevice slave_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "slave_device",
	.fops = &slave_fops
};

static int __init slave_init(void)
{
	int ret;
	file1 = debugfs_create_file("slave_debug", 0644, NULL, NULL, &slave_fops);

	//register the device
	if( (ret = misc_register(&slave_dev)) < 0){
		printk(KERN_ERR "misc_register failed!\n");
		return ret;
	}

	printk(KERN_INFO "slave has been registered!\n");

	return 0;
}

static void __exit slave_exit(void)
{
	misc_deregister(&slave_dev);
	printk(KERN_INFO "slave exited!\n");
	debugfs_remove(file1);
}


int slave_open(struct inode *inode, struct file *filp)
{
	struct mmap_info *info = kmalloc(sizeof(struct mmap_info), GFP_KERNEL);
    info->data = (char *) get_zeroed_page(GFP_KERNEL);
    filp->private_data = info; // assign this info struct to the file
	mmap_buf = info;
    return 0;
}
 
int slave_close(struct inode *inode, struct file *filp)
{
	struct mmap_info *info = filp->private_data;
    free_page((unsigned long)info->data);
    kfree(info);
    filp->private_data = NULL;
	mmap_buf = NULL;
    return 0;
}

static long slave_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param)
{
	long ret = -EINVAL;

	int addr_len ;
	unsigned int i;
	size_t len, data_size = 0;
	char *tmp, ip[20], buf[BUF_SIZE];
	struct page *p_print;
	unsigned char *px;

    pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
    pte_t *ptep, pte;
	old_fs = get_fs();
	set_fs(KERNEL_DS);

    printk("slave device ioctl");

	switch(ioctl_num){
		case slave_IOCTL_CREATESOCK:// create socket and connect to master
            printk("slave device ioctl create socket");

			if(copy_from_user(ip, (char*)ioctl_param, sizeof(ip)))
				return -ENOMEM;

			sprintf(current->comm, "ksktcli");

			memset(&addr_srv, 0, sizeof(addr_srv));
			addr_srv.sin_family = AF_INET;
			addr_srv.sin_port = htons(2325);
			addr_srv.sin_addr.s_addr = inet_addr(ip);
			addr_len = sizeof(struct sockaddr_in);

			sockfd_cli = ksocket(AF_INET, SOCK_STREAM, 0);
			printk("sockfd_cli = 0x%p  socket is created\n", sockfd_cli);
			if (sockfd_cli == NULL) {
				printk("socket failed");
				return -1;
			}
			if (kconnect(sockfd_cli, (struct sockaddr*)&addr_srv, addr_len) < 0) {
				printk("connect failed");
				return -1;
			}
			tmp = inet_ntoa(&addr_srv.sin_addr);
			printk("connected to : %s %d\n", tmp, ntohs(addr_srv.sin_port));
			kfree(tmp);
            printk("kfree(tmp)");
            ret = 0;
            break;
        case slave_IOCTL_MMAP:
            if (copy_from_user(&ioctl_arg, (const void *) ioctl_param,
                        sizeof(struct dev_mmap_arg)) != 0)
                return -1;
            if (ioctl_arg.offset + ioctl_arg.count > PAGE_SIZE)
                return -1;
            ret = krecv(sockfd_cli, mmap_buf->data + ioctl_arg.offset, ioctl_arg.count, 0);
            printk("Slave receives %lu bytes of data and writes to mmap buf\n", ret);
            break;
		case slave_IOCTL_EXIT:
			if (kclose(sockfd_cli) == -1) {
				printk("kclose cli error\n");
				return -1;
			}
			ret = 0;
			break;
		default:
            pgd = pgd_offset(current->mm, ioctl_param);
			p4d = p4d_offset(pgd, ioctl_param);
			pud = pud_offset(p4d, ioctl_param);
			pmd = pmd_offset(pud, ioctl_param);
			ptep = pte_offset_kernel(pmd , ioctl_param);
			pte = *ptep;
			printk("slave: %lX\n", pte);
			ret = 0;
			break;
	}
    set_fs(old_fs);

	return ret;
}

ssize_t receive_msg(struct file *filp, char *buf, size_t count, loff_t *offp )
{
//call when user is reading from this device
	char msg[BUF_SIZE];
	size_t len;
	len = krecv(sockfd_cli, msg, sizeof(msg), 0);
	if(copy_to_user(buf, msg, len))
		return -ENOMEM;
	return len;
}
void mmap_open(struct vm_area_struct *vma)
{
    struct mmap_info *info = (struct mmap_info *)vma->vm_private_data;
    info->reference++;
}
 
void mmap_close(struct vm_area_struct *vma)
{
    struct mmap_info *info = (struct mmap_info *)vma->vm_private_data;
    info->reference--;
}
 
static int mmap_fault(struct vm_fault *vmf)
{
    struct page *page;
    struct mmap_info *info;    
     
    info = (struct mmap_info *)vmf->vma->vm_private_data;
    if (info->data) {
        page = virt_to_page(info->data);      
    	get_page(page);
    	vmf->page = page;
    }
    return 0;
}
 
int slave_mmap(struct file *filp, struct vm_area_struct *vma)
{
    vma->vm_ops = &mmap_vm_ops;
    vma->vm_flags |= VM_RESERVED;    
    vma->vm_private_data = filp->private_data;
    mmap_open(vma);
    return 0;
}

module_init(slave_init);
module_exit(slave_exit);
MODULE_LICENSE("GPL");
