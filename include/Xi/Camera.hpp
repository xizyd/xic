#ifndef XI_CAMERA_HPP
#define XI_CAMERA_HPP

#include "Tree.hpp"
#include "Texture.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"

namespace Xi
{
    struct Renderable3 : public TreeItem, public Transform3
    {
        Mesh3 *mesh = nullptr;
        Shader *shader = nullptr;
        Texture *texture = nullptr;
    };

    struct ShaderData
    {
        Matrix4 mvp;   // 64 bytes
        Matrix4 world; // 64 bytes
    };

    struct Camera3 : public Transform3
    {
        TreeItem *root = nullptr;
        Texture texture;

        float clipStart = 0.1f;
        float clipEnd = 100.0f;

        float shiftX = 0.0f;
        float shiftY = 0.0f;

        bool isOrtho = false;
        float fov = 50.0f;      // En degrés
        float orthoScale = 8.0f;

        Diligent::RefCntAutoPtr<Diligent::ITextureView> pDSV;

        Camera3()
        {
            texture.onUpdate = [this]()
            {
                this->render();
            };
        }

        virtual ~Camera3() = default;

        void render(void *rtv, void *dsv, i32 w, i32 h)
        {
            if (!root || !rtv)
                return;

            gContext.bindResources(rtv, dsv, w, h);

            f32 aspect = (f32)w / (f32)h;

            // 1. Calcul de la Vue (View Matrix)
            Matrix4 viewRotation = Matrix4::multiply(
                Matrix4::rotateY(-rotation.y),
                Matrix4::rotateX(-rotation.x)
            );
            Matrix4 viewTranslation = Matrix4::translate(-position.x, -position.y, -position.z);
            Matrix4 view = Matrix4::multiply(viewTranslation, viewRotation);

            // 2. Calcul de la Projection
            Matrix4 proj;
            if (isOrtho)
            {
                // Mode Orthographique (2D / Isométrique)
                float halfW = (orthoScale * aspect) / 2.0f;
                float halfH = orthoScale / 2.0f;
                proj = Matrix4::ortho(-halfW, halfW, -halfH, halfH, clipStart, clipEnd);
            }
            else
            {
                // Mode Perspective (3D Standard)
                // Conversion FOV degrés vers radians
                float fovRad = fov * (3.14159f / 180.0f);
                proj = Matrix4::perspective(fovRad, aspect, clipStart, clipEnd);
            }

            // 3. Application du Lens Shift (Décalage de la lentille)
            // Utile pour le rendu UI décentré ou les corrections architecturales
            if (shiftX != 0.0f || shiftY != 0.0f)
            {
                Matrix4 shiftMat = Matrix4::translate(shiftX, shiftY, 0.0f);
                proj = Matrix4::multiply(proj, shiftMat);
            }

            Matrix4 vp = Matrix4::multiply(view, proj);
            _renderRec(root, Matrix4::identity(), vp);
        }

        void render()
        {
            texture.touchGPU();
            if (!texture.pTexture)
                return;

            _ensureDepthBuffer(texture.width, texture.height);

            auto *pRTV = texture.pTexture->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
            float clearColor[] = {0.1f, 0.1f, 0.12f, 1.0f}; // Gris foncé "Xi"

            gContext.ctx->SetRenderTargets(1, &pRTV, pDSV, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            gContext.ctx->ClearRenderTarget(pRTV, clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            gContext.ctx->ClearDepthStencil(pDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            render((void *)pRTV, (void *)pDSV, texture.width, texture.height);
        }

    private:
        void _ensureDepthBuffer(i32 w, i32 h)
        {
            if (pDSV)
            {
                auto *pTex = pDSV->GetTexture();
                if (pTex && pTex->GetDesc().Width == (uint32_t)w && pTex->GetDesc().Height == (uint32_t)h)
                    return;
            }

            Diligent::TextureDesc desc;
            desc.Name = "Xi_Camera_InternalDepth";
            desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
            desc.Width = w;
            desc.Height = h;
            desc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
            desc.BindFlags = Diligent::BIND_DEPTH_STENCIL;

            Diligent::RefCntAutoPtr<Diligent::ITexture> pDepth;
            gContext.device->CreateTexture(desc, nullptr, &pDepth);
            pDSV = pDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
        }

        static void _renderRec(TreeItem *n, Matrix4 p, const Matrix4 &vp)
        {
            if (!n) return;

            Renderable3 *r = dynamic_cast<Renderable3 *>(n);
            Matrix4 world = p;

            if (r)
            {
                world = Matrix4::multiply(r->getMatrix(), p);

                if (r->mesh && r->shader)
                {
                    r->mesh->upload();
                    r->shader->create();

                    ShaderData gpuData;
                    Matrix4 mvp = Matrix4::multiply(world, vp);
                    gpuData.mvp = Matrix4::transpose(mvp);
                    gpuData.world = Matrix4::transpose(world);

                    r->shader->updateUniforms(&gpuData, sizeof(ShaderData));
                    gContext.setPipelineState(r->shader->_pso);

                    auto *srb = (Diligent::IShaderResourceBinding *)r->shader->_srb;
                    auto *pTexVar = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture");
                    auto *pSamplerVar = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture_sampler");

                    if (pTexVar && r->texture && r->texture->pTexture)
                    {
                        auto *pView = r->texture->pTexture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
                        pTexVar->Set(pView);
                    }

                    if (pSamplerVar)
                    {
                        static Diligent::RefCntAutoPtr<Diligent::ISampler> pDefaultSampler;
                        if (!pDefaultSampler)
                        {
                            Diligent::SamplerDesc SamDesc;
                            gContext.device->CreateSampler(SamDesc, &pDefaultSampler);
                        }
                        pSamplerVar->Set(pDefaultSampler);
                    }

                    gContext.commitResources(r->shader->_srb);
                    gContext.drawMesh(r->mesh->_vb, r->mesh->_ib, r->mesh->indices.length);
                }
            }

            for (usz i = 0; i < n->children.length; ++i)
                _renderRec(n->children[i], world, vp);
        }
    };
}

#endif