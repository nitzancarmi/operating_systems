#include "chardev.h"    

/* ***** Example w/ minimal error handling - for ease of reading ***** */

#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>

int main()
{
  int file_desc, ret_val;

  file_desc = open("/dev/"DEVICE_FILE_NAME, 0);
  if (file_desc < 0) {
    printf ("Can't open device file: %s\n", 
            DEVICE_FILE_NAME);
    exit(-1);
  }

  ret_val = ioctl(file_desc, IOCTL_SET_ENC, 1);

	if (ret_val < 0) {
	     printf ("ioctl_set_msg failed:%d\n", ret_val);
	     exit(-1);
	}	 
  close(file_desc); 
  return 0;
}
