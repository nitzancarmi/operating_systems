/* Taken from:
 * https://bbs.archlinux.org/viewtopic.php?id=139406
 * */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <asm/paravirt.h>

#define FALSE   0
#define TRUE    1
#define MIN(x, y)       (((x) < (y)) ? (x) : (y))

unsigned long **sys_call_table;
unsigned long original_cr0;
static int gpid = -1;
static int gfd = -1;
static int cipher = 0;

asmlinkage long (*ref_read) (unsigned int fd, char __user *buf, size_t count);
asmlinkage long (*ref_write)(unsigned int fd, const char __user *buf, size_t count);

int is_expected_file(int fd) {
    return (gpid == current->pid &&
            gfd == fd);
}
asmlinkage long encrypted_write(unsigned int fd, const char __user *buf, size_t count) {
	int i;
    int offset;
	int b_write = 0;
	char buf_enc[256];

    if(is_expected_file(fd) && cipher) {
        while(count > 0) {
            for(i=0; i<MIN(256, count); i++) {
                get_user(buf_enc[i], buf + offset);
                buf_enc[i] += 1;
                offset++;
            }
            count -= 256;
            b_write = ref_write(fd, buf_enc, 256);
            if (b_write < 0) {
                pr_err("failed to write encrypted buffer to file");
                return -1;
            }
        }
    }
    else
        return ref_write(fd, buf, count);
        
    printk("Writing to fd %d\n", fd);
    return 0;
}

asmlinkage long decrypted_read (unsigned int fd, char __user *buf, size_t count) {
	int i;
	int b_read = 0;
    int eff_size = MIN(4096, count);
	char buf_dec[4096];

    if(is_expected_file(fd) && cipher) {
        b_read = ref_read(fd, buf_dec, eff_size);
        if (b_read < 0) {
            pr_err("failed to read encrypted buffer from file");
            return b_read;
        }
        for(i=0; i<b_read; i++) {
            buf_dec[i]--;
            put_user(buf_dec[i], buf + i);
        }
    }
    else
        return ref_read(fd, buf, count);
        
    printk("reading from fd %d\n", fd);
    return 0;
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

static int __init kci_kmod_init(void) 
{
	if(!(sys_call_table = acquire_sys_call_table()))
		return -1;

    printk("initializing file encryptor/decryptor\n");	
	original_cr0 = read_cr0();

	write_cr0(original_cr0 & ~0x00010000);
	ref_read = (void *)sys_call_table[__NR_read];
	ref_write = (void *)sys_call_table[__NR_write];
	sys_call_table[__NR_read] = (unsigned long *)decrypted_read;
	sys_call_table[__NR_write] = (unsigned long *)encrypted_write;
	write_cr0(original_cr0);
	
	return 0;
}

static void __exit kci_kmod_exit(void) 
{
	if(!sys_call_table) {
		return;
	}
	printk("exiting file encryptor/decryptor\n");
	write_cr0(original_cr0 & ~0x00010000);
	sys_call_table[__NR_read] = (unsigned long *)ref_read;
	sys_call_table[__NR_write] = (unsigned long *)ref_write;
	write_cr0(original_cr0);
	
	msleep(2000);
}

module_init(kci_kmod_init);
module_exit(kci_kmod_exit);

MODULE_LICENSE("GPL");
