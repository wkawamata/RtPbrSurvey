#include "stdafx.h"

#include "Renderer/AnimatedGifEncoder.h"

#include <combaseapi.h>

namespace Engine
{
    namespace
    {
        class ScopedComInitialization
        {
        public:
            ScopedComInitialization() : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
            {
            }

            ~ScopedComInitialization()
            {
                if (SUCCEEDED(m_result))
                {
                    CoUninitialize();
                }
            }

            bool IsAvailable() const
            {
                return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
            }

        private:
            HRESULT m_result = E_FAIL;
        };

        std::string HResultMessage(HRESULT result)
        {
            char message[48] = {};
            sprintf_s(message, "HRESULT 0x%08X", static_cast<unsigned int>(result));
            return message;
        }
    } // namespace

    AnimatedGifEncoder::~AnimatedGifEncoder()
    {
        Reset();
    }

    bool AnimatedGifEncoder::AppendFrame(const std::filesystem::path& path,
                                         unsigned int width,
                                         unsigned int height,
                                         const std::uint8_t* rgba8,
                                         std::uint16_t delayCentiseconds,
                                         std::optional<std::uint16_t> repeatCount,
                                         std::uint8_t disposal,
                                         std::string& error)
    {
        error.clear();
        if (path.empty() || width == 0 || height == 0 || rgba8 == nullptr)
        {
            error = "Invalid GIF frame arguments.";
            return false;
        }
        ScopedComInitialization comInitialization;
        if (!comInitialization.IsAvailable())
        {
            error = "COM initialization failed.";
            return false;
        }
        if (!IsActive() && !Begin(path, width, height, repeatCount, error))
        {
            return false;
        }
        if (m_width != width || m_height != height)
        {
            error = "GIF frame dimensions must remain constant within a capture session.";
            return false;
        }

        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
        Microsoft::WRL::ComPtr<IPropertyBag2> properties;
        Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> metadata;
        HRESULT result = m_factory->CreateBitmapFromMemory(width,
                                                           height,
                                                           GUID_WICPixelFormat32bppRGBA,
                                                           width * 4,
                                                           width * height * 4,
                                                           const_cast<std::uint8_t*>(rgba8),
                                                           &bitmap);
        if (SUCCEEDED(result))
            result = m_encoder->CreateNewFrame(&frame, &properties);
        if (SUCCEEDED(result))
            result = frame->Initialize(properties.Get());
        if (SUCCEEDED(result))
            result = frame->SetSize(width, height);
        if (SUCCEEDED(result))
            result = frame->GetMetadataQueryWriter(&metadata);
        if (SUCCEEDED(result))
        {
            PROPVARIANT delay = {};
            delay.vt = VT_UI2;
            delay.uiVal = delayCentiseconds;
            result = metadata->SetMetadataByName(L"/grctlext/Delay", &delay);
        }
        if (SUCCEEDED(result))
        {
            PROPVARIANT disposalValue = {};
            disposalValue.vt = VT_UI1;
            disposalValue.bVal = disposal;
            result = metadata->SetMetadataByName(L"/grctlext/Disposal", &disposalValue);
        }
        if (SUCCEEDED(result))
            result = frame->WriteSource(bitmap.Get(), nullptr);
        if (SUCCEEDED(result))
            result = frame->Commit();
        if (FAILED(result))
        {
            error = HResultMessage(result);
            return false;
        }
        return true;
    }

    bool AnimatedGifEncoder::Finalize(std::string& error)
    {
        error.clear();
        ScopedComInitialization comInitialization;
        if (!IsActive())
        {
            return true;
        }
        const HRESULT result = m_encoder->Commit();
        Reset();
        if (FAILED(result))
        {
            error = HResultMessage(result);
            return false;
        }
        return true;
    }

    void AnimatedGifEncoder::Reset()
    {
        m_encoder.Reset();
        m_stream.Reset();
        m_factory.Reset();
        m_width = 0;
        m_height = 0;
        if (m_ownsComInitialization)
        {
            CoUninitialize();
            m_ownsComInitialization = false;
        }
    }

    bool AnimatedGifEncoder::IsActive() const
    {
        return m_encoder != nullptr;
    }

    bool AnimatedGifEncoder::Begin(const std::filesystem::path& path,
                                   unsigned int width,
                                   unsigned int height,
                                   std::optional<std::uint16_t> repeatCount,
                                   std::string& error)
    {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_ownsComInitialization = SUCCEEDED(comResult);
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
        {
            error = "COM initialization failed.";
            return false;
        }
        try
        {
            if (path.has_parent_path())
            {
                std::filesystem::create_directories(path.parent_path());
            }
        }
        catch (const std::exception& exception)
        {
            Reset();
            error = exception.what();
            return false;
        }

        HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_factory));
        if (SUCCEEDED(result))
            result = m_factory->CreateStream(&m_stream);
        if (SUCCEEDED(result))
            result = m_stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
        if (SUCCEEDED(result))
            result = m_factory->CreateEncoder(GUID_ContainerFormatGif, nullptr, &m_encoder);
        if (SUCCEEDED(result))
            result = m_encoder->Initialize(m_stream.Get(), WICBitmapEncoderNoCache);
        if (SUCCEEDED(result) && repeatCount.has_value())
        {
            Microsoft::WRL::ComPtr<IWICMetadataQueryWriter> metadata;
            result = m_encoder->GetMetadataQueryWriter(&metadata);
            if (SUCCEEDED(result))
            {
                BYTE application[] = {'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0'};
                BYTE data[] = {
                    3,
                    1,
                    static_cast<BYTE>(*repeatCount & 0xff),
                    static_cast<BYTE>(*repeatCount >> 8),
                    0,
                };
                PROPVARIANT applicationValue = {};
                applicationValue.vt = VT_VECTOR | VT_UI1;
                applicationValue.caub.cElems = ARRAYSIZE(application);
                applicationValue.caub.pElems = application;
                result = metadata->SetMetadataByName(L"/appext/Application", &applicationValue);
                if (SUCCEEDED(result))
                {
                    PROPVARIANT dataValue = {};
                    dataValue.vt = VT_VECTOR | VT_UI1;
                    dataValue.caub.cElems = ARRAYSIZE(data);
                    dataValue.caub.pElems = data;
                    result = metadata->SetMetadataByName(L"/appext/Data", &dataValue);
                }
            }
        }
        if (FAILED(result))
        {
            Reset();
            error = HResultMessage(result);
            return false;
        }
        m_width = width;
        m_height = height;
        return true;
    }
} // namespace Engine
