#ifndef XI_TEXTURE_HPP
#define XI_TEXTURE_HPP

#include "Graphics.hpp"
#include "Func.hpp"

namespace Xi
{

    struct Texture
    {
    public:
        // Core metadata
        i32 width = 0;
        i32 height = 0;

        // Data containers
        String localData;                                      // CPU side
        Diligent::RefCntAutoPtr<Diligent::ITexture> pTexture;  // GPU side
        Diligent::RefCntAutoPtr<Diligent::ITextureView> pView; // GPU view

        // Reactive Logic
        // The user can listen to this to fill localData or pTexture/pView
        Func<void()> onUpdate;

        bool isLocked = false;

        Texture() = default;
        Texture(i32 w, i32 h) : width(w), height(h) {}
        virtual ~Texture() { _cleanup(); }

        // --- CPU ACCESS ---

        virtual String *lock()
        {
            if (onUpdate.isValid())
                onUpdate();

            // If CPU data is already present, just return it
            if (localData.size() > 0)
            {
                isLocked = true;
                return &localData;
            }

            // If we have a GPU texture but no CPU data, we must download
            if (pTexture)
            {
                _downloadFromGPU();
                isLocked = true;
                return &localData;
            }

            return &localData;
        }

        virtual void unlock()
        {
            if (isLocked)
            {
                localData.clear(); // Empty localData as requested
                isLocked = false;
            }
        }

        // --- GPU ACCESS ---

        virtual Diligent::ITextureView *getView()
        {
            if (onUpdate.isValid())
                onUpdate();

            // If View is empty but we have CPU data, upload it
            if (!pView && localData.size() > 0)
            {
                touchGPU();
            }

            return pView;
        }

        void touchGPU()
        {
            // 1. Sanitize dimensions
            if (width <= 0)
                width = 1;
            if (height <= 0)
                height = 1;

            // 2. Smart Check: If texture exists, does it match our current needs?
            if (pTexture)
            {
                const auto &desc = pTexture->GetDesc();
                if (desc.Width == (uint32_t)width && desc.Height == (uint32_t)height)
                {
                    // Already initialized and correct size, nothing to do.
                    return;
                }
                else
                {
                    // Size mismatch: we must clear old resources before overwriting
                    _cleanup();
                }
            }

            // 3. Prepare Descriptor
            Diligent::TextureDesc desc;
            desc.Name = "Xi_AutoUpload_Texture";
            desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
            desc.Width = (uint32_t)width;
            desc.Height = (uint32_t)height;
            desc.Format = Diligent::TEX_FORMAT_BGRA8_UNORM;
            desc.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_RENDER_TARGET;
            desc.Usage = Diligent::USAGE_DEFAULT;

            // 4. Handle Data Logic
            usz requiredSize = (usz)(width * height * 4);
            Diligent::TextureSubResData subRes;
            subRes.Stride = (uint64_t)width * 4;

            const void *pDataToUpload = nullptr;
            Array<u8> blackPadding;

            if (localData.size() >= requiredSize)
            {
                pDataToUpload = localData.data();
            }
            else
            {
                blackPadding.allocate(requiredSize);
                if (localData.size() > 0)
                {
                    for (usz i = 0; i < localData.size(); ++i)
                        blackPadding[i] = (u8)localData[i];
                }
                pDataToUpload = blackPadding.data();
            }

            subRes.pData = pDataToUpload;

            Diligent::TextureData initData;
            initData.pSubResources = &subRes;
            initData.NumSubresources = 1;

            // 5. Create Object (Safe now because pTexture was released if necessary)
            gContext.device->CreateTexture(desc, &initData, &pTexture);

            if (pTexture)
            {
                pView = pTexture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
            }
        }

    private:
        void _cleanup()
        {
            pView.Release();
            pTexture.Release();
        }

        void _downloadFromGPU()
        {
            if (!pTexture)
                return;

            // 1. Create a Staging texture for reading
            Diligent::TextureDesc stagingDesc = pTexture->GetDesc();
            stagingDesc.Name = "Xi_Staging_Download";
            stagingDesc.Usage = Diligent::USAGE_STAGING;
            stagingDesc.BindFlags = Diligent::BIND_NONE;
            stagingDesc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;

            Diligent::RefCntAutoPtr<Diligent::ITexture> pStaging;
            gContext.device->CreateTexture(stagingDesc, nullptr, &pStaging);

            // 2. Copy GPU data to Staging
            Diligent::CopyTextureAttribs copyAttribs(
                pTexture, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                pStaging, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            gContext.ctx->CopyTexture(copyAttribs);
            gContext.ctx->WaitForIdle(); // Block until GPU is finished

            // 3. Map memory and copy to localData
            Diligent::MappedTextureSubresource mapped;
            gContext.ctx->MapTextureSubresource(pStaging, 0, 0, Diligent::MAP_READ, Diligent::MAP_FLAG_NONE, nullptr, mapped);

            localData.set((const u8 *)mapped.pData, width * height * 4);

            gContext.ctx->UnmapTextureSubresource(pStaging, 0, 0);
        }
    };

} // namespace Xi

#endif