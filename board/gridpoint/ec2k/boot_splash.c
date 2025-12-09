#include <video.h>

#include "boot_splash.h"
#include "gridpoint_boot_splash-dark.h"

int show_gridpoint_boot_splash(void)
{
	return bmp_display((u_long)gridpoint_boot_splash_powering_up, 0, 0);
}

int show_gridpoint_boot_splash_booting(void)
{
	return bmp_display((u_long)gridpoint_boot_splash_system_starting, 0, 0);
}
