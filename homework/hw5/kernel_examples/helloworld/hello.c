/* Declare what kind of code we want from the header files
   Defining __KERNEL__ and MODULE allows us to access kernel-level 
   code not usually available to userspace programs. */
#undef __KERNEL__
#define __KERNEL__ /* We're part of the kernel */
#undef MODULE
#define MODULE     /* Not a permanent part, though. */
 
 /* ***** Example w/ minimal error handling - for ease of reading ***** */
 
#include <linux/module.h>    // included for all kernel modules
#include <linux/init.h>        // included for __init and __exit macros

MODULE_LICENSE("GPL");


// loader 
static int __init hello_init(void)
{
    printk("Hello world!\n");
    return 0;    // Non-zero return means that the module couldn't be loaded.
}
 
// unloader
static void __exit hello_cleanup(void)
{
    printk("Goodbye cruel world! (Cleaning up hello module).\n");
}
 
module_init(hello_init);
module_exit(hello_cleanup);

