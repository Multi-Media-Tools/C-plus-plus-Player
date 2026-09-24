#include "ui_descriptioncard.h"
#include "app.h"
#include "imgui_internal.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <string>
#include <algorithm>
#include <vector>
#include <random>

// --- HLSL Shaders for Dual-Kawase Blur ---
static const char* s_blurShaderHLSL = R"(
struct VS_OUT {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VS_OUT VS_Main(uint vid : SV_VertexID) {
    VS_OUT o;
    o.uv = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(o.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

Texture2D    srcTex    : register(t0);
SamplerState smpLinear : register(s0);

cbuffer BlurParams : register(b0) {
    float2 invTargetSize;
    float  offset;
    float  padding;
};

float4 PS_Kawase(VS_OUT input) : SV_Target {
    float2 uv = input.uv;
    float2 d = invTargetSize * offset;
    float4 col = srcTex.Sample(smpLinear, uv + float2(-d.x, -d.y)) * 0.25f;
    col += srcTex.Sample(smpLinear, uv + float2( d.x, -d.y)) * 0.25f;
    col += srcTex.Sample(smpLinear, uv + float2(-d.x,  d.y)) * 0.25f;
    col += srcTex.Sample(smpLinear, uv + float2( d.x,  d.y)) * 0.25f;
    return col;
}
)";

struct BlurParamsCB {
    float invTargetSize[2];
    float offset;
    float padding;
};

// Shader & Texture state
static ID3D11VertexShader*       s_vs = nullptr;
static ID3D11PixelShader*        s_ps = nullptr;
static ID3D11SamplerState*       s_sampler = nullptr;
static ID3D11Buffer*             s_cbParams = nullptr;

static const int kBlurTexW = 256;
static const int kBlurTexH = 128;

static ID3D11Texture2D*          s_texSource = nullptr;
static ID3D11ShaderResourceView* s_srvSource = nullptr;

static ID3D11Texture2D*          s_texPing = nullptr;
static ID3D11RenderTargetView*   s_rtvPing = nullptr;
static ID3D11ShaderResourceView* s_srvPing = nullptr;

static ID3D11Texture2D*          s_texPong = nullptr;
static ID3D11RenderTargetView*   s_rtvPong = nullptr;
static ID3D11ShaderResourceView* s_srvPong = nullptr;

static ID3D11Texture2D*          s_texNoise = nullptr;
static ID3D11ShaderResourceView* s_srvNoise = nullptr;

static bool                      s_initialized = false;

// Per-frame capture rectangle for draw callback
struct BlurCallbackData {
    int srcX = 0;
    int srcY = 0;
    int srcW = 0;
    int srcH = 0;
};
static BlurCallbackData s_currentCallbackData;

static void CreateNoiseTexture(ID3D11Device* device) {
    const int noiseSize = 64;
    std::vector<uint32_t> noisePixels(noiseSize * noiseSize);
    std::mt19937 rng(1337);
    std::uniform_int_distribution<int> dist(0, 255);

    for (int i = 0; i < noiseSize * noiseSize; ++i) {
        uint8_t val = (uint8_t)dist(rng);
        // Subtle monochrome grain with very low alpha (~5%)
        noisePixels[i] = (uint32_t(18) << 24) | (uint32_t(val) << 16) | (uint32_t(val) << 8) | uint32_t(val);
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = noiseSize;
    desc.Height = noiseSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subData{};
    subData.pSysMem = noisePixels.data();
    subData.SysMemPitch = noiseSize * sizeof(uint32_t);

    if (SUCCEEDED(device->CreateTexture2D(&desc, &subData, &s_texNoise))) {
        device->CreateShaderResourceView(s_texNoise, nullptr, &s_srvNoise);
    }
}

void DescriptionCard_Init(ID3D11Device* device) {
    if (s_initialized || !device)
        return;

    // 1. Compile Shaders
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errBlob = nullptr;

    HRESULT hr = D3DCompile(
        s_blurShaderHLSL, strlen(s_blurShaderHLSL), nullptr, nullptr, nullptr,
        "VS_Main", "vs_4_0", 0, 0, &vsBlob, &errBlob);
    if (SUCCEEDED(hr) && vsBlob) {
        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &s_vs);
        vsBlob->Release();
    }
    if (errBlob) { errBlob->Release(); errBlob = nullptr; }

    hr = D3DCompile(
        s_blurShaderHLSL, strlen(s_blurShaderHLSL), nullptr, nullptr, nullptr,
        "PS_Kawase", "ps_4_0", 0, 0, &psBlob, &errBlob);
    if (SUCCEEDED(hr) && psBlob) {
        device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &s_ps);
        psBlob->Release();
    }
    if (errBlob) { errBlob->Release(); errBlob = nullptr; }

    // 2. Sampler State (Linear Clamp)
    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampDesc, &s_sampler);

    // 3. Constant Buffer
    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(BlurParamsCB);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&cbDesc, nullptr, &s_cbParams);

    // 4. Source Texture (Dynamic capture rect)
    D3D11_TEXTURE2D_DESC srcDesc{};
    srcDesc.Width = kBlurTexW;
    srcDesc.Height = kBlurTexH;
    srcDesc.MipLevels = 1;
    srcDesc.ArraySize = 1;
    srcDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srcDesc.SampleDesc.Count = 1;
    srcDesc.Usage = D3D11_USAGE_DEFAULT;
    srcDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (SUCCEEDED(device->CreateTexture2D(&srcDesc, nullptr, &s_texSource))) {
        device->CreateShaderResourceView(s_texSource, nullptr, &s_srvSource);
    }

    // 5. Ping-Pong Render Targets
    D3D11_TEXTURE2D_DESC rtDesc{};
    rtDesc.Width = kBlurTexW;
    rtDesc.Height = kBlurTexH;
    rtDesc.MipLevels = 1;
    rtDesc.ArraySize = 1;
    rtDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtDesc.SampleDesc.Count = 1;
    rtDesc.Usage = D3D11_USAGE_DEFAULT;
    rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (SUCCEEDED(device->CreateTexture2D(&rtDesc, nullptr, &s_texPing))) {
        device->CreateRenderTargetView(s_texPing, nullptr, &s_rtvPing);
        device->CreateShaderResourceView(s_texPing, nullptr, &s_srvPing);
    }
    if (SUCCEEDED(device->CreateTexture2D(&rtDesc, nullptr, &s_texPong))) {
        device->CreateRenderTargetView(s_texPong, nullptr, &s_rtvPong);
        device->CreateShaderResourceView(s_texPong, nullptr, &s_srvPong);
    }

    // 6. Frosted Noise Texture
    CreateNoiseTexture(device);

    s_initialized = true;
}

void DescriptionCard_Shutdown() {
    if (s_srvNoise) { s_srvNoise->Release(); s_srvNoise = nullptr; }
    if (s_texNoise) { s_texNoise->Release(); s_texNoise = nullptr; }
    if (s_srvPong) { s_srvPong->Release(); s_srvPong = nullptr; }
    if (s_rtvPong) { s_rtvPong->Release(); s_rtvPong = nullptr; }
    if (s_texPong) { s_texPong->Release(); s_texPong = nullptr; }
    if (s_srvPing) { s_srvPing->Release(); s_srvPing = nullptr; }
    if (s_rtvPing) { s_rtvPing->Release(); s_rtvPing = nullptr; }
    if (s_texPing) { s_texPing->Release(); s_texPing = nullptr; }
    if (s_srvSource) { s_srvSource->Release(); s_srvSource = nullptr; }
    if (s_texSource) { s_texSource->Release(); s_texSource = nullptr; }
    if (s_cbParams) { s_cbParams->Release(); s_cbParams = nullptr; }
    if (s_sampler) { s_sampler->Release(); s_sampler = nullptr; }
    if (s_ps) { s_ps->Release(); s_ps = nullptr; }
    if (s_vs) { s_vs->Release(); s_vs = nullptr; }

    s_initialized = false;
}

// Executes on the GPU right when the ImDrawList reaches the description card
static void BlurCaptureCallback(const ImDrawList* parent_list, const ImDrawCmd* cmd) {
    (void)parent_list;
    (void)cmd;

    ID3D11Device* device = App_GetD3DDevice();
    if (!device || !s_initialized) return;

    ID3D11DeviceContext* ctx = nullptr;
    device->GetImmediateContext(&ctx);
    if (!ctx) return;

    // Get current swapchain backbuffer from active RTV
    ID3D11RenderTargetView* curRTV = nullptr;
    ID3D11DepthStencilView* curDSV = nullptr;
    ctx->OMGetRenderTargets(1, &curRTV, &curDSV);

    if (!curRTV) {
        ctx->Release();
        return;
    }

    ID3D11Resource* backBufferRes = nullptr;
    curRTV->GetResource(&backBufferRes);

    if (backBufferRes && s_texSource) {
        // Clamp copy box safely to backbuffer dimensions
        D3D11_BOX srcBox{};
        srcBox.left = (UINT)std::max(0, s_currentCallbackData.srcX);
        srcBox.top = (UINT)std::max(0, s_currentCallbackData.srcY);
        srcBox.front = 0;
        srcBox.right = (UINT)std::max(srcBox.left + 1, (UINT)(s_currentCallbackData.srcX + s_currentCallbackData.srcW));
        srcBox.bottom = (UINT)std::max(srcBox.top + 1, (UINT)(s_currentCallbackData.srcY + s_currentCallbackData.srcH));
        srcBox.back = 1;

        // Copy screen region into source texture
        ctx->CopySubresourceRegion(s_texSource, 0, 0, 0, 0, backBufferRes, 0, &srcBox);
        backBufferRes->Release();

        // Save viewport
        UINT numVp = 1;
        D3D11_VIEWPORT savedVp{};
        ctx->RSGetViewports(&numVp, &savedVp);

        // Setup blur viewport
        D3D11_VIEWPORT blurVp{};
        blurVp.Width = (float)kBlurTexW;
        blurVp.Height = (float)kBlurTexH;
        blurVp.MinDepth = 0.0f;
        blurVp.MaxDepth = 1.0f;
        ctx->RSSetViewports(1, &blurVp);

        // Set shader pipeline
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(s_vs, nullptr, 0);
        ctx->PSSetShader(s_ps, nullptr, 0);
        ctx->PSSetSamplers(0, 1, &s_sampler);
        ctx->PSSetConstantBuffers(0, 1, &s_cbParams);

        // Kawase Pass 1: Source -> Ping (offset 1.5)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(ctx->Map(s_cbParams, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                BlurParamsCB* cb = (BlurParamsCB*)mapped.pData;
                cb->invTargetSize[0] = 1.0f / (float)kBlurTexW;
                cb->invTargetSize[1] = 1.0f / (float)kBlurTexH;
                cb->offset = 1.5f;
                cb->padding = 0.0f;
                ctx->Unmap(s_cbParams, 0);
            }

            ID3D11RenderTargetView* rtvTarget = s_rtvPing;
            ctx->OMSetRenderTargets(1, &rtvTarget, nullptr);
            ctx->PSSetShaderResources(0, 1, &s_srvSource);
            ctx->Draw(3, 0);
        }

        // Kawase Pass 2: Ping -> Pong (offset 2.5)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(ctx->Map(s_cbParams, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                BlurParamsCB* cb = (BlurParamsCB*)mapped.pData;
                cb->invTargetSize[0] = 1.0f / (float)kBlurTexW;
                cb->invTargetSize[1] = 1.0f / (float)kBlurTexH;
                cb->offset = 2.5f;
                cb->padding = 0.0f;
                ctx->Unmap(s_cbParams, 0);
            }

            ID3D11ShaderResourceView* nullSRV = nullptr;
            ctx->PSSetShaderResources(0, 1, &nullSRV);

            ID3D11RenderTargetView* rtvTarget = s_rtvPong;
            ctx->OMSetRenderTargets(1, &rtvTarget, nullptr);
            ctx->PSSetShaderResources(0, 1, &s_srvPing);
            ctx->Draw(3, 0);
        }

        // Kawase Pass 3: Pong -> Ping (offset 3.5, rich smooth blur)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(ctx->Map(s_cbParams, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                BlurParamsCB* cb = (BlurParamsCB*)mapped.pData;
                cb->invTargetSize[0] = 1.0f / (float)kBlurTexW;
                cb->invTargetSize[1] = 1.0f / (float)kBlurTexH;
                cb->offset = 3.5f;
                cb->padding = 0.0f;
                ctx->Unmap(s_cbParams, 0);
            }

            ID3D11ShaderResourceView* nullSRV = nullptr;
            ctx->PSSetShaderResources(0, 1, &nullSRV);

            ID3D11RenderTargetView* rtvTarget = s_rtvPing;
            ctx->OMSetRenderTargets(1, &rtvTarget, nullptr);
            ctx->PSSetShaderResources(0, 1, &s_srvPong);
            ctx->Draw(3, 0);
        }

        // Unbind ping so ImGui can bind s_srvPing as texture
        ID3D11RenderTargetView* nullRTV = nullptr;
        ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx->PSSetShaderResources(0, 1, &nullSRV);

        // Restore original render target and viewport
        ctx->OMSetRenderTargets(1, &curRTV, curDSV);
        ctx->RSSetViewports(1, &savedVp);
    }

    curRTV->Release();
    if (curDSV) curDSV->Release();
    ctx->Release();
}

// Truncates string with "..." if its rendered width exceeds maxWidth
static std::string TruncateToWidth(const std::string& str, float maxWidth) {
    if (str.empty()) return "";
    float fullW = ImGui::CalcTextSize(str.c_str()).x;
    if (fullW <= maxWidth) return str;

    const std::string ellipsis = "...";
    float ellipsisW = ImGui::CalcTextSize(ellipsis.c_str()).x;
    float budget = maxWidth - ellipsisW;
    if (budget <= 0.0f) return ellipsis;

    // Binary search for exact fitting length
    size_t low = 1, high = str.length();
    size_t best = 1;
    while (low <= high) {
        size_t mid = low + (high - low) / 2;
        std::string sub = str.substr(0, mid);
        if (ImGui::CalcTextSize(sub.c_str()).x <= budget) {
            best = mid;
            low = mid + 1;
        } else {
            if (mid == 0) break;
            high = mid - 1;
        }
    }
    return str.substr(0, best) + ellipsis;
}

// Path truncation with middle ellipsis (e.g. C:\Users\...\video.mp4)
static std::string TruncatePathToWidth(const std::string& path, float maxWidth) {
    if (path.empty()) return "";
    if (ImGui::CalcTextSize(path.c_str()).x <= maxWidth) return path;

    size_t lastSlash = path.find_last_of("\\/");
    std::string filePart = (lastSlash != std::string::npos) ? path.substr(lastSlash) : path;
    std::string prefixPart = (lastSlash != std::string::npos) ? path.substr(0, std::min<size_t>(12, lastSlash)) : "";

    std::string candidate = prefixPart + "\\..." + filePart;
    if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
        return candidate;

    return TruncateToWidth(path, maxWidth);
}

void RenderDescriptionCard(
    const VideoItem& video,
    ImVec2 cardPos,
    ImVec2 cardSize,
    ImVec2 viewportMin,
    ImVec2 viewportMax
) {
    if (!s_initialized) {
        DescriptionCard_Init(App_GetD3DDevice());
    }

    // --- Compact, Fixed Single-Line Dimensions ---
    float tipW = std::clamp(cardSize.x * 1.50f, 360.0f, 440.0f);
    const float tipH = 76.0f; // Exactly fits 3 crisp single lines
    const float cardRounding = 12.0f;

    // 1. Horizontal Boundary Positioning
    float toolX = cardPos.x;
    if (toolX + tipW > viewportMax.x) {
        toolX = viewportMax.x - tipW;
    }
    if (toolX < viewportMin.x) {
        toolX = viewportMin.x;
    }

    // 2. Vertical Boundary Positioning: prefer below card, flip above if nearing bottom
    float toolY = cardPos.y + cardSize.y + 6.0f;
    if (toolY + tipH > viewportMax.y) {
        toolY = cardPos.y - tipH - 6.0f; // Flip directly above the card
        if (toolY < viewportMin.y) {
            toolY = std::clamp(cardPos.y, viewportMin.y, viewportMax.y - tipH);
        }
    }

    ImVec2 tipMin = ImVec2(toolX, toolY);
    ImVec2 tipMax = ImVec2(toolX + tipW, toolY + tipH);

    // Record capture coordinates for GPU blur pass
    s_currentCallbackData.srcX = (int)tipMin.x;
    s_currentCallbackData.srcY = (int)tipMin.y;
    s_currentCallbackData.srcW = (int)tipW;
    s_currentCallbackData.srcH = (int)tipH;

    // Render directly on ForegroundDrawList: never clips, never gets a scrollbar
    ImDrawList* fgList = ImGui::GetForegroundDrawList();

    // Step A: Trigger GPU Dual-Kawase blur callback on the backbuffer region
    fgList->AddCallback(BlurCaptureCallback, nullptr);
    fgList->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);

    // Step B: Draw Blurred Frosted Glass Background
    if (s_srvPing) {
        fgList->AddImageRounded(
            (ImTextureID)s_srvPing,
            tipMin, tipMax,
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
            IM_COL32_WHITE, cardRounding
        );
    }

    // Step C: Translucent Midnight Dark Acrylic Tint (lets blur shine through with rich dark tone)
    fgList->AddRectFilled(
        tipMin, tipMax,
        IM_COL32(14, 14, 18, 175),
        cardRounding
    );

    // Step D: Frosted Micro-Grain Texture
    if (s_srvNoise) {
        fgList->AddImageRounded(
            (ImTextureID)s_srvNoise,
            tipMin, tipMax,
            ImVec2(0.0f, 0.0f), ImVec2(tipW / 64.0f, tipH / 64.0f),
            IM_COL32(255, 255, 255, 14),
            cardRounding
        );
    }

    // Step E: Top-Lit Acrylic Highlight Border
    fgList->AddRect(
        tipMin, tipMax,
        IM_COL32(255, 255, 255, 38),
        cardRounding, 0, 1.2f
    );

    // --- Step F: Render 3 Clean Single Lines with Strict Truncation ---
    const float padX = 14.0f;
    const float textAvailW = tipW - padX * 2.0f;

    // Line 1: File Name (Bright white, single-line)
    std::string lineTitle = TruncateToWidth(video.fileName, textAvailW);
    fgList->AddText(
        ImVec2(tipMin.x + padX, tipMin.y + 10.0f),
        IM_COL32(245, 245, 250, 255),
        lineTitle.c_str()
    );

    // Line 2: Folder & Size Metadata (Muted grey, single-line)
    std::string metaStr = "Folder: " + video.category + "    |    Size: " + video.sizeFormatted;
    std::string lineMeta = TruncateToWidth(metaStr, textAvailW);
    fgList->AddText(
        ImVec2(tipMin.x + padX, tipMin.y + 31.0f),
        IM_COL32(165, 165, 175, 255),
        lineMeta.c_str()
    );

    // Line 3: Full Path (Subtle disabled grey, single-line)
    std::string linePath = TruncatePathToWidth(video.fullPath, textAvailW);
    fgList->AddText(
        ImVec2(tipMin.x + padX, tipMin.y + 51.0f),
        IM_COL32(130, 130, 140, 255),
        linePath.c_str()
    );
}
