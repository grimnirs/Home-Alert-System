#ifndef _KERNEL_VERSION_H_
#define _KERNEL_VERSION_H_

/* The template values come from cmake/modules/version.cmake
 * BUILD_VERSION related template values will be 'git describe',
 * alternatively user defined BUILD_VERSION.
 */

#define ZEPHYR_VERSION_CODE 262657
#define ZEPHYR_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))

#define KERNELVERSION                   0x4020100
#define KERNEL_VERSION_NUMBER           0x40201
#define KERNEL_VERSION_MAJOR            4
#define KERNEL_VERSION_MINOR            2
#define KERNEL_PATCHLEVEL               1
#define KERNEL_TWEAK                    0
#define KERNEL_VERSION_STRING           "4.2.1"
#define KERNEL_VERSION_EXTENDED_STRING  "4.2.1+0"
#define KERNEL_VERSION_TWEAK_STRING     "4.2.1+0"

#define BUILD_VERSION v4.2.1


#endif /* _KERNEL_VERSION_H_ */
