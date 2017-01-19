/* Taken from:
 * https://bbs.archlinux.org/viewtopic.php?id=139406
 * */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <asm/paravirt.h>

/* ***** Example w/ minimal error handling - for ease of reading ***** */

unsigned long **sys_call_table;
unsigned long original_cr0;

asmlinkage long (*ref_mkdir)(const char __user *pathname, umode_t mode);

asmlinkage long new_mkdir(const char __user *pathname, umode_t mode)
{
	int i;
	long ret;
	char kpath_name[11];
	ret = ref_mkdir(pathname, mode);

  for (i = 0; i < 10; i++)
    get_user(kpath_name[i], pathname + i);
	
	kpath_name[10] = '\0';
  printk("Making folder: %s\n", kpath_name);

	return ret;
}

static unsigned long **aquire_sys_call_table(void)
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

static int __init interceptor_start(void) 
{
	if(!(sys_call_table = aquire_sys_call_table()))
		return -1;

  printk("Intercepting MKDIR syscall\n");	
	original_cr0 = read_cr0();

	write_cr0(original_cr0 & ~0x00010000);
	ref_mkdir = (void *)sys_call_table[__NR_mkdir];
	sys_call_table[__NR_mkdir] = (unsigned long *)new_mkdir;
	write_cr0(original_cr0);
	
	return 0;
}

static void __exit interceptor_end(void) 
{
	if(!sys_call_table) {
		return;
	}
	printk("Restoring MKDIR syscall\n");
	write_cr0(original_cr0 & ~0x00010000);
	sys_call_table[__NR_mkdir] = (unsigned long *)ref_mkdir;
	write_cr0(original_cr0);
	
	msleep(2000);
}

module_init(interceptor_start);
module_exit(interceptor_end);

MODULE_LICENSE("GPL");
