#pragma once

#include <Geode/loader/Event.hpp>
#include <Geode/Result.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <span>

namespace ffmpeg {
inline namespace v2 {

enum class PixelFormat : int {
    NONE = -1,
    YUV420P,
    YUYV422,
    RGB24,
    BGR24,
    YUV422P,
    YUV444P,
    YUV410P,
    YUV411P,
    GRAY8,
    MONOWHITE,
    MONOBLACK,
    PAL8,
    YUVJ420P,
    YUVJ422P,
    YUVJ444P,
    UYVY422,
    UYYVYY411,
    BGR8,
    BGR4,
    BGR4_BYTE,
    RGB8,
    RGB4,
    RGB4_BYTE,
    NV12,
    NV21,

    ARGB,
    RGBA,
    ABGR,
    BGRA,

    GRAY16BE,
    GRAY16LE,
    YUV440P,
    YUVJ440P,
    YUVA420P,
    RGB48BE,
    RGB48LE,

    RGB565BE,
    RGB565LE,
    RGB555BE,
    RGB555LE,

    BGR565BE,
    BGR565LE,
    BGR555BE,
    BGR555LE,

    VAAPI,

    YUV420P16LE,
    YUV420P16BE,
    YUV422P16LE,
    YUV422P16BE,
    YUV444P16LE,
    YUV444P16BE,
    DXVA2_VLD,

    RGB444LE,
    RGB444BE,
    BGR444LE,
    BGR444BE,
    YA8,

    Y400A = YA8,
    GRAY8A = YA8,

    BGR48BE,
    BGR48LE,

    YUV420P9BE,
    YUV420P9LE,
    YUV420P10BE,
    YUV420P10LE,
    YUV422P10BE,
    YUV422P10LE,
    YUV444P9BE,
    YUV444P9LE,
    YUV444P10BE,
    YUV444P10LE,
    YUV422P9BE,
    YUV422P9LE,
    GBRP,
    GBR24P = GBRP,
    GBRP9BE,
    GBRP9LE,
    GBRP10BE,
    GBRP10LE,
    GBRP16BE,
    GBRP16LE,
    YUVA422P,
    YUVA444P,
    YUVA420P9BE,
    YUVA420P9LE,
    YUVA422P9BE,
    YUVA422P9LE,
    YUVA444P9BE,
    YUVA444P9LE,
    YUVA420P10BE,
    YUVA420P10LE,
    YUVA422P10BE,
    YUVA422P10LE,
    YUVA444P10BE,
    YUVA444P10LE,
    YUVA420P16BE,
    YUVA420P16LE,
    YUVA422P16BE,
    YUVA422P16LE,
    YUVA444P16BE,
    YUVA444P16LE,

    VDPAU,

    XYZ12LE,
    XYZ12BE,
    NV16,
    NV20LE,
    NV20BE,

    RGBA64BE,
    RGBA64LE,
    BGRA64BE,
    BGRA64LE,

    YVYU422,

    YA16BE,
    YA16LE,

    GBRAP,
    GBRAP16BE,
    GBRAP16LE,

    QSV,

    MMAL,

    D3D11VA_VLD,

    CUDA,

    _0RGB,
    RGB0,
    _0BGR,
    BGR0,

    YUV420P12BE,
    YUV420P12LE,
    YUV420P14BE,
    YUV420P14LE,
    YUV422P12BE,
    YUV422P12LE,
    YUV422P14BE,
    YUV422P14LE,
    YUV444P12BE,
    YUV444P12LE,
    YUV444P14BE,
    YUV444P14LE,
    GBRP12BE,
    GBRP12LE,
    GBRP14BE,
    GBRP14LE,
    YUVJ411P,

    BAYER_BGGR8,
    BAYER_RGGB8,
    BAYER_GBRG8,
    BAYER_GRBG8,
    BAYER_BGGR16LE,
    BAYER_BGGR16BE,
    BAYER_RGGB16LE,
    BAYER_RGGB16BE,
    BAYER_GBRG16LE,
    BAYER_GBRG16BE,
    BAYER_GRBG16LE,
    BAYER_GRBG16BE,

    YUV440P10LE,
    YUV440P10BE,
    YUV440P12LE,
    YUV440P12BE,
    AYUV64LE,
    AYUV64BE,

    VIDEOTOOLBOX,

    P010LE,
    P010BE,

    GBRAP12BE,
    GBRAP12LE,

    GBRAP10BE,
    GBRAP10LE,

    MEDIACODEC,

    GRAY12BE,
    GRAY12LE,
    GRAY10BE,
    GRAY10LE,

    P016LE,
    P016BE,

    D3D11,

    GRAY9BE,
    GRAY9LE,

    GBRPF32BE,
    GBRPF32LE,
    GBRAPF32BE,
    GBRAPF32LE,

    DRM_PRIME,

    OPENCL,

    GRAY14BE,
    GRAY14LE,

    GRAYF32BE,
    GRAYF32LE,

    YUVA422P12BE,
    YUVA422P12LE,
    YUVA444P12BE,
    YUVA444P12LE,

    NV24,
    NV42,

    VULKAN,

    Y210BE,
    Y210LE,

    X2RGB10LE,
    X2RGB10BE,
    X2BGR10LE,
    X2BGR10BE,

    P210BE,
    P210LE,

    P410BE,
    P410LE,

    P216BE,
    P216LE,

    P416BE,
    P416LE,

    VUYA,

    RGBAF16BE,
    RGBAF16LE,

    VUYX,

    P012LE,
    P012BE,

    Y212BE,
    Y212LE,

    XV30BE,
    XV30LE,

    XV36BE,
    XV36LE,

    RGBF32BE,
    RGBF32LE,

    RGBAF32BE,
    RGBAF32LE,

    P212BE,
    P212LE,

    P412BE,
    P412LE,

    GBRAP14BE,
    GBRAP14LE,

    D3D12,

    NB
};

enum class HardwareAccelerationType : int {
    NONE = 0,
    CUDA = 2,
    D3D11VA = 7,
};

struct RenderSettings {
    HardwareAccelerationType m_hardwareAccelerationType = HardwareAccelerationType::NONE;
    PixelFormat m_pixelFormat = PixelFormat::RGB0;
    std::string m_codec;
    std::string m_colorspaceFilters;
    bool m_doVerticalFlip = true;
    int64_t m_bitrate = 30000000;
    uint32_t m_width = 1920;
    uint32_t m_height = 1080;
    uint16_t m_fps = 60;
    std::filesystem::path m_outputFile;
};

}

namespace events {
namespace impl {
    constexpr size_t VTABLE_VERSION = 1;
    using CreateRecorder_t = void*(*)();
    using DeleteRecorder_t = void(*)(void*);
    using InitRecorder_t = geode::Result<>(*)(void*, const RenderSettings&);
    using StopRecorder_t = void(*)(void*);
    using WriteFrame_t = geode::Result<>(*)(void*, std::span<uint8_t const>);
    using GetAvailableCodecs_t = std::vector<std::string>(*)();
    using MixVideoAudio_t = geode::Result<>(*)(const std::filesystem::path&, const std::filesystem::path&, const std::filesystem::path&);
    using MixVideoRaw_t = geode::Result<>(*)(const std::filesystem::path&, std::span<float>, const std::filesystem::path&);

    struct VTable {
        CreateRecorder_t createRecorder = nullptr;
        DeleteRecorder_t deleteRecorder = nullptr;
        InitRecorder_t initRecorder = nullptr;
        StopRecorder_t stopRecorder = nullptr;
        WriteFrame_t writeFrame = nullptr;
        GetAvailableCodecs_t getAvailableCodecs = nullptr;
        MixVideoAudio_t mixVideoAudio = nullptr;
        MixVideoRaw_t mixVideoRaw = nullptr;
    };

    struct FetchVTableEvent : geode::Event<FetchVTableEvent, bool(VTable&, size_t)> {
        using Event::Event;
    };

    inline VTable& getVTable() {
        static VTable vtable;
        static bool initialized = false;
        if (!initialized) {
            initialized = FetchVTableEvent().send(vtable, VTABLE_VERSION);
        }
        return vtable;
    }
}

class Recorder {
public:
    Recorder() {
        auto& vtable = impl::getVTable();
        if (!vtable.createRecorder) {
            m_ptr = nullptr;
        } else {
            m_ptr = vtable.createRecorder();
        }
    }

    ~Recorder() {
        if (m_ptr) {
            auto& vtable = impl::getVTable();
            if (vtable.deleteRecorder) {
                vtable.deleteRecorder(m_ptr);
            }
        }
    }

    bool isValid() const { return m_ptr != nullptr; }

    geode::Result<> init(RenderSettings const& settings) {
        auto& vtable = impl::getVTable();
        if (!vtable.initRecorder || !m_ptr) {
            return geode::Err("FFmpeg API is not available.");
        }
        return vtable.initRecorder(m_ptr, settings);
    }

    void stop() {
        auto& vtable = impl::getVTable();
        if (vtable.stopRecorder && m_ptr) {
            vtable.stopRecorder(m_ptr);
        }
    }

    geode::Result<> writeFrame(std::span<uint8_t const> frameData) {
        auto& vtable = impl::getVTable();
        if (!vtable.writeFrame || !m_ptr) {
            return geode::Err("FFmpeg API is not available.");
        }
        return vtable.writeFrame(m_ptr, frameData);
    }

    static std::vector<std::string> getAvailableCodecs() {
        auto& vtable = impl::getVTable();
        if (!vtable.getAvailableCodecs) {
            return {};
        }
        return vtable.getAvailableCodecs();
    }

private:
    void* m_ptr = nullptr;
};

class AudioMixer {
public:
    AudioMixer() = delete;

    static geode::Result<> mixVideoAudio(std::filesystem::path const& videoFile, std::filesystem::path const& audioFile, std::filesystem::path const& outputMp4File) {
        auto& vtable = impl::getVTable();
        if (!vtable.mixVideoAudio) {
            return geode::Err("FFmpeg API is not available.");
        }
        return vtable.mixVideoAudio(videoFile, audioFile, outputMp4File);
    }

    static geode::Result<> mixVideoRaw(std::filesystem::path const& videoFile, std::span<float> raw, std::filesystem::path const& outputMp4File) {
        auto& vtable = impl::getVTable();
        if (!vtable.mixVideoRaw) {
            return geode::Err("FFmpeg API is not available.");
        }
        return vtable.mixVideoRaw(videoFile, raw, outputMp4File);
    }
};

}
}
