#undef __KERNEL__
#define __KERNEL__ /* We're part of the kernel */
#undef MODULE
#define MODULE     /* Not a permanent part, though. */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <asm/paravirt.h>
#include <linux/fs.h>       /* for register_chrdev */
#include <asm/uaccess.h>    /* for get_user and put_user */
#include <linux/string.h>   /* for memset. NOTE - not string.h!*/
#include "kci.h"            /*our shared header file*/
#include <linux/debugfs.h>  /* for logging messages */

#define LOG_WRITE_MSG(act,tot)  pr_debug("Write operation(PID: %d, FD: %d): %d bytes out of total %d were written to file\n", gpid, gfd, act, tot);
#define LOG_READ_MSG(act,tot)  pr_debug("Read operation(PID: %d, FD: %d): %d bytes out of total %d were read from file\n", gpid, gfd, act, tot);

unsigned long **sys_call_table;
unsigned long original_cr0;
static int gpid = -1;
static int gfd = -1;
static int cipher = 0;

static struct dentry *logfile;
static struct dentry *logdir;

/************** Interception functions *****************/

asmlinkage long (*ref_read) (unsigned int fd, char __user *buf, size_t count);
asmlinkage long (*ref_write)(unsigned int fd, const char __user *buf, size_t count);

int is_expected_file(int fd) {
    return (gpid == current->pid &&
            gfd == fd);
}

asmlinkage long encrypted_write(unsigned int fd, char __user *buf, size_t count) {
	int i;
    char c;
    int rc;

    //encrypt buffer for writing
    if(is_expected_file(fd) && cipher) {
        for(i=0; i<count; i++) {
            get_user(c, buf + i);
            c++;
            put_user(c, buf + i);
        }
        printk("special write. actual write <%s>\n", buf);
    }

    rc = ref_write(fd, buf, count);
    if(is_expected_file(fd) && cipher)
        LOG_WRITE_MSG(rc, (int)count);

    //decrypt back buffer so user won't notice any difference
    if(is_expected_file(fd) && cipher) {
        for(i=0; i<count; i++) {
            get_user(c, buf + i);
            c--;
            put_user(c, buf + i);
        }
    }

    return rc;
}

asmlinkage long decrypted_read (unsigned int fd, char __user *buf, size_t count) {
	int i, b_read;
	char c;

    b_read = ref_read(fd, buf, count);
    if(is_expected_file(fd) && cipher)
        LOG_READ_MSG(b_read, (int)count);

    //decrypt buffer for expected file
    if(is_expected_file(fd) && cipher) {
        printk("special read. actual read <%s>\n", buf);
        for(i=0; i<b_read; i++) {
            get_user(c, buf + i);
            c--;
            put_user(c, buf + i);
        }
    }

    return b_read;
}

static unsigned long **acquire_sys_call_table(void)
{
	unsigned long int offset = PAGE_OFFSET;
	unsigned long **sct;

	while (offset < ULLONG_MAX) {
		sct = (unsigned long **)offset;

		if (sct[__NR_close] == (unsigned long *) sys_close) 
			return sct;

		offset += sizeof(void *);
	}
	
	return NULL;
}


/************** Device related functions *****************/

static
long device_ioctl(struct file*   file,
                  unsigned int   ioctl_num,
                  unsigned long  ioctl_param)
{
    /* Switch according to the ioctl called */
    switch(ioctl_num) {
    case IOCTL_SET_PID:
        gpid = (int)ioctl_param;
        pr_debug("ioctl: pid set to %d\n", gpid);
        break;
    case IOCTL_SET_FD:
        gfd = (int)ioctl_param;
        pr_debug("ioctl: fd set to %d\n", gfd);
        break;
    case IOCTL_CIPHER:
        cipher = (int)ioctl_param;
        printk("ioctl: cipher %s\n", cipher ? "started":"stopped");
        break;
    }    

    return 0;
}


/************** Module Declarations *****************/
struct file_operations Fops = {
    .unlocked_ioctl = device_ioctl,
};

static int __init kci_kmod_init(void) 
{
	unsigned int rc = 0;

    /*create logger file using debugfs*/
	logdir = debugfs_create_dir(LOG_DIR, NULL);
	if (IS_ERR(logdir))
		return PTR_ERR(logdir);
	if (!logdir)
		return -ENOENT;

	logfile = debugfs_create_file(LOG_FILE, 0666, logdir, NULL, &Fops);
	if (!logfile) {
		debugfs_remove_recursive(logdir);
		return -ENOENT;
	}
    pr_debug("Started debug kci_kmod module\n");

    /*change default functions read, write*/
	if(!(sys_call_table = acquire_sys_call_table())) {
        pr_err("failed to acquire syscall table\n");
		return -1;
    }
	original_cr0 = read_cr0();
	write_cr0(original_cr0 & ~0x00010000);
	ref_read = (void *)sys_call_table[__NR_read];
	ref_write = (void *)sys_call_table[__NR_write];
	sys_call_table[__NR_read] = (unsigned long *)decrypted_read;
	sys_call_table[__NR_write] = (unsigned long *)encrypted_write;
	write_cr0(original_cr0);
	
    /*register a new character device */
    rc = register_chrdev(MAJOR_NUM, MODULE_NAME, &Fops);
    if (rc < 0) {
        printk(KERN_ALERT "%s failed with %d\n",
               "Sorry, registering the character device ", MAJOR_NUM);
        return -1;
    }

    printk("Module %s is loaded\n", MODULE_NAME);
	return 0;
}

static void __exit kci_kmod_exit(void) 
{
	if(!sys_call_table) {
		return;
	}

    /*set back old functions as default*/
	write_cr0(original_cr0 & ~0x00010000);
	sys_call_table[__NR_read] = (unsigned long *)ref_read;
	sys_call_table[__NR_write] = (unsigned long *)ref_write;
	write_cr0(original_cr0);
	
	msleep(2000);

    /* Unregister the character device */
    unregister_chrdev(MAJOR_NUM, MODULE_NAME);

    /* remove logger files recuresively */
	debugfs_remove_recursive(logdir);
	printk("module %s is removed\n", MODULE_NAME);
}

module_init(kci_kmod_init);
module_exit(kci_kmod_exit);

MODULE_LICENSE("GPL");
