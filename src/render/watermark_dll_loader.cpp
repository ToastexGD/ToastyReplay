#include "render/watermark_dll_loader.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <wincrypt.h>
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Advapi32.lib")
#endif

#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/string.hpp>

#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>
#include <string>
#include <mutex>

//sha 256 hash here
#include "render/watermark_hash_embedded.hpp"

namespace toasty::watermark {

namespace {

using DrawFn = void(*)(const PlaybackWatermarkFrame*);

static HMODULE s_dllHandle = nullptr;
static DrawFn s_drawFn = nullptr;
static bool s_loaded = false;
static bool s_loadAttempted = false;
static std::mutex s_mutex;

static std::string computeFileSha256(const std::filesystem::path& path) {
#ifdef _WIN32
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string result;

    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return {};
    }

    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return {};
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return {};
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        DWORD bytesRead = static_cast<DWORD>(file.gcount());
        if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(buffer), bytesRead, 0)) {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            return {};
        }
    }

    DWORD hashLen = 32; // SHA-256 = 32 bytes
    BYTE hashBytes[32];
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hashBytes, &hashLen, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return {};
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    static constexpr char hexChars[] = "0123456789abcdef";
    result.reserve(64);
    for (DWORD i = 0; i < hashLen; ++i) {
        result.push_back(hexChars[(hashBytes[i] >> 4) & 0xF]);
        result.push_back(hexChars[hashBytes[i] & 0xF]);
    }
    return result;
#else
    (void)path;
    return {};
#endif
}

} 

bool loadWatermarkDll(std::string& outError) {
    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_loadAttempted) {
        if (s_loaded) return true;
        outError = "Watermark DLL load previously failed.";
        return false;
    }
    s_loadAttempted = true;

#ifdef _WIN32
    auto mod = geode::Mod::get();
    std::filesystem::path resourcesPath = mod->getResourcesDir() / "bin" / "toasty_watermark.dll";
    std::filesystem::path configFallback = mod->getConfigDir() / "toasty_watermark.dll";

    std::filesystem::path dllPath;
    bool usingFallback = false;
    if (std::filesystem::exists(resourcesPath)) {
        dllPath = resourcesPath;
    } else if (std::filesystem::exists(configFallback)) {
        dllPath = configFallback;
        usingFallback = true;
        geode::log::warn(
            "Watermark DLL loaded from legacy config directory: {}. "
            "This path is deprecated; reinstall the latest .geode to "
            "pick up the bundled copy.",
            geode::utils::string::pathToString(dllPath)
        );
    } else {
        outError = "Watermark DLL not found. Expected at: "
            + geode::utils::string::pathToString(resourcesPath);
        return false;
    }
    (void)usingFallback;

    std::string expectedHash = toasty::watermark::embedded::kWatermarkDllSha256;
    if (expectedHash.empty() || expectedHash.find("PLACEHOLDER") != std::string::npos) {
        //Dev build without real hash
#if defined(TOASTYREPLAY_ALLOW_WATERMARK_STUB)
        // Debug build
#else
        outError = "Watermark DLL integrity hash not configured (build without CI).";
        return false;
#endif
    } else {
        std::string actualHash = computeFileSha256(dllPath);
        if (actualHash.empty()) {
            outError = "Failed to compute SHA-256 of watermark DLL.";
            return false;
        }
        if (actualHash != expectedHash) {
            outError = "Watermark DLL integrity check failed. Expected: " + expectedHash + " Got: " + actualHash;
            return false;
        }
    }

    //Load the DLL
    auto dllPathStr = geode::utils::string::pathToString(dllPath);
    s_dllHandle = LoadLibraryA(dllPathStr.c_str());
    if (!s_dllHandle) {
        DWORD err = GetLastError();
        outError = "Failed to load watermark DLL (error " + std::to_string(err) + ")";
        return false;
    }

    //Resolve the exported function
    s_drawFn = reinterpret_cast<DrawFn>(
        GetProcAddress(s_dllHandle, "toastyreplay_private_draw_playback_watermark")
    );
    if (!s_drawFn) {
        outError = "Watermark DLL missing export: toastyreplay_private_draw_playback_watermark";
        FreeLibrary(s_dllHandle);
        s_dllHandle = nullptr;
        return false;
    }

    s_loaded = true;
    return true;
#else
    outError = "Watermark DLL loading not supported on this platform.";
    return false;
#endif
}

bool isWatermarkDllReady() {
    return s_loaded && s_drawFn != nullptr;
}

void callDllDrawPlaybackWatermark(PlaybackWatermarkFrame const& frame) {
    if (s_drawFn) {
        s_drawFn(&frame);
    }
}

void unloadWatermarkDll() {
    std::lock_guard<std::mutex> lock(s_mutex);
#ifdef _WIN32
    if (s_dllHandle) {
        FreeLibrary(s_dllHandle);
        s_dllHandle = nullptr;
    }
#endif
    s_drawFn = nullptr;
    s_loaded = false;
    s_loadAttempted = false;
}

} // namespace toasty::watermark
