CFLAGS_kci_kmod.o := -DDEBUG

obj-m += kci_kmod.o

kci_kmod-objs := kci_kmod.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
