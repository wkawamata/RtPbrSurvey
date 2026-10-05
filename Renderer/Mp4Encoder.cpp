#include "stdafx.h"

#include "Renderer/Mp4Encoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mfapi.h>
#include <mferror.h>
#include <codecapi.h>

namespace Engine
{
    namespace
    {
        std::string HResultMessage(HRESULT result)
        {
            char message[96] = {};
            snprintf(message, sizeof(message), "Media Foundation H.264/MP4 error 0x%08lX.", static_cast<unsigned long>(result));
            return message;
        }

        std::uint8_t VideoByte(double value)
        {
            return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(value)), 0, 255));
        }

        // Top-down BT.709 limited-range NV12. Duplicate edge pixels for odd ROI sizes.
        void ConvertToNv12(const std::uint8_t* rgba, unsigned int width, unsigned int height,
                           unsigned int encodedWidth, unsigned int encodedHeight, std::uint8_t* output)
        {
            const auto pixel = [&](unsigned int x, unsigned int y)
            {
                return rgba + (static_cast<std::size_t>((std::min)(y, height - 1)) * width + (std::min)(x, width - 1)) * 4;
            };
            for (unsigned int y = 0; y < encodedHeight; ++y)
            {
                for (unsigned int x = 0; x < encodedWidth; ++x)
                {
                    const std::uint8_t* rgb = pixel(x, y);
                    output[static_cast<std::size_t>(y) * encodedWidth + x] =
                        VideoByte(16 + 0.182586 * rgb[0] + 0.614231 * rgb[1] + 0.062007 * rgb[2]);
                }
            }
            std::uint8_t* chroma = output + static_cast<std::size_t>(encodedWidth) * encodedHeight;
            for (unsigned int y = 0; y < encodedHeight; y += 2)
            {
                for (unsigned int x = 0; x < encodedWidth; x += 2)
                {
                    double red = 0, green = 0, blue = 0;
                    for (unsigned int dy = 0; dy < 2; ++dy)
                    {
                        for (unsigned int dx = 0; dx < 2; ++dx)
                        {
                            const std::uint8_t* rgb = pixel(x + dx, y + dy);
                            red += rgb[0] * 0.25;
                            green += rgb[1] * 0.25;
                            blue += rgb[2] * 0.25;
                        }
                    }
                    const std::size_t offset = static_cast<std::size_t>(y / 2) * encodedWidth + x;
                    chroma[offset] = VideoByte(128 - 0.100644 * red - 0.338572 * green + 0.439216 * blue);
                    chroma[offset + 1] = VideoByte(128 + 0.439216 * red - 0.398942 * green - 0.040274 * blue);
                }
            }
        }

        HRESULT SetVideoType(IMFMediaType* type, REFGUID subtype, unsigned int width,
                             unsigned int height, std::uint32_t fps)
        {
            HRESULT result = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            if (SUCCEEDED(result)) result = type->SetGUID(MF_MT_SUBTYPE, subtype);
            if (SUCCEEDED(result)) result = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
            if (SUCCEEDED(result)) result = MFSetAttributeSize(type, MF_MT_FRAME_SIZE, width, height);
            if (SUCCEEDED(result)) result = MFSetAttributeRatio(type, MF_MT_FRAME_RATE, fps, 1);
            if (SUCCEEDED(result)) result = MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
            if (SUCCEEDED(result)) result = type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
            if (SUCCEEDED(result)) result = type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
            if (SUCCEEDED(result)) result = type->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
            if (SUCCEEDED(result)) result = type->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
            return result;
        }
    } // namespace

    Mp4Encoder::~Mp4Encoder()
    {
        Reset();
    }

    bool Mp4Encoder::Begin(const std::filesystem::path& path, unsigned int width, unsigned int height,
                           std::uint32_t framesPerSecond, std::uint32_t bitrate, std::string& error)
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_ownsCom = SUCCEEDED(result);
        if (result == RPC_E_CHANGED_MODE) result = S_OK;
        if (SUCCEEDED(result))
        {
            result = MFStartup(MF_VERSION);
            m_ownsMediaFoundation = SUCCEEDED(result);
        }
        std::error_code fileError;
        if (SUCCEEDED(result) && path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), fileError);
            if (fileError)
            {
                error = "Unable to create MP4 output folder: " + fileError.message();
                Reset();
                return false;
            }
        }
        if (SUCCEEDED(result))
        {
            result = MFCreateFile(MF_ACCESSMODE_READWRITE, MF_OPENMODE_FAIL_IF_EXIST, MF_FILEFLAGS_NONE, path.c_str(), &m_stream);
            if (SUCCEEDED(result))
            {
                m_path = path;
                m_removeIncomplete = true;
            }
        }
        Microsoft::WRL::ComPtr<IMFAttributes> attributes;
        if (SUCCEEDED(result)) result = MFCreateAttributes(&attributes, 3);
        if (SUCCEEDED(result)) result = attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
        if (SUCCEEDED(result)) result = attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        // The capture scheduler controls pacing; never wait here for presentation time.
        if (SUCCEEDED(result)) result = attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        if (SUCCEEDED(result)) result = MFCreateSinkWriterFromURL(nullptr, m_stream.Get(), attributes.Get(), &m_writer);
        m_encodedWidth = (width + 1) & ~1u;
        m_encodedHeight = (height + 1) & ~1u;
        Microsoft::WRL::ComPtr<IMFMediaType> outputType;
        if (SUCCEEDED(result)) result = MFCreateMediaType(&outputType);
        if (SUCCEEDED(result)) result = SetVideoType(outputType.Get(), MFVideoFormat_H264, m_encodedWidth, m_encodedHeight, framesPerSecond);
        if (SUCCEEDED(result)) result = outputType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        // Main profile is widely supported; no audio track is added.
        if (SUCCEEDED(result)) result = outputType->SetUINT32(MF_MT_MPEG2_PROFILE, 77);
        if (SUCCEEDED(result)) result = m_writer->AddStream(outputType.Get(), &m_streamIndex);
        Microsoft::WRL::ComPtr<IMFMediaType> inputType;
        if (SUCCEEDED(result)) result = MFCreateMediaType(&inputType);
        if (SUCCEEDED(result)) result = SetVideoType(inputType.Get(), MFVideoFormat_NV12, m_encodedWidth, m_encodedHeight, framesPerSecond);
        Microsoft::WRL::ComPtr<IMFAttributes> encoderParameters;
        if (SUCCEEDED(result)) result = MFCreateAttributes(&encoderParameters, 1);
        if (SUCCEEDED(result)) result = encoderParameters->SetUINT32(CODECAPI_AVEncMPVDefaultBPictureCount, 0);
        if (SUCCEEDED(result)) result = m_writer->SetInputMediaType(m_streamIndex, inputType.Get(), encoderParameters.Get());
        if (SUCCEEDED(result)) result = m_writer->BeginWriting();
        if (FAILED(result))
        {
            error = HResultMessage(result);
            Reset();
            return false;
        }
        m_width = width;
        m_height = height;
        m_fps = framesPerSecond;
        m_bitrate = bitrate;
        return true;
    }

    bool Mp4Encoder::AppendFrame(const std::filesystem::path& path, unsigned int width, unsigned int height,
                                 const std::uint8_t* rgba8, std::uint32_t framesPerSecond,
                                 std::uint32_t bitrate, std::string& error, std::optional<std::uint64_t> timestamp100ns)
    {
        error.clear();
        if (!rgba8 || width == 0 || height == 0 || width > 16384 || height > 16384 ||
            framesPerSecond == 0 || framesPerSecond > 240 || bitrate < 1000000 || bitrate > 100000000)
        {
            error = "MP4 requires valid pixels, FPS 1-240, and bitrate 1-100 Mbps.";
            Reset();
            return false;
        }
        if (!m_writer && !Begin(path, width, height, framesPerSecond, bitrate, error)) return false;
        if (path != m_path || width != m_width || height != m_height || framesPerSecond != m_fps || bitrate != m_bitrate)
        {
            error = "MP4 frame size, path, FPS, and bitrate must remain constant during a session.";
            Reset();
            return false;
        }
        const DWORD byteCount = m_encodedWidth * m_encodedHeight * 3 / 2;
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        HRESULT result = MFCreateMemoryBuffer(byteCount, &buffer);
        BYTE* destination = nullptr;
        if (SUCCEEDED(result)) result = buffer->Lock(&destination, nullptr, nullptr);
        if (SUCCEEDED(result))
        {
            ConvertToNv12(rgba8, width, height, m_encodedWidth, m_encodedHeight, destination);
            result = buffer->Unlock();
        }
        if (SUCCEEDED(result)) result = buffer->SetCurrentLength(byteCount);
        Microsoft::WRL::ComPtr<IMFSample> sample;
        if (SUCCEEDED(result)) result = MFCreateSample(&sample);
        if (SUCCEEDED(result)) result = sample->AddBuffer(buffer.Get());
        // Fixed-step uses rational cadence; real-time preserves gaps between captured frames.
        const LONGLONG start = static_cast<LONGLONG>(timestamp100ns.value_or(m_frameIndex * 10000000 / m_fps));
        if (m_pendingSample && start <= m_pendingTime)
        {
            error = "MP4 timestamps must increase between frames.";
            Reset();
            return false;
        }
        if (SUCCEEDED(result)) result = sample->SetSampleTime(start);
        if (SUCCEEDED(result)) result = WritePendingFrame(start);
        if (FAILED(result))
        {
            error = HResultMessage(result);
            Reset();
            return false;
        }
        ++m_frameIndex;
        m_pendingSample = std::move(sample);
        m_pendingTime = start;
        return true;
    }

    HRESULT Mp4Encoder::WritePendingFrame(LONGLONG endTime)
    {
        if (!m_pendingSample) return S_OK;
        HRESULT result = m_pendingSample->SetSampleDuration(endTime - m_pendingTime);
        if (SUCCEEDED(result)) result = m_writer->WriteSample(m_streamIndex, m_pendingSample.Get());
        m_pendingSample.Reset();
        return result;
    }

    bool Mp4Encoder::Finalize(std::string& error, std::optional<std::uint64_t> endTimestamp100ns)
    {
        error.clear();
        if (!m_writer) return true;
        const LONGLONG minimumEnd = m_pendingTime + 10000000 / m_fps;
        const LONGLONG end = endTimestamp100ns ?
            (std::max)(minimumEnd, static_cast<LONGLONG>(*endTimestamp100ns)) :
            (std::max)(minimumEnd, static_cast<LONGLONG>(m_frameIndex * 10000000 / m_fps));
        HRESULT result = WritePendingFrame(end);
        if (SUCCEEDED(result)) result = m_writer->Finalize();
        m_removeIncomplete = FAILED(result);
        if (FAILED(result)) error = HResultMessage(result);
        Reset();
        return SUCCEEDED(result);
    }

    void Mp4Encoder::Reset()
    {
        m_pendingSample.Reset();
        m_pendingTime = 0;
        m_writer.Reset();
        m_stream.Reset();
        if (m_removeIncomplete)
        {
            std::error_code fileError;
            std::filesystem::remove(m_path, fileError);
        }
        m_path.clear();
        m_width = m_height = m_encodedWidth = m_encodedHeight = m_fps = m_bitrate = 0;
        m_frameIndex = 0;
        m_removeIncomplete = false;
        if (m_ownsMediaFoundation)
        {
            MFShutdown();
            m_ownsMediaFoundation = false;
        }
        if (m_ownsCom)
        {
            CoUninitialize();
            m_ownsCom = false;
        }
    }
} // namespace Engine
