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
#include <wiiu/procui.h>
#include <iosuhax.h>
#include <iosuhax_devoptab.h>
#include <wiiu/ios.h>

#include <sys/stat.h>

#include "Common/Profiler/Profiler.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Display.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Common/Log.h"

#include "Common/GraphicsContext.h"
#include "WiiU/WiiUHost.h"


extern "C" int wiiu_sd_is_mounted(void);
extern "C" int wiiu_fs_root_is_mounted(void);


const char *PROGRAM_NAME = "PPSSPP";
const char *PROGRAM_VERSION = "Wii U Autoboot";

static int g_QuitRequested;

static void LogStage(std::ofstream &log, const char *stage) {
	if (!log.is_open())
		return;

	log << "Stage: " << stage << "\n";
	log.flush();
}

void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "finish")) {
		g_QuitRequested = true;
		UpdateUIState(UISTATE_EXIT);
		Core_Stop();
	}
}

bool WiiUProcessSystemMessages() {
	const ProcUIStatus status = ProcUIProcessMessages(false);
	switch (status) {
	case PROCUI_STATUS_IN_FOREGROUND:
		return true;
	case PROCUI_STATUS_RELEASE_FOREGROUND:
		ProcUIDrawDoneRelease();
		return false;
	case PROCUI_STATUS_EXITING:
		System_SendMessage("finish", "");
		return false;
	case PROCUI_STATUS_IN_BACKGROUND:
	default:
		return false;
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

static void EnsureDirectoryIfMissing(const char *path) {
	struct stat info;
	if (stat(path, &info) != 0)
		mkdir(path, 0777);
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

static std::string NormalizePerformanceProfile(const std::string &value) {
	if (EqualsIgnoreCase(value, "custom"))
		return "custom";

	if (EqualsIgnoreCase(value, "compat") ||
		EqualsIgnoreCase(value, "compatibility") ||
		EqualsIgnoreCase(value, "safe"))
		return "compatibility";

	if (EqualsIgnoreCase(value, "balanced") ||
		EqualsIgnoreCase(value, "default"))
		return "balanced";

	if (EqualsIgnoreCase(value, "fast") ||
		EqualsIgnoreCase(value, "speed"))
		return "fast";

	if (EqualsIgnoreCase(value, "max") ||
		EqualsIgnoreCase(value, "maxspeed") ||
		EqualsIgnoreCase(value, "max_speed") ||
		EqualsIgnoreCase(value, "maximum"))
		return "max_speed";

	return "compatibility";
}

struct WiiUPerformanceOverrides {
	bool hasInternalResolution = false;
	int internalResolution = 1;
	bool hasFrameSkip = false;
	int frameSkip = 1;
	bool hasAutoFrameSkip = false;
	bool autoFrameSkip = true;
	bool hasEnableSound = false;
	bool enableSound = true;
	bool hasBlockTransferGpu = false;
	bool blockTransferGpu = true;
	bool hasDisableSlowFramebufEffects = false;
	bool disableSlowFramebufEffects = true;
	bool hasSplineBezierQuality = false;
	int splineBezierQuality = 0;
};

static int ClampInt(int value, int minValue, int maxValue) {
	if (value < minValue)
		return minValue;
	if (value > maxValue)
		return maxValue;
	return value;
}

static bool ParseInt(const std::string &value, int *parsed) {
	if (!parsed || value.empty())
		return false;
	char *end = nullptr;
	const long result = std::strtol(value.c_str(), &end, 10);
	if (end == value.c_str() || *end != '\0')
		return false;
	*parsed = (int)result;
	return true;
}

static void ApplyWiiUPerformanceProfile(const std::string &profile, const WiiUPerformanceOverrides &overrides, std::ofstream &log) {
	const std::string normalized = NormalizePerformanceProfile(profile);
	if (log.is_open())
		log << "Applying performance profile: " << normalized << "\n";

	g_Config.iPSPModel = PSP_MODEL_SLIM;
	g_Config.iGPUBackend = (int)GPUBackend::GX2;
	g_Config.iCpuCore = (int)CPUCore::JIT;
	g_Config.bFastMemory = true;
	g_Config.bFuncReplacements = true;
	g_Config.bSeparateIOThread = true;
	g_Config.iIOTimingMethod = IOTIMING_FAST;
	g_Config.bEnableSound = true;
	g_Config.bPauseExitsEmulator = false;
	g_Config.bPauseMenuExitsEmulator = false;
	g_Config.bSoftwareRendering = false;
	g_Config.bHardwareTransform = true;
	g_Config.bSoftwareSkinning = false;
	g_Config.bVertexCache = true;
	g_Config.bVertexDecoderJit = false;
	g_Config.iInternalResolution = 1;
	g_Config.iTexFiltering = 1;
	g_Config.iBufFilter = SCALE_NEAREST;
	g_Config.iAnisotropyLevel = 0;
	g_Config.iTexScalingLevel = 1;
	g_Config.bTexDeposterize = false;
	g_Config.bTexHardwareScaling = false;
	g_Config.bVSync = false;
	g_Config.bReplaceTextures = false;
	g_Config.bSaveNewTextures = false;
	g_Config.iBloomHack = 0;
	g_Config.bHardwareTessellation = false;
	g_Config.bRenderDuplicateFrames = false;
	g_Config.iInflightFrames = 2;
	g_Config.bClearFramebuffersOnFirstUseHack = true;

	if (normalized == "compatibility") {
		// The Wii U port has no usable software presentation path. Keep the
		// interpreter for a conservative CPU probe, but always present through
		// GX2 so menu-only diagnostics exercise the same renderer as Nico's
		// hardware-proven Tiramisu build.
		g_Config.iCpuCore = (int)CPUCore::INTERPRETER;
		g_Config.bFastMemory = false;
		g_Config.bSeparateIOThread = false;
		g_Config.bSoftwareRendering = false;
		g_Config.bHardwareTransform = true;
		g_Config.bSoftwareSkinning = false;
		g_Config.bVertexCache = false;
		g_Config.iFrameSkip = 1;
		g_Config.bAutoFrameSkip = true;
		g_Config.iSplineBezierQuality = 2;
		g_Config.bBlockTransferGPU = false;
		g_Config.bDisableSlowFramebufEffects = false;
		g_Config.bFragmentTestCache = false;
	} else if (normalized == "balanced") {
		g_Config.iFrameSkip = 0;
		g_Config.bAutoFrameSkip = false;
		g_Config.iSplineBezierQuality = 1;
		g_Config.bBlockTransferGPU = true;
		g_Config.bDisableSlowFramebufEffects = false;
		g_Config.bFragmentTestCache = true;
	} else if (normalized == "max_speed") {
		g_Config.iFrameSkip = 2;
		g_Config.bAutoFrameSkip = true;
		g_Config.iSplineBezierQuality = 0;
		g_Config.bBlockTransferGPU = false;
		g_Config.bDisableSlowFramebufEffects = true;
		g_Config.bFragmentTestCache = false;
		g_Config.bEnableSound = false;
	} else {
		g_Config.iFrameSkip = 1;
		g_Config.bAutoFrameSkip = true;
		g_Config.iSplineBezierQuality = 0;
		g_Config.bBlockTransferGPU = true;
		g_Config.bDisableSlowFramebufEffects = true;
		g_Config.bFragmentTestCache = true;
	}

	if (overrides.hasInternalResolution) {
		g_Config.iInternalResolution = ClampInt(overrides.internalResolution, 1, 4);
		if (log.is_open())
			log << "Custom internal_resolution=" << g_Config.iInternalResolution << "\n";
	}
	if (overrides.hasFrameSkip) {
		g_Config.iFrameSkip = ClampInt(overrides.frameSkip, 0, 5);
		if (log.is_open())
			log << "Custom frame_skip=" << g_Config.iFrameSkip << "\n";
	}
	if (overrides.hasAutoFrameSkip) {
		g_Config.bAutoFrameSkip = overrides.autoFrameSkip;
		if (log.is_open())
			log << "Custom auto_frame_skip=" << (g_Config.bAutoFrameSkip ? "true" : "false") << "\n";
	}
	if (overrides.hasEnableSound) {
		g_Config.bEnableSound = overrides.enableSound;
		if (log.is_open())
			log << "Custom enable_sound=" << (g_Config.bEnableSound ? "true" : "false") << "\n";
	}
	if (overrides.hasBlockTransferGpu) {
		g_Config.bBlockTransferGPU = overrides.blockTransferGpu;
		if (log.is_open())
			log << "Custom block_transfer_gpu=" << (g_Config.bBlockTransferGPU ? "true" : "false") << "\n";
	}
	if (overrides.hasDisableSlowFramebufEffects) {
		g_Config.bDisableSlowFramebufEffects = overrides.disableSlowFramebufEffects;
		if (log.is_open())
			log << "Custom disable_slow_framebuf_effects=" << (g_Config.bDisableSlowFramebufEffects ? "true" : "false") << "\n";
	}
	if (overrides.hasSplineBezierQuality) {
		g_Config.iSplineBezierQuality = ClampInt(overrides.splineBezierQuality, 0, 2);
		if (log.is_open())
			log << "Custom spline_bezier_quality=" << g_Config.iSplineBezierQuality << "\n";
	}

	if (log.is_open()) {
		log << "CPU core: " << (g_Config.iCpuCore == (int)CPUCore::INTERPRETER ? "interpreter" : "JIT") << "\n"
			<< "PSP renderer: " << (g_Config.bSoftwareRendering ? "software" : "GX2") << "\n";
		log.flush();
	}
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

static std::string BuildInstalledContentRoot(const char *volume, const char *titleIDHex) {
	return std::string(volume) +
		":/usr/title/" +
		std::string(titleIDHex, 8) +
		"/" +
		std::string(titleIDHex + 8, 8) +
		"/content/";
}

static std::string EnsureTrailingSlash(const std::string &value) {
	if (value.empty() || value[value.size() - 1] == '/')
		return value;
	return value + "/";
}

static std::string ResolveConfiguredPath(const std::string &value, const std::string &configPath) {
	if (value.find(":/") != std::string::npos)
		return value;

	const size_t slash = configPath.find_last_of('/');
	if (slash != std::string::npos)
		return configPath.substr(0, slash + 1) + value;

	return "sd:/ppsspp/" + value;
}

static bool ReadAutobootConfig(const std::string &path, std::string *gamePath, bool *copyToSd, bool *menuOnly, std::string *performanceProfile, WiiUPerformanceOverrides *overrides, std::ofstream &log) {
	std::ifstream file(path);
	if (!file.is_open())
		return false;

	if (log.is_open())
		log << "Using autoboot config: " << path << "\n";

	bool handled = false;
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
			handled = true;
			continue;
		}

		const std::string key = Trim(line.substr(0, equals));
		const std::string value = Trim(line.substr(equals + 1));
		if (EqualsIgnoreCase(key, "iso") ||
			EqualsIgnoreCase(key, "game") ||
			EqualsIgnoreCase(key, "path") ||
			EqualsIgnoreCase(key, "rom")) {
			*gamePath = ResolveConfiguredPath(value, path);
			handled = true;
		} else if (EqualsIgnoreCase(key, "copy_to_sd") ||
			EqualsIgnoreCase(key, "copyToSd") ||
			EqualsIgnoreCase(key, "copy")) {
			*copyToSd = ParseBool(value);
			handled = true;
		} else if (menuOnly && (EqualsIgnoreCase(key, "runtime_probe") ||
			EqualsIgnoreCase(key, "menu_only") ||
			EqualsIgnoreCase(key, "menuOnly"))) {
			*menuOnly = ParseBool(value);
			handled = true;
		} else if (menuOnly && (EqualsIgnoreCase(key, "launch_mode") ||
			EqualsIgnoreCase(key, "launchMode"))) {
			*menuOnly = EqualsIgnoreCase(value, "menu") ||
				EqualsIgnoreCase(value, "probe") ||
				EqualsIgnoreCase(value, "runtime_probe");
			handled = true;
		} else if (EqualsIgnoreCase(key, "performance_profile") ||
			EqualsIgnoreCase(key, "performanceProfile") ||
			EqualsIgnoreCase(key, "profile") ||
			EqualsIgnoreCase(key, "speed_profile")) {
			*performanceProfile = NormalizePerformanceProfile(value);
			handled = true;
		} else if (overrides && (EqualsIgnoreCase(key, "internal_resolution") ||
			EqualsIgnoreCase(key, "internalResolution") ||
			EqualsIgnoreCase(key, "render_resolution") ||
			EqualsIgnoreCase(key, "renderResolution"))) {
			int parsed = 0;
			if (ParseInt(value, &parsed)) {
				overrides->internalResolution = ClampInt(parsed, 1, 4);
				overrides->hasInternalResolution = true;
				handled = true;
			}
		} else if (overrides && (EqualsIgnoreCase(key, "frame_skip") ||
			EqualsIgnoreCase(key, "frameSkip") ||
			EqualsIgnoreCase(key, "frameskip"))) {
			int parsed = 0;
			if (ParseInt(value, &parsed)) {
				overrides->frameSkip = ClampInt(parsed, 0, 5);
				overrides->hasFrameSkip = true;
				handled = true;
			}
		} else if (overrides && (EqualsIgnoreCase(key, "auto_frame_skip") ||
			EqualsIgnoreCase(key, "autoFrameSkip") ||
			EqualsIgnoreCase(key, "auto_frameskip"))) {
			overrides->autoFrameSkip = ParseBool(value);
			overrides->hasAutoFrameSkip = true;
			handled = true;
		} else if (overrides && (EqualsIgnoreCase(key, "enable_sound") ||
			EqualsIgnoreCase(key, "enableSound") ||
			EqualsIgnoreCase(key, "sound"))) {
			overrides->enableSound = ParseBool(value);
			overrides->hasEnableSound = true;
			handled = true;
		} else if (overrides && (EqualsIgnoreCase(key, "block_transfer_gpu") ||
			EqualsIgnoreCase(key, "blockTransferGpu"))) {
			overrides->blockTransferGpu = ParseBool(value);
			overrides->hasBlockTransferGpu = true;
			handled = true;
		} else if (overrides && (EqualsIgnoreCase(key, "disable_slow_framebuf_effects") ||
			EqualsIgnoreCase(key, "disableSlowFramebufEffects"))) {
			overrides->disableSlowFramebufEffects = ParseBool(value);
			overrides->hasDisableSlowFramebufEffects = true;
			handled = true;
		} else if (overrides && (EqualsIgnoreCase(key, "spline_bezier_quality") ||
			EqualsIgnoreCase(key, "splineBezierQuality"))) {
			int parsed = 0;
			if (ParseInt(value, &parsed)) {
				overrides->splineBezierQuality = ClampInt(parsed, 0, 2);
				overrides->hasSplineBezierQuality = true;
				handled = true;
			}
		}
	}

	if (!handled && log.is_open())
		log << "Autoboot config had no recognized settings; continuing fallback search.\n";

	return handled;
}

static bool TryReadAutobootConfig(const char *titleIDHex, std::string *gamePath, bool *copyToSd, bool *menuOnly, std::string *performanceProfile, WiiUPerformanceOverrides *overrides, std::ofstream &log) {
	const std::string configPaths[] = {
		"fs:/vol/content/autoboot.txt",
		BuildInstalledContentPath("storage_usb", titleIDHex, "autoboot.txt"),
		BuildInstalledContentPath("storage_Nand", titleIDHex, "autoboot.txt"),
		"sd:/wiiu/apps/ppsspp/autoboot.txt",
		"sd:/ppsspp/autoboot.txt"
	};

	for (const std::string &path : configPaths) {
		if (ReadAutobootConfig(path, gamePath, copyToSd, menuOnly, performanceProfile, overrides, log))
			return true;
	}

	return false;
}

static bool HasInstalledPackageContent(bool fsAvailable) {
	if (!fsAvailable)
		return false;

	const char *files[] = {
		"autoboot.txt",
		"game.iso",
		"game.cso",
		"game.pbp",
		"assets/ppge_atlas.zim",
		"assets/ui_atlas.zim",
		"assets/langregion.ini",
		"assets/flash0/font/ltn0.pgf"
	};

	for (const char *fileName : files) {
		const std::string path = std::string("fs:/vol/content/") + fileName;
		if (file_exists(path))
			return true;
	}

	return false;
}

static std::string SelectWritableRoot(bool installedPackage, bool fsAvailable, bool sdAvailable) {
	if (installedPackage && fsAvailable) {
		mkdir("fs:/vol/save/ppsspp", 0777);
		if (file_exists("fs:/vol/save/ppsspp"))
			return "fs:/vol/save/ppsspp/";
	}

	if (sdAvailable) {
		mkdir("sd:/ppsspp", 0777);
		return "sd:/ppsspp/";
	}

	return "";
}

static bool OpenAutobootDiagnosticLog(const std::string &fallbackRoot, bool sdAvailable, std::ofstream *log, std::string *logPath) {
	if (!log || !logPath)
		return false;

	if (sdAvailable) {
		mkdir("sd:/uinjectforge", 0777);
		mkdir("sd:/uinjectforge/ppsspp", 0777);
		*logPath = "sd:/uinjectforge/ppsspp/autoboot.log";
		log->open(*logPath, std::ofstream::out | std::ofstream::trunc);
		if (log->is_open()) {
			log->setf(std::ios::unitbuf);
			return true;
		}
		log->clear();
	}

	if (fallbackRoot.empty())
		return false;
	*logPath = EnsureTrailingSlash(fallbackRoot) + "autoboot.log";
	log->open(*logPath, std::ofstream::out | std::ofstream::trunc);
	if (log->is_open())
		log->setf(std::ios::unitbuf);
	return log->is_open();
}

static std::string FindInstalledGamePath(const char *titleIDHex, std::ofstream &log) {
	const char *files[] = { "game.iso", "game.cso", "game.pbp" };
	for (const char *fileName : files) {
		const std::string path = std::string("fs:/vol/content/") + fileName;
		if (log.is_open())
			log << "Checking package content path: " << path << "\n";
		if (file_exists(path))
			return path;
	}

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

static std::string FindRuntimeRoot(const char *titleIDHex, std::ofstream &log) {
	const std::string packageRoot = "fs:/vol/content/";
	const std::string packagePpgeAtlas = packageRoot + "assets/ppge_atlas.zim";
	const std::string packageUiAtlas = packageRoot + "assets/ui_atlas.zim";
	const std::string packageLangRegion = packageRoot + "assets/langregion.ini";
	const std::string packageFlash0Font = packageRoot + "assets/flash0/font/ltn0.pgf";
	if (log.is_open())
		log << "Checking package-local PPSSPP runtime root: " << packageRoot << "\n";
	if (file_exists(packagePpgeAtlas) || file_exists(packageUiAtlas) || file_exists(packageLangRegion) || file_exists(packageFlash0Font)) {
		if (log.is_open())
			log << "Using package-local runtime root: " << packageRoot << "\n";
		return packageRoot;
	}

	const char *volumes[] = { "storage_usb", "storage_Nand" };

	for (const char *volume : volumes) {
		const std::string root = BuildInstalledContentRoot(volume, titleIDHex);
		const std::string ppgeAtlas = root + "assets/ppge_atlas.zim";
		const std::string uiAtlas = root + "assets/ui_atlas.zim";
		const std::string langRegion = root + "assets/langregion.ini";
		const std::string flash0Font = root + "assets/flash0/font/ltn0.pgf";
		if (log.is_open())
			log << "Checking installed PPSSPP runtime root: " << root << "\n";
		if (file_exists(ppgeAtlas) || file_exists(uiAtlas) || file_exists(langRegion) || file_exists(flash0Font)) {
			if (log.is_open())
				log << "Using installed content runtime root: " << root << "\n";
			return root;
		}
	}

	if (log.is_open())
		log << "Installed content assets were not found; falling back to sd:/ppsspp/.\n";
	return "sd:/ppsspp/";
}

static std::string BuildSdCachePathForSource(const std::string &sourcePath) {
	if (EndsWithIgnoreCase(sourcePath, ".cso"))
		return "sd:/ppsspp/game.cso";
	if (EndsWithIgnoreCase(sourcePath, ".pbp"))
		return "sd:/ppsspp/game.pbp";
	return "sd:/ppsspp/game.iso";
}

static bool CopyFileToSdCache(const std::string &sourcePath, const std::string &destinationPath, std::ofstream &log) {
	EnsureDirectoryIfMissing("sd:/ppsspp");

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
	PROFILE_INIT();
	PPCSetFpIEEEMode();

	host = new WiiUHost();

	std::string app_name;
	std::string app_name_nice;
	std::string version;
	bool landscape;
	NativeGetAppInfo(&app_name, &app_name_nice, &landscape, &version);

	uint64_t tID;
	tID = OSGetTitleID();
	char titleIDHex[17];
	bytes2hex(tID, titleIDHex);
	const bool sdAvailable = wiiu_sd_is_mounted() != 0;
	const bool fsAvailable = wiiu_fs_root_is_mounted() != 0;
	const bool standaloneTitle = (uint32_t)(tID >> 32) == 0x00050002;
	if (standaloneTitle && !fsAvailable) {
		OSReport("UIF PPSSPP: installed-title filesystem mount failed; launch cannot continue.\n");
		return 1;
	}

	const bool installedPackage = HasInstalledPackageContent(fsAvailable);
	const std::string writableRoot = SelectWritableRoot(installedPackage, fsAvailable, sdAvailable);
	if (writableRoot.empty()) {
		OSReport("UIF PPSSPP: no writable mounted filesystem is available.\n");
		return 1;
	}
	std::string logPath;
	std::ofstream fw;
	OpenAutobootDiagnosticLog(writableRoot, sdAvailable, &fw, &logPath);
	LogStage(fw, "main entered");
	if (fw.is_open())
		fw << "Title ID: " << titleIDHex << "\n"
			<< "Installed package content detected: " << (installedPackage ? "yes" : "no") << "\n"
			<< "Writable root: " << writableRoot << "\n"
			<< "Diagnostic log: " << logPath << "\n"
			<< "SD mounted: " << (sdAvailable ? "yes" : "no") << "\n"
			<< "Installed-title filesystem mounted: " << (fsAvailable ? "yes" : "no") << "\n"
			<< "Storage mounts are skipped for normal installable launch; using fs:/vol/content first.\n";
	const std::string runtimeRoot = EnsureTrailingSlash(FindRuntimeRoot(titleIDHex, fw));
	if (fw.is_open())
		fw << "Runtime root: " << runtimeRoot << "\n";

	bool copyToSd = false;
	bool menuOnly = false;
	std::string performanceProfile = "compatibility";
	WiiUPerformanceOverrides performanceOverrides;
	std::string gamePath;
	TryReadAutobootConfig(titleIDHex, &gamePath, &copyToSd, &menuOnly, &performanceProfile, &performanceOverrides, fw);

	if (!gamePath.empty() && !file_exists(gamePath)) {
		if (fw.is_open())
			fw << "Configured game path was not found: " << gamePath << "\n";
		gamePath.clear();
	}

	if (gamePath.empty())
		gamePath = FindInstalledGamePath(titleIDHex, fw);

	if (!menuOnly && !gamePath.empty() && copyToSd && !sdAvailable) {
		if (fw.is_open())
			fw << "copy_to_sd requested, but the SD filesystem is unavailable; attempting direct boot path instead.\n";
	}
	else if (!menuOnly && !gamePath.empty() && copyToSd) {
		const std::string sdCachePath = BuildSdCachePathForSource(gamePath);
		if (fw.is_open())
			fw << "copy_to_sd enabled. Copying " << gamePath << " to " << sdCachePath << "\n";
		if (CopyFileToSdCache(gamePath, sdCachePath, fw))
			gamePath = sdCachePath;
		else if (fw.is_open())
			fw << "SD copy failed; attempting direct boot path instead.\n";
	}

	if (fw.is_open()) {
		if (menuOnly)
			fw << "Runtime menu probe enabled. The packaged PSP source will not be launched.\n";
		else if (gamePath.empty())
			fw << "No PSP source found. Launching PPSSPP menu.\n";
		else
			fw << "Launching PSP source: " << gamePath << "\n";
	}

	const char *argv_[3] = {
		"PPSSPP.rpx",
		nullptr,
//		"-d",
//		"-v",
//		"-j",
//		"-r",
//		"-i",
//		"sd:/cube.elf",
		nullptr
	};
	if (!menuOnly && !gamePath.empty())
		argv_[1] = gamePath.c_str();
	const int nativeArgc = menuOnly || gamePath.empty() ? 1 : 2;

	//arg size, arg, savegamedir, external dir, cache dir
	LogStage(fw, "before NativeInit");
	NativeInit(nativeArgc, argv_, writableRoot.c_str(), runtimeRoot.c_str(), writableRoot.c_str());
	LogStage(fw, "after NativeInit");
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

	LogStage(fw, "before performance profile");
	ApplyWiiUPerformanceProfile(performanceProfile, performanceOverrides, fw);
	LogStage(fw, "after performance profile");
	std::string error_string;
	GraphicsContext *ctx = nullptr;
	LogStage(fw, "before platform graphics init");
	if (!host->InitGraphics(&error_string, &ctx) || !ctx) {
		if (fw.is_open())
			fw << "Platform graphics initialization failed: " << error_string << "\n";
		NativeShutdown();
		return 1;
	}
	LogStage(fw, "after platform graphics init");
	LogStage(fw, "before native graphics init");
	if (!NativeInitGraphics(ctx)) {
		LogStage(fw, "native graphics init failed");
		NativeShutdown();
		return 1;
	}
	LogStage(fw, "after native graphics init");
	NativeResized();

	LogStage(fw, "before sound init");
	host->InitSound();
	LogStage(fw, "after sound init");
	LogStage(fw, "before emulator run loop");
	while (true) {
		if (g_QuitRequested)
			break;

		if (!Core_IsActive())
			UpdateUIState(UISTATE_MENU);
		Core_Run(ctx);
	}
	LogStage(fw, "after emulator run loop");
	host->ShutdownSound();
	NativeShutdownGraphics();
	NativeShutdown();
	LogStage(fw, "clean shutdown complete");

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
