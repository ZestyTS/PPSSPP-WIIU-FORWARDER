
#include <stdlib.h>
#include <stdio.h>
#include <fat.h>
#include <iosuhax.h>
#include <wiiu/ios.h>
#include <wiiu/os/thread.h>
#include <wiiu/os/memory.h>
#include <wiiu/os/debug.h>
#include <wiiu/os/systeminfo.h>
#include <wiiu/sysapp.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "fs_utils.h"
#include "sd_fat_devoptab.h"
#include "exception_handler.h"

void wiiu_log_init(void);
void wiiu_log_deinit(void);
int main(int argc, char **argv);

void __eabi(void) {}

extern void (*const __CTOR_LIST__[])(void);
extern void (*const __CTOR_END__[])(void);
extern void (*const __DTOR_LIST__[])(void);
extern void (*const __DTOR_END__[])(void);

void __init(void) {
	void (*const *ctor)(void) = __CTOR_LIST__;
	while (ctor < __CTOR_END__)
		(*ctor++)();
}

void __fini(void) {
	void (*const *dtor)(void) = __DTOR_LIST__;
	while (dtor < __DTOR_END__)
		(*dtor++)();
}

/* libiosuhax related */

// just to be able to call async
void someFunc(void *arg) { (void)arg; }

static int mcp_hook_fd = -1;

int MCPHookOpen(void) {
	// take over mcp thread
	mcp_hook_fd = IOS_Open("/dev/mcp", 0);

	if (mcp_hook_fd < 0)
		return -1;

	IOS_IoctlAsync(mcp_hook_fd, 0x62, (void *)0, 0, (void *)0, 0, (void *)someFunc, (void *)0);
	// let wupserver start up
	OSSleepTicks(ms_to_ticks(1));

	if (IOSUHAX_Open("/dev/mcp") < 0) {
		IOS_Close(mcp_hook_fd);
		mcp_hook_fd = -1;
		return -1;
	}

	return 0;
}

void MCPHookClose(void) {
	if (mcp_hook_fd < 0)
		return;

	// close down wupserver, return control to mcp
	IOSUHAX_Close();
	// wait for mcp to return
	OSSleepTicks(ms_to_ticks(1));
	IOS_Close(mcp_hook_fd);
	mcp_hook_fd = -1;
}

static int iosuhaxMount = 0;
static int fsRootMount = 0;
static int sdMount = 0;
static FILE *entryLog = NULL;

static void entry_log_open(void) {
	if (entryLog)
		return;
	mkdir("sd:/uinjectforge", 0777);
	mkdir("sd:/uinjectforge/ppsspp", 0777);
	entryLog = fopen("sd:/uinjectforge/ppsspp/entry.log", "w");
	if (!entryLog)
		entryLog = fopen("fs:/vol/save/ppsspp-entry.log", "w");
}

static void entry_log_stage(const char *stage) {
	if (!entryLog)
		return;
	fprintf(entryLog, "Stage: %s\n", stage);
	fflush(entryLog);
}


void entry_log_crash(const char *message) {
	if (!entryLog)
		return;
	fprintf(entryLog, "Fatal exception:\n%s\n", message ? message : "(no exception text)");
	fflush(entryLog);
}
static void entry_log_close(void) {
	if (!entryLog)
		return;
	fclose(entryLog);
	entryLog = NULL;
}

static int is_standalone_installable(void) {
	uint64_t title_id = OSGetTitleID();
	return (uint32_t)(title_id >> 32) == 0x00050002;
}

static void fsdev_init(void) {
	int sdResult;
	int fsResult;

	iosuhaxMount = 0;
	fsRootMount = 0;
	sdMount = 0;
	if (!OSIsHLE() && !is_standalone_installable()) {
		int res = IOSUHAX_Open(NULL);

		if (res < 0)
			res = MCPHookOpen();

		if (res >= 0) {
			iosuhaxMount = 1;
			fatInitDefault();
			sdMount = 1;
			entry_log_open();
			entry_log_stage("IOSUHAX storage initialized");
			return;
		}
	}

	sdResult = mount_sd_fat("sd");
	if (sdResult == 0)
		sdMount = 1;
	entry_log_open();
	if (entryLog) {
		fprintf(entryLog, "SD mount result: %d\n", sdResult);
		fflush(entryLog);
	}

	fsResult = mount_wiiu_fs_root("fs");
	if (fsResult == 0)
		fsRootMount = 1;
	entry_log_open();
	if (entryLog) {
		fprintf(entryLog, "Installed-title root mount result: %d\n", fsResult);
		fflush(entryLog);
	}
}
static void fsdev_exit(void) {
	if (iosuhaxMount) {
		fatUnmount("sd:");
		fatUnmount("usb:");

		if (mcp_hook_fd >= 0)
			MCPHookClose();
		else
			IOSUHAX_Close();
		return;
	}
	if (fsRootMount)
		unmount_wiiu_fs_root("fs");
	if (sdMount)
		unmount_sd_fat("sd");
}

__attribute__((noreturn)) void __shutdown_program(void) {
	entry_log_stage("shutdown requested");
	entry_log_close();
	fsdev_exit();
	memoryRelease();
	wiiu_log_deinit();
	SYSRelaunchTitle(0, 0);
	exit(0);
}

__attribute__((noreturn))
void __rpx_start(int argc, char **argv) {
	setup_os_exceptions();
	socket_lib_init();
	wiiu_log_init();
	DEBUG_LINE();
	fsdev_init();
	entry_log_open();
	entry_log_stage("storage initialized");
	entry_log_stage("before memory initialization");
	if (!memoryInitialize()) {
		const char *error = memoryInitializationError();
		if (entryLog) {
			fprintf(entryLog, "Memory initialization failed: %s\n", error ? error : "unknown error");
			fflush(entryLog);
		}
		__shutdown_program();
	}
	entry_log_stage("after memory initialization");

	DEBUG_VAR(iosuhaxMount);
	DEBUG_VAR(fsRootMount);
	DEBUG_VAR(mcp_hook_fd);
	DEBUG_VAR2(MEM1_avail());
	DEBUG_VAR2(MEM2_avail());
	DEBUG_VAR2(MEMBucket_avail());
	entry_log_stage("before C++ constructors");
	__init();
	entry_log_stage("after C++ constructors");
	entry_log_stage("before main");
	main(argc, argv);
	entry_log_stage("after main");
	__fini();
	entry_log_stage("after C++ destructors");

	deinit_os_exceptions();
	__shutdown_program();
}

__attribute__((noreturn)) void abort(void) {
	entry_log_stage("abort called");
	printf("Abort called\n");
	DEBUG_VAR(MEM2_avail());
	fflush(stdout);
	__shutdown_program();
}
