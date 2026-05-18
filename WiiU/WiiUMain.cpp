#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <unistd.h>
#include <fstream>

#include <wiiu/os/systeminfo.h>
#include <wiiu/os/thread.h>
#include <wiiu/os/debug.h>
#include <wiiu/gx2/event.h>
#include <wiiu/ax/core.h>
#include <iosuhax.h>
#include <iosuhax_devoptab.h>
#include <wiiu/ios.h>

#include <sys/stat.h>

#include "Common/Profiler/Profiler.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Display.h"
#include "Core/Core.h"
#include "Common/Log.h"

#include "Common/GraphicsContext.h"
#include "WiiU/WiiUHost.h"


const char *PROGRAM_NAME = "PPSSPP";
const char *PROGRAM_VERSION = "Wii U Autoboot";

static int g_QuitRequested;
void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "finish")) {
		g_QuitRequested = true;
		UpdateUIState(UISTATE_EXIT);
		Core_Stop();
	}
}

bool file_exists (const std::string &s)
{
  struct stat buffer;
  return (stat (s.c_str(), &buffer) == 0);
}
bool IsPathExist(const std::string &s)
{
  struct stat buffer;
  return (stat (s.c_str(), &buffer) == 0);
}

void bytes2hex(uint64_t input, char* output) {
    const char table[] = "0123456789ABCDEF";
    for(size_t i = 0, o = 15; i != 16; i++, o--) {
        output[o] = table[(input >> (i * 4)) & 0xF];
    }
	output[16] = '\0';
}

static std::string Trim(const std::string &value) {
	const size_t start = value.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
		return "";
	const size_t end = value.find_last_not_of(" \t\r\n");
	return value.substr(start, end - start + 1);
}

static bool EqualsIgnoreCase(const std::string &left, const std::string &right) {
	if (left.size() != right.size())
		return false;
	for (size_t i = 0; i < left.size(); i++) {
		char a = left[i];
		char b = right[i];
		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = (char)(b - 'A' + 'a');
		if (a != b)
			return false;
	}
	return true;
}

static bool EndsWithIgnoreCase(const std::string &value, const std::string &suffix) {
	if (suffix.size() > value.size())
		return false;
	return EqualsIgnoreCase(value.substr(value.size() - suffix.size()), suffix);
}

static bool ParseBool(const std::string &value) {
	return EqualsIgnoreCase(value, "1") ||
		EqualsIgnoreCase(value, "true") ||
		EqualsIgnoreCase(value, "yes") ||
		EqualsIgnoreCase(value, "on");
}

static std::string BuildInstalledContentPath(const char *volume, const char *titleIDHex, const char *fileName) {
	return std::string(volume) +
		":/usr/title/" +
		std::string(titleIDHex, 8) +
		"/" +
		std::string(titleIDHex + 8, 8) +
		"/content/" +
		fileName;
}

static std::string ResolveConfiguredPath(const std::string &value, const std::string &configPath) {
	if (value.find(":/") != std::string::npos)
		return value;

	const size_t slash = configPath.find_last_of('/');
	if (slash != std::string::npos)
		return configPath.substr(0, slash + 1) + value;

	return "sd:/ppsspp/" + value;
}

static bool ReadAutobootConfig(const std::string &path, std::string *gamePath, bool *copyToSd, std::ofstream &log) {
	std::ifstream file(path);
	if (!file.is_open())
		return false;

	if (log.is_open())
		log << "Using autoboot config: " << path << "\n";

	std::string line;
	while (std::getline(file, line)) {
		const size_t comment = line.find('#');
		if (comment != std::string::npos)
			line = line.substr(0, comment);
		line = Trim(line);
		if (line.empty())
			continue;

		const size_t equals = line.find('=');
		if (equals == std::string::npos) {
			*gamePath = ResolveConfiguredPath(line, path);
			continue;
		}

		const std::string key = Trim(line.substr(0, equals));
		const std::string value = Trim(line.substr(equals + 1));
		if (EqualsIgnoreCase(key, "iso") ||
			EqualsIgnoreCase(key, "game") ||
			EqualsIgnoreCase(key, "path") ||
			EqualsIgnoreCase(key, "rom")) {
			*gamePath = ResolveConfiguredPath(value, path);
		} else if (EqualsIgnoreCase(key, "copy_to_sd") ||
			EqualsIgnoreCase(key, "copyToSd") ||
			EqualsIgnoreCase(key, "copy")) {
			*copyToSd = ParseBool(value);
		}
	}

	return true;
}

static bool TryReadAutobootConfig(const char *titleIDHex, std::string *gamePath, bool *copyToSd, std::ofstream &log) {
	const std::string configPaths[] = {
		"sd:/wiiu/apps/ppsspp/autoboot.txt",
		"sd:/ppsspp/autoboot.txt",
		BuildInstalledContentPath("storage_usb", titleIDHex, "autoboot.txt"),
		BuildInstalledContentPath("storage_Nand", titleIDHex, "autoboot.txt")
	};

	for (const std::string &path : configPaths) {
		if (ReadAutobootConfig(path, gamePath, copyToSd, log))
			return true;
	}

	return false;
}

static std::string FindInstalledGamePath(const char *titleIDHex, std::ofstream &log) {
	const char *files[] = { "game.iso", "game.cso", "game.pbp" };
	const char *volumes[] = { "storage_usb", "storage_Nand" };

	for (const char *volume : volumes) {
		for (const char *fileName : files) {
			const std::string path = BuildInstalledContentPath(volume, titleIDHex, fileName);
			if (log.is_open())
				log << "Checking installed content path: " << path << "\n";
			if (file_exists(path))
				return path;
		}
	}

	return "";
}

static std::string BuildSdCachePathForSource(const std::string &sourcePath) {
	if (EndsWithIgnoreCase(sourcePath, ".cso"))
		return "sd:/ppsspp/game.cso";
	if (EndsWithIgnoreCase(sourcePath, ".pbp"))
		return "sd:/ppsspp/game.pbp";
	return "sd:/ppsspp/game.iso";
}

static bool CopyFileToSdCache(const std::string &sourcePath, const std::string &destinationPath, std::ofstream &log) {
	FILE *source = fopen(sourcePath.c_str(), "rb");
	if (!source) {
		if (log.is_open())
			log << "Failed to open source for SD copy: " << sourcePath << "\n";
		return false;
	}

	FILE *destination = fopen(destinationPath.c_str(), "wb");
	if (!destination) {
		if (log.is_open())
			log << "Failed to open destination for SD copy: " << destinationPath << "\n";
		fclose(source);
		return false;
	}

	const size_t bufferSize = 128 * 1024;
	void *buffer = std::malloc(bufferSize);
	if (!buffer) {
		if (log.is_open())
			log << "Failed to allocate copy buffer.\n";
		fclose(source);
		fclose(destination);
		return false;
	}

	bool ok = true;
	while (true) {
		const size_t read = fread(buffer, 1, bufferSize, source);
		if (read > 0 && fwrite(buffer, 1, read, destination) != read) {
			if (log.is_open())
				log << "Write error during SD copy.\n";
			ok = false;
			break;
		}

		if (read < bufferSize) {
			if (ferror(source)) {
				if (log.is_open())
					log << "Read error during SD copy.\n";
				ok = false;
			}
			break;
		}
	}

	std::free(buffer);
	fclose(source);
	fclose(destination);
	return ok;
}




int main(int argc, char **argv) {
	IOSUHAX_Open(NULL);
	int fsaHandle = IOSUHAX_FSA_Open();
	mount_fs("storage_Nand", fsaHandle, NULL, "/vol/storage_mlc01");
	mount_fs("storage_usb", fsaHandle, NULL, "/vol/storage_usb01");
	PROFILE_INIT();
	PPCSetFpIEEEMode();

	host = new WiiUHost();

	std::string app_name;
	std::string app_name_nice;
	std::string version;
	bool landscape;
	NativeGetAppInfo(&app_name, &app_name_nice, &landscape, &version);

	mkdir("sd:/ppsspp", 0777);
	std::ofstream fw("sd:/ppsspp/autoboot.log", std::ofstream::out);
	uint64_t tID;
	tID = OSGetTitleID();
	char titleIDHex[17];
	bytes2hex(tID, titleIDHex);
	if (fw.is_open())
		fw << "Title ID: " << titleIDHex << "\n";

	bool copyToSd = false;
	std::string gamePath;
	TryReadAutobootConfig(titleIDHex, &gamePath, &copyToSd, fw);

	if (!gamePath.empty() && !file_exists(gamePath)) {
		if (fw.is_open())
			fw << "Configured game path was not found: " << gamePath << "\n";
		gamePath.clear();
	}

	if (gamePath.empty())
		gamePath = FindInstalledGamePath(titleIDHex, fw);

	if (!gamePath.empty() && copyToSd) {
		const std::string sdCachePath = BuildSdCachePathForSource(gamePath);
		if (fw.is_open())
			fw << "copy_to_sd enabled. Copying " << gamePath << " to " << sdCachePath << "\n";
		if (CopyFileToSdCache(gamePath, sdCachePath, fw))
			gamePath = sdCachePath;
		else if (fw.is_open())
			fw << "SD copy failed; attempting direct boot path instead.\n";
	}

	if (fw.is_open()) {
		if (gamePath.empty())
			fw << "No PSP source found. Launching PPSSPP menu.\n";
		else
			fw << "Launching PSP source: " << gamePath << "\n";
	}

	const char *argv_[3] = {
		"sd:/ppsspp/PPSSPP.rpx",
		nullptr,
//		"-d",
//		"-v",
//		"-j",
//		"-r",
//		"-i",
//		"sd:/cube.elf",
		nullptr
	};
	if (!gamePath.empty())
		argv_[1] = gamePath.c_str();
	const int nativeArgc = gamePath.empty() ? 1 : 2;

	//arg size, arg, savegamedir, external dir, cache dir
	NativeInit(nativeArgc, argv_, "sd:/ppsspp/", "sd:/ppsspp/", nullptr);
#if 0
	UpdateScreenScale(854,480);
#else
	float dpi_scale = 1.0f;
	g_dpi = 96.0f;
	pixel_xres = 854;
	pixel_yres = 480;
	dp_xres = (float)pixel_xres * dpi_scale;
	dp_yres = (float)pixel_yres * dpi_scale;
	pixel_in_dps_x = (float)pixel_xres / dp_xres;
	pixel_in_dps_y = (float)pixel_yres / dp_yres;
	g_dpi_scale_x = dp_xres / (float)pixel_xres;
	g_dpi_scale_y = dp_yres / (float)pixel_yres;
	g_dpi_scale_real_x = g_dpi_scale_x;
	g_dpi_scale_real_y = g_dpi_scale_y;
#endif
	printf("Pixels: %i x %i\n", pixel_xres, pixel_yres);
	printf("Virtual pixels: %i x %i\n", dp_xres, dp_yres);

	g_Config.iPSPModel = PSP_MODEL_SLIM;
	g_Config.iGPUBackend = (int)GPUBackend::GX2;
	g_Config.bEnableSound = true;
	g_Config.bPauseExitsEmulator = false;
	g_Config.bPauseMenuExitsEmulator = false;
//	g_Config.iCpuCore = (int)CPUCore::JIT;
	g_Config.bVertexDecoderJit = false;
	g_Config.bSoftwareRendering = false;
//	g_Config.iFpsLimit = 0;
	g_Config.bHardwareTransform = true;
	g_Config.bSoftwareSkinning = false;
	g_Config.bVertexCache = true;
//	PSP_CoreParameter().gpuCore = GPUCORE_NULL;
//	g_Config.bTextureBackoffCache = true;
	std::string error_string;
	GraphicsContext *ctx;
	host->InitGraphics(&error_string, &ctx);
	NativeInitGraphics(ctx);
	NativeResized();

	host->InitSound();
	while (true) {
		if (g_QuitRequested)
			break;

		if (!Core_IsActive())
			UpdateUIState(UISTATE_MENU);
		Core_Run(ctx);
	}
	host->ShutdownSound();
	//unmount_fs("storage_Nand");
	//unmount_fs("storage_usb");
	NativeShutdownGraphics();
	NativeShutdown();

	return 0;
}

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
		return "Wii-U";
	case SYSPROP_LANGREGION:
		return "en_US";
	default:
		return "";
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60000; // internal refresh rate is always 59.940, even for PAL output.
	case SYSPROP_DISPLAY_XRES:
		return 854;
	case SYSPROP_DISPLAY_YRES:
		return 480;
	case SYSPROP_DEVICE_TYPE:
		return DEVICE_TYPE_TV;
	case SYSPROP_AUDIO_SAMPLE_RATE:
	case SYSPROP_AUDIO_OPTIMAL_SAMPLE_RATE:
		return 48000;
	case SYSPROP_AUDIO_FRAMES_PER_BUFFER:
	case SYSPROP_AUDIO_OPTIMAL_FRAMES_PER_BUFFER:
		return AX_FRAME_SIZE;
	case SYSPROP_SYSTEMVERSION:
	default:
		return -1;
	}
}
bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	default:
		return false;
	}
}

float System_GetPropertyFloat(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60.0f;
	case SYSPROP_DISPLAY_SAFE_INSET_LEFT:
	case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
	case SYSPROP_DISPLAY_SAFE_INSET_TOP:
	case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM:
		return 0.0f;
	default:
		return -1;
	}
}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

void LaunchBrowser(const char *url) {}
void ShowKeyboard() {}
void Vibrate(int length_ms) {}
