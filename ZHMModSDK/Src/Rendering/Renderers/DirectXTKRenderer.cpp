#include <directx/d3dx12.h>

#include "DirectXTKRenderer.h"

#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include "Hooks.h"
#include "Logging.h"

#include <Glacier/ZApplicationEngineWin32.h>
#include <Glacier/ZDelegate.h>

#include <UI/Console.h>

#include "CommonStates.h"
#include "ResourceUploadBatch.h"
#include "DirectXHelpers.h"
#include "Functions.h"
#include "Globals.h"
#include "Glacier/ZEntity.h"
#include "Glacier/ZActor.h"
#include "Glacier/ZRender.h"
#include "Glacier/ZCameraEntity.h"
#include "D3DUtils.h"
#include "Fonts.h"
#include "ModSDK.h"
#include "Glacier/ZGameLoopManager.h"
#include "Glacier/MDF_FONT.h"

using namespace Rendering::Renderers;

DirectXTKRenderer::DirectXTKRenderer() {}

DirectXTKRenderer::~DirectXTKRenderer() {
    const ZMemberDelegate<DirectXTKRenderer, void(const SGameUpdateEvent&)> s_Delegate(
        this, &DirectXTKRenderer::OnFrameUpdate
    );
    Globals::GameLoopManager->UnregisterFrameUpdate(s_Delegate, 1, EUpdateMode::eUpdateAlways);

    OnReset();

    if (m_CommandQueue) {
        m_CommandQueue->Release();
        m_CommandQueue = nullptr;
    }
}

void DirectXTKRenderer::OnEngineInit() {
    const ZMemberDelegate<DirectXTKRenderer, void(const SGameUpdateEvent&)> s_Delegate(
        this, &DirectXTKRenderer::OnFrameUpdate
    );
    Globals::GameLoopManager->RegisterFrameUpdate(s_Delegate, INT_MAX, EUpdateMode::eUpdateAlways);
}

void DirectXTKRenderer::OnFrameUpdate(const SGameUpdateEvent& p_UpdateEvent) {}

void DirectXTKRenderer::Draw() {
    const auto s_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
    const auto s_RtvHandle = m_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    const CD3DX12_CPU_DESCRIPTOR_HANDLE s_RtvDescriptor(s_RtvHandle, s_BackBufferIndex, m_RtvDescriptorSize);

    // Use depth buffer from the game.
    // TODO: Remove hardcoded 12 index. Allow rendering both with and without depth clipping.
    /*const auto s_DsvHandle = Globals::RenderManager->m_pDevice->m_pDescriptorHeapDSV->GetCPUDescriptorHandleForHeapStart();
    const CD3DX12_CPU_DESCRIPTOR_HANDLE s_DsvDescriptor(s_DsvHandle, 12, m_DsvDescriptorSize);

    Logger::Debug("DSV descriptor handle: {:X}", s_DsvDescriptor.ptr);*/

    //m_CommandList->OMSetRenderTargets(1, &s_RtvDescriptor, false, &s_DsvDescriptor);
    m_CommandList->OMSetRenderTargets(1, &s_RtvDescriptor, false, nullptr);

    ID3D12DescriptorHeap* s_Heaps[] = { m_ResourceDescriptors->Heap() };
    m_CommandList->SetDescriptorHeaps(static_cast<UINT>(std::size(s_Heaps)), s_Heaps);

    m_CurrentPrimitiveType = PrimitiveType::Triangle;

    //m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    //m_TextEffect->Apply(m_CommandList);
    //m_LineEffect->Apply(m_CommandList);
    m_TriangleEffect->Apply(m_CommandList);

    m_PrimitiveBatch->Begin(m_CommandList);

    ModSDK::GetInstance()->OnDraw3D();

    m_PrimitiveBatch->End();

    m_CurrentPrimitiveType = PrimitiveType::Line;

    //m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

    m_LineEffect->Apply(m_CommandList);

    m_PrimitiveBatch->Begin(m_CommandList);

    ModSDK::GetInstance()->OnDraw3D();

    m_PrimitiveBatch->End();

    /*m_SpriteBatch->Begin(m_CommandList);

    m_CurrentPrimitiveType = PrimitiveType::Text2D;

    ModSDK::GetInstance()->OnDraw3D();

    m_SpriteBatch->End();*/
}

void DirectXTKRenderer::DepthDraw() {
    const auto s_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

    const auto s_RtvHandle = m_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    const CD3DX12_CPU_DESCRIPTOR_HANDLE s_RtvDescriptor(s_RtvHandle, s_BackBufferIndex, m_RtvDescriptorSize);

    const auto s_DsvHandle = Globals::RenderManager->m_pDevice->m_pDescriptorHeapDSV->GetCPUDescriptorHandleForHeapStart();
    const CD3DX12_CPU_DESCRIPTOR_HANDLE s_DsvDescriptor(s_DsvHandle, *m_DsvIndex, m_DsvDescriptorSize);

    m_CommandList->OMSetRenderTargets(1, &s_RtvDescriptor, false, &s_DsvDescriptor);

    ID3D12DescriptorHeap* s_Heaps[] = { m_ResourceDescriptors->Heap(), commonStates->Heap() };
    m_CommandList->SetDescriptorHeaps(static_cast<UINT>(std::size(s_Heaps)), s_Heaps);

    m_CurrentPrimitiveType = PrimitiveType::Triangle;

    m_TriangleEffect->Apply(m_CommandList);

    //m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_PrimitiveBatch->Begin(m_CommandList);

    ModSDK::GetInstance()->OnDepthDraw3D();

    m_PrimitiveBatch->End();

    m_CurrentPrimitiveType = PrimitiveType::Line;

    m_LineEffect->Apply(m_CommandList);

    //m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

    m_PrimitiveBatch->Begin(m_CommandList);

    ModSDK::GetInstance()->OnDepthDraw3D();

    m_PrimitiveBatch->End();

    m_CurrentPrimitiveType = PrimitiveType::Text3D;

    m_TextEffect->Apply(m_CommandList);

    m_CommandList->SetPipelineState(pipelineState);

    m_TextBatch->Begin(m_CommandList);

    ModSDK::GetInstance()->OnDepthDraw3D();

    m_TextBatch->End();
}

void DirectXTKRenderer::OnPresent(IDXGISwapChain3* p_SwapChain) {
    if (!m_CommandQueue)
        return;

    if (!SetupRenderer(p_SwapChain)) {
        Logger::Error("Failed to set up DirectXTK renderer.");
        OnReset();
        return;
    }

    // Get context of next frame to render.
    auto& s_FrameCtx = m_FrameContext[++m_FrameCounter % m_FrameContext.size()];

    // If this context is still being rendered, we should wait for it.
    if (s_FrameCtx.FenceValue != 0 && s_FrameCtx.FenceValue > m_Fence->GetCompletedValue()) {
        BreakIfFailed(m_Fence->SetEventOnCompletion(s_FrameCtx.FenceValue, m_FenceEvent.Handle));
        WaitForSingleObject(m_FenceEvent.Handle, INFINITE);
    }

    const auto s_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();

    // Reset command list and allocator.
    s_FrameCtx.CommandAllocator->Reset();
    BreakIfFailed(m_CommandList->Reset(s_FrameCtx.CommandAllocator, nullptr));

    // Transition the render target into the correct state to allow for drawing into it.
    const D3D12_RESOURCE_BARRIER s_RTBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_BackBuffers[s_BackBufferIndex],
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );

    m_CommandList->ResourceBarrier(1, &s_RTBarrier);

    // Update camera matrices.
    const auto s_CameraRight = Globals::RenderManager->m_pDevice->m_Constants.cameraRight;
    const auto s_CameraUp = Globals::RenderManager->m_pDevice->m_Constants.cameraUp;
    const auto s_CameraFwd = Globals::RenderManager->m_pDevice->m_Constants.cameraFwd;
    const auto s_CameraPos = Globals::RenderManager->m_pDevice->m_Constants.cameraPos;

    const auto s_CameraView = SMatrix {
        {s_CameraRight.x, s_CameraRight.y, s_CameraRight.z, 0.f},
        {s_CameraUp.x, s_CameraUp.y, s_CameraUp.z, 0.f},
        {-s_CameraFwd.x, -s_CameraFwd.y, -s_CameraFwd.z, 0.f},
        {s_CameraPos.x, s_CameraPos.y, s_CameraPos.z, 1.f}
    }.Inverse();

    m_View = *reinterpret_cast<DirectX::FXMMATRIX*>(&s_CameraView);
    m_Projection = *reinterpret_cast<DirectX::FXMMATRIX*>(&Globals::RenderManager->m_pDevice->m_Constants.
        cameraViewToClip);

    m_ViewProjection = m_View * m_Projection;
    m_ProjectionViewInverse = (m_Projection * m_View).Invert();

    if (m_RendererSetup) {
        m_LineEffect->SetView(m_View);
        m_LineEffect->SetProjection(m_Projection);

        m_TriangleEffect->SetView(m_View);
        m_TriangleEffect->SetProjection(m_Projection);
    }

    // Set up the viewport.
    D3D12_VIEWPORT s_Viewport = {0.0f, 0.0f, m_WindowWidth, m_WindowHeight, D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
    m_CommandList->RSSetViewports(1, &s_Viewport);

    D3D12_RECT s_ScissorRect = {0, 0, static_cast<LONG>(m_WindowWidth), static_cast<LONG>(m_WindowHeight)};
    m_CommandList->RSSetScissorRects(1, &s_ScissorRect);

    const auto s_RtvHandle = m_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    const CD3DX12_CPU_DESCRIPTOR_HANDLE s_RtvDescriptor(s_RtvHandle, s_BackBufferIndex, m_RtvDescriptorSize);

    // Use depth buffer from the game.
    // TODO: Allow rendering both with and without depth clipping.
    if (m_DsvIndex.has_value()) {
        const auto s_DsvHandle = Globals::RenderManager->m_pDevice->m_pDescriptorHeapDSV->
                                                         GetCPUDescriptorHandleForHeapStart();

        const CD3DX12_CPU_DESCRIPTOR_HANDLE s_DsvDescriptor(s_DsvHandle, *m_DsvIndex, m_DsvDescriptorSize);

        m_CommandList->OMSetRenderTargets(1, &s_RtvDescriptor, false, &s_DsvDescriptor);
    }
    else {
        m_CommandList->OMSetRenderTargets(1, &s_RtvDescriptor, false, nullptr);
    }

    //TEntityRef<ZHitman5> s_LocalHitman = SDK()->GetLocalPlayer();

    if (m_DsvIndex.has_value())
    {
        DepthDraw();
    }

    Draw();

    const D3D12_RESOURCE_BARRIER s_PresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_BackBuffers[s_BackBufferIndex],
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT
    );

    m_CommandList->ResourceBarrier(1, &s_PresentBarrier);
    BreakIfFailed(m_CommandList->Close());

    m_CommandQueue->ExecuteCommandLists(1, CommandListCast(&m_CommandList.Ref));
}

void DirectXTKRenderer::PostPresent(IDXGISwapChain3* p_SwapChain, HRESULT p_PresentResult) {
    if (!m_RendererSetup || !m_CommandQueue)
        return;

    if (p_PresentResult == DXGI_ERROR_DEVICE_REMOVED || p_PresentResult == DXGI_ERROR_DEVICE_RESET) {
        Logger::Error("Device lost after present.");
        abort();
    }
    else {
        m_GraphicsMemory->Commit(m_CommandQueue);

        FrameContext& s_FrameCtx = m_FrameContext[m_FrameCounter % MaxRenderedFrames];

        // Update the fence value for this frame and ask to receive a signal with this
        // fence value as soon as the GPU has finished rendering the frame. We update this
        // monotonically in order to always have the latest number represent the most
        // recently submitted frame, and in order to avoid having multiple frames share
        // the same fence value.
        s_FrameCtx.FenceValue = ++m_FenceValue;
        BreakIfFailed(m_CommandQueue->Signal(m_Fence, s_FrameCtx.FenceValue));
    }
}

void DirectXTKRenderer::WaitForCurrentFrameToFinish() const {
    if (m_FenceValue != 0 && m_FenceValue > m_Fence->GetCompletedValue()) {
        BreakIfFailed(m_Fence->SetEventOnCompletion(m_FenceValue, m_FenceEvent.Handle));
        WaitForSingleObject(m_FenceEvent.Handle, INFINITE);
    }
}

bool DirectXTKRenderer::CompileShaderFromString(const std::string& shaderCode, const std::string& entryPoint, const std::string& shaderModel, ID3DBlob** shaderBlob) {
    UINT compileFlags = 0;

    // Enable debug flags in debug builds
#if defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ScopedD3DRef<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompile(
        shaderCode.c_str(),
        shaderCode.size(),
        nullptr,
        nullptr,
        nullptr,
        entryPoint.c_str(),
        shaderModel.c_str(),
        compileFlags,
        0,
        shaderBlob,
        &errorBlob.Ref);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            Logger::Error("{}", static_cast<const char*>(errorBlob->GetBufferPointer()));
        }

        return false;
    }

    return true;
}

bool DirectXTKRenderer::CreateFontDistanceFieldTexture() {
    ScopedD3DRef<ID3D12Device> s_Device;

    if (m_SwapChain->GetDevice(REF_IID_PPV_ARGS(s_Device)) != S_OK)
    {
        return false;
    }

    D3D12_RESOURCE_DESC s_TextureDesc = {};

    s_TextureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    s_TextureDesc.Width = 1024;
    s_TextureDesc.Height = 384;
    s_TextureDesc.DepthOrArraySize = 1;
    s_TextureDesc.MipLevels = 1;
    s_TextureDesc.Format = DXGI_FORMAT_R8_UNORM;
    s_TextureDesc.SampleDesc.Count = 1;
    s_TextureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    s_TextureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);

    HRESULT hr = s_Device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &s_TextureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_FontDistanceFieldTexture.Ref));

    if (FAILED(hr))
    {
        Logger::Error("Unable to create font distance field texture!");

        return false;
    }

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_FontDistanceFieldTexture.Ref, 0, 1);

    CD3DX12_HEAP_PROPERTIES heapProperties2(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
    Microsoft::WRL::ComPtr<ID3D12Resource> textureUploadHeap;

    hr = s_Device->CreateCommittedResource(
        &heapProperties2,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&textureUploadHeap));

    if (FAILED(hr))
    {
        Logger::Error("Unable to create texture upload heap!");

        return false;
    }

    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = MDF_FONT::g_DistanceField;
    textureData.RowPitch = 1024;
    textureData.SlicePitch = textureData.RowPitch * 384;

    UpdateSubresources(m_CommandList.Ref, m_FontDistanceFieldTexture.Ref, textureUploadHeap.Get(), 0, 0, 1, &textureData);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_FontDistanceFieldTexture.Ref,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_CommandList->ResourceBarrier(1, &barrier);

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};

    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = s_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SrvDescriptorHeap.Ref));

    if (FAILED(hr))
    {
        Logger::Error("Unable to create SRV descriptor heap!");
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = s_TextureDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    s_Device->CreateShaderResourceView(m_FontDistanceFieldTexture.Ref, &srvDesc, m_SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    return true;
}

bool DirectXTKRenderer::SetupRenderer(IDXGISwapChain3* p_SwapChain) {
    if (m_RendererSetup)
        return true;

    Logger::Debug("Setting up DirectXTK renderer.");

    ScopedD3DRef<ID3D12Device> s_Device;

    if (p_SwapChain->GetDevice(REF_IID_PPV_ARGS(s_Device)) != S_OK)
        return false;

    DXGI_SWAP_CHAIN_DESC1 s_SwapChainDesc;

    if (p_SwapChain->GetDesc1(&s_SwapChainDesc) != S_OK)
        return false;

    m_SwapChain = p_SwapChain;

    const auto s_BufferCount = s_SwapChainDesc.BufferCount;

    {
        D3D12_DESCRIPTOR_HEAP_DESC s_Desc = {};
        s_Desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        s_Desc.NumDescriptors = s_BufferCount;
        s_Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        s_Desc.NodeMask = 0;

        if (s_Device->CreateDescriptorHeap(&s_Desc, IID_PPV_ARGS(m_RtvDescriptorHeap.ReleaseAndGetPtr())) != S_OK)
            return false;

        D3D_SET_OBJECT_NAME_A(m_RtvDescriptorHeap, "ZHMModSDK DirectXTK Rtv Descriptor Heap");
    }

    m_FrameContext.clear();

    for (UINT i = 0; i < MaxRenderedFrames; ++i) {
        FrameContext s_Frame {};

        if (s_Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(s_Frame.CommandAllocator.ReleaseAndGetPtr())
        ) != S_OK)
            return false;

        char s_CmdAllocDebugName[128];
        sprintf_s(s_CmdAllocDebugName, sizeof(s_CmdAllocDebugName), "ZHMModSDK DirectXTK Command Allocator #%u", i);
        D3D_SET_OBJECT_NAME_A(s_Frame.CommandAllocator, s_CmdAllocDebugName);

        s_Frame.FenceValue = 0;

        m_FrameContext.push_back(std::move(s_Frame));
    }

    // Create RTVs for back buffers.
    m_BackBuffers.clear();
    m_BackBuffers.resize(s_BufferCount);

    m_RtvDescriptorSize = s_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_DsvDescriptorSize = s_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    const auto s_RtvHandle = m_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < s_BufferCount; ++i) {
        if (p_SwapChain->GetBuffer(i, IID_PPV_ARGS(m_BackBuffers[i].ReleaseAndGetPtr())) != S_OK)
            return false;

        const CD3DX12_CPU_DESCRIPTOR_HANDLE s_RtvDescriptor(s_RtvHandle, i, m_RtvDescriptorSize);
        s_Device->CreateRenderTargetView(m_BackBuffers[i], nullptr, s_RtvDescriptor);
    }

    if (s_Device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_FrameContext[0].CommandAllocator, nullptr,
            IID_PPV_ARGS(m_CommandList.ReleaseAndGetPtr())
        ) != S_OK ||
        m_CommandList->Close() != S_OK)
        return false;

    char s_CmdListDebugName[128];
    sprintf_s(s_CmdListDebugName, sizeof(s_CmdListDebugName), "ZHMModSDK DirectXTK Command List");
    D3D_SET_OBJECT_NAME_A(m_CommandList, s_CmdListDebugName);

    if (s_Device->CreateFence(m_FenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_Fence.ReleaseAndGetPtr())) != S_OK)
        return false;

    char s_FenceDebugName[128];
    sprintf_s(s_FenceDebugName, sizeof(s_FenceDebugName), "ZHMModSDK DirectXTK Fence");
    D3D_SET_OBJECT_NAME_A(m_Fence, s_FenceDebugName);

    m_FenceEvent = CreateEventW(nullptr, false, false, nullptr);

    if (!m_FenceEvent)
        return false;

    if (p_SwapChain->GetHwnd(&m_Hwnd) != S_OK)
        return false;

    RECT s_Rect = {0, 0, 0, 0};
    GetClientRect(m_Hwnd, &s_Rect);

    m_WindowWidth = static_cast<float>(s_Rect.right - s_Rect.left);
    m_WindowHeight = static_cast<float>(s_Rect.bottom - s_Rect.top);

    m_GraphicsMemory = std::make_unique<DirectX::GraphicsMemory>(s_Device);

    m_PrimitiveBatch = std::make_unique<DirectX::PrimitiveBatch<DirectX::VertexPositionColor>>(s_Device);

    DirectX::RenderTargetState s_RtState(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT_S8X24_UINT);

    {
        D3D12_BLEND_DESC s_AlphaBlend = {};

        s_AlphaBlend.AlphaToCoverageEnable = FALSE;
        s_AlphaBlend.IndependentBlendEnable = FALSE;
        s_AlphaBlend.RenderTarget[0].BlendEnable = TRUE;
        s_AlphaBlend.RenderTarget[0].LogicOpEnable = FALSE;
        s_AlphaBlend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        s_AlphaBlend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        s_AlphaBlend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        s_AlphaBlend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        s_AlphaBlend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        s_AlphaBlend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        s_AlphaBlend.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        s_AlphaBlend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;

        DirectX::EffectPipelineStateDescription s_Desc(
            &DirectX::VertexPositionColor::InputLayout,
            s_AlphaBlend,
            DirectX::CommonStates::DepthReverseZ,
            DirectX::CommonStates::CullNone,
            s_RtState,
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);

        m_TriangleEffect = std::make_unique<DirectX::BasicEffect>(s_Device, DirectX::EffectFlags::VertexColor, s_Desc);

        s_Desc.primitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

        m_LineEffect = std::make_unique<DirectX::BasicEffect>(s_Device, DirectX::EffectFlags::VertexColor, s_Desc);

        s_Desc.inputLayout = DirectX::VertexPositionColorTexture::InputLayout;
        s_Desc.depthStencilDesc = DirectX::CommonStates::DepthReadReverseZ;

        m_TextEffect = std::make_unique<DirectX::BasicEffect>(s_Device, DirectX::EffectFlags::VertexColor | DirectX::EffectFlags::Texture, s_Desc);

        m_TextBatch = std::make_unique<DirectX::PrimitiveBatch<DirectX::VertexPositionColorTexture>>(s_Device);

        if (!CreateFontDistanceFieldTexture())
        {
            return false;
        }

        commonStates = std::make_unique<DirectX::CommonStates>(s_Device.Ref);
        D3D12_GPU_DESCRIPTOR_HANDLE linearClampSamplerHandle = commonStates->LinearClamp();

        m_TextEffect->SetTexture(m_SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), linearClampSamplerHandle);

        const std::string debugRenderDistanceFieldFontVertexShader = R"(
			cbuffer Parameters : register(b0)
			{
				float4 DiffuseColor             : packoffset(c0);
				float3 EmissiveColor            : packoffset(c1);
				float3 SpecularColor            : packoffset(c2);
				float  SpecularPower            : packoffset(c2.w);

				float3 LightDirection[3]        : packoffset(c3);
				float3 LightDiffuseColor[3]     : packoffset(c6);
				float3 LightSpecularColor[3]    : packoffset(c9);

				float3 EyePosition              : packoffset(c12);

				float3 FogColor                 : packoffset(c13);
				float4 FogVector                : packoffset(c14);

				float4x4 World                  : packoffset(c15);
				float3x3 WorldInverseTranspose  : packoffset(c19);
				float4x4 WorldViewProj          : packoffset(c22);
			};

			struct VSInputTxVc
			{
				float4 Position : SV_Position;
				float2 TexCoord : TEXCOORD0;
				float4 Color    : COLOR;
			};

			struct VSOutputTxNoFog
			{
				float4 Diffuse    : COLOR0;
				float2 TexCoord   : TEXCOORD0;
				float4 PositionPS : SV_Position;
			};

			struct CommonVSOutput
			{
				float4 Pos_ps;
				float4 Diffuse;
				float3 Specular;
				float  FogFactor;
			};

			float ComputeFogFactor(float4 position)
			{
				return saturate(dot(position, FogVector));
			}

			CommonVSOutput ComputeCommonVSOutput(float4 position)
			{
				CommonVSOutput vout;

				vout.Pos_ps = mul(position, WorldViewProj);
				vout.Diffuse = DiffuseColor;
				vout.Specular = 0;
				vout.FogFactor = ComputeFogFactor(position);

				return vout;
			}

			#define SetCommonVSOutputParamsNoFog \
				vout.PositionPS = cout.Pos_ps; \
				vout.Diffuse = cout.Diffuse;

		    VSOutputTxNoFog VSBasicTxVcNoFog(VSInputTxVc vin)
			{
				VSOutputTxNoFog vout;

				CommonVSOutput cout = ComputeCommonVSOutput(vin.Position);
				SetCommonVSOutputParamsNoFog;

				vout.TexCoord = vin.TexCoord;
				vout.Diffuse *= vin.Color;

				return vout;
			}
		)";

        const std::string debugRenderDistanceFieldFontPixelShader = R"(
		    Texture2D<float4> mapDebug2DLinear : register(t0);
			sampler samplerLinearClamp : register(s0);

			struct PSInput
			{
				float4 color : COLOR0;
				float2 uv : TEXCOORD0;
			};

			float4 mainPS(PSInput input) : SV_Target0
			{
				// Sample the font distance field texture
				float distance = mapDebug2DLinear.Sample(samplerLinearClamp, input.uv).x;

				// Shift and scale the distance to control edge sharpness
				const float edgeThreshold = -0.4f; // Shift distance for smoothing
				const float scaleFactor = 5.0f; // Scale to sharpen the edge
				distance = saturate((distance + edgeThreshold) * scaleFactor);

				// Further control the smoothness of the transition
				float smoothedAlpha = distance * distance * (-2.0f) + 3.0f * distance;

				// Output the final color with computed alpha
				float alpha = smoothedAlpha * distance;
    
				return float4(input.color.xyz, alpha);
			}
		)";

        ScopedD3DRef<ID3DBlob> vertexShaderBlob;
        ScopedD3DRef<ID3DBlob> pixelShaderBlob;

        if (!CompileShaderFromString(debugRenderDistanceFieldFontVertexShader, "VSBasicTxVcNoFog", "vs_5_0", &vertexShaderBlob.Ref))
        {
            return false;
        }

        if (!CompileShaderFromString(debugRenderDistanceFieldFontPixelShader, "mainPS", "ps_5_0", &pixelShaderBlob.Ref))
        {
            return false;
        }

        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

        CD3DX12_ROOT_PARAMETER rootParameters[RootParameterIndex::RootParameterCount] = {};
        rootParameters[RootParameterIndex::ConstantBuffer].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_ROOT_SIGNATURE_DESC rsigDesc = {};

        const CD3DX12_DESCRIPTOR_RANGE textureSRV(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        const CD3DX12_DESCRIPTOR_RANGE textureSampler(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);

        rootParameters[RootParameterIndex::TextureSRV].InitAsDescriptorTable(1, &textureSRV, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[RootParameterIndex::TextureSampler].InitAsDescriptorTable(1, &textureSampler, D3D12_SHADER_VISIBILITY_PIXEL);

        rsigDesc.Init(static_cast<UINT>(std::size(rootParameters)), rootParameters, 0, nullptr, rootSignatureFlags);

        ScopedD3DRef<ID3D12RootSignature> rootSignature;

        DirectX::CreateRootSignature(s_Device, &rsigDesc, &rootSignature.Ref);
        //mRootSignature = GetRootSignature(1, rsigDesc);

        D3D12_SHADER_BYTECODE vertexShader;
        D3D12_SHADER_BYTECODE pixelShader;

        vertexShader.pShaderBytecode = vertexShaderBlob->GetBufferPointer();
        vertexShader.BytecodeLength = vertexShaderBlob->GetBufferSize();

        pixelShader.pShaderBytecode = pixelShaderBlob->GetBufferPointer();
        pixelShader.BytecodeLength = pixelShaderBlob->GetBufferSize();

        s_Desc.CreatePipelineState(s_Device, rootSignature.Ref, vertexShader, pixelShader, &pipelineState.Ref);
    }

    {
        m_ResourceDescriptors = std::make_unique<DirectX::DescriptorHeap>(
            s_Device, static_cast<int>(Descriptors::Count)
        );

        DirectX::ResourceUploadBatch s_ResourceUpload(s_Device);

        s_ResourceUpload.Begin();

        m_Font = std::make_unique<DirectX::SpriteFont>(
            s_Device,
            s_ResourceUpload,
            RobotoRegularSpritefont_data,
            RobotoRegularSpritefont_size,
            m_ResourceDescriptors->GetCpuHandle(static_cast<int>(Descriptors::FontRegular)),
            m_ResourceDescriptors->GetGpuHandle(static_cast<int>(Descriptors::FontRegular))
        );

        DirectX::SpriteBatchPipelineStateDescription s_Desc(s_RtState);
        m_SpriteBatch = std::make_unique<DirectX::SpriteBatch>(s_Device, s_ResourceUpload, s_Desc);

        s_ResourceUpload.End(m_CommandQueue).wait();

        const D3D12_VIEWPORT s_Viewport = {0.0f, 0.0f, m_WindowWidth, m_WindowHeight, D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
        m_SpriteBatch->SetViewport(s_Viewport);
    }

    m_LineEffect->SetWorld(m_World);
    m_LineEffect->SetView(m_View);
    m_LineEffect->SetProjection(m_Projection);

    m_TriangleEffect->SetWorld(m_World);
    m_TriangleEffect->SetView(m_View);
    m_TriangleEffect->SetProjection(m_Projection);

    m_RendererSetup = true;

    Logger::Debug("DirectXTK renderer successfully set up.");

    return true;
}

void DirectXTKRenderer::OnReset() {
    if (!m_RendererSetup)
        return;

    WaitForCurrentFrameToFinish();

    // Reset all fence values to latest fence value since we don't
    // really care about tracking any previous frames after a reset.
    // We only care about the last submitted frame having completed
    // (which means that all the previous ones have too).
    for (auto& s_Frame : m_FrameContext)
        s_Frame.FenceValue = m_FenceValue;

    m_BackBuffers.clear();
}

void DirectXTKRenderer::PostReset() {
    if (!m_RendererSetup)
        return;

    DXGI_SWAP_CHAIN_DESC1 s_SwapChainDesc;

    if (m_SwapChain->GetDesc1(&s_SwapChainDesc) != S_OK)
        return;

    ScopedD3DRef<ID3D12Device> s_Device;

    if (m_SwapChain->GetDevice(REF_IID_PPV_ARGS(s_Device)) != S_OK)
        return;

    // Reset the back buffers.
    m_BackBuffers.resize(s_SwapChainDesc.BufferCount);

    m_RtvDescriptorSize = s_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_DsvDescriptorSize = s_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    const auto s_RtvHandle = m_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < m_BackBuffers.size(); ++i) {
        if (m_SwapChain->GetBuffer(i, IID_PPV_ARGS(m_BackBuffers[i].ReleaseAndGetPtr())) != S_OK)
            return;

        const CD3DX12_CPU_DESCRIPTOR_HANDLE s_RtvDescriptor(s_RtvHandle, i, m_RtvDescriptorSize);
        s_Device->CreateRenderTargetView(m_BackBuffers[i], nullptr, s_RtvDescriptor);
    }

    RECT s_Rect = {0, 0, 0, 0};
    GetClientRect(m_Hwnd, &s_Rect);

    m_WindowWidth = static_cast<float>(s_Rect.right - s_Rect.left);
    m_WindowHeight = static_cast<float>(s_Rect.bottom - s_Rect.top);

    const D3D12_VIEWPORT s_Viewport = {0.0f, 0.0f, m_WindowWidth, m_WindowHeight, D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
    m_SpriteBatch->SetViewport(s_Viewport);
}

void DirectXTKRenderer::SetCommandQueue(ID3D12CommandQueue* p_CommandQueue) {
    if (m_CommandQueue == p_CommandQueue)
        return;

    if (m_CommandQueue) {
        m_CommandQueue->Release();
        m_CommandQueue = nullptr;
    }

    Logger::Debug("Setting up DirectXTK12 command queue.");
    m_CommandQueue = p_CommandQueue;
    m_CommandQueue->AddRef();
}

void DirectXTKRenderer::DrawLine3D(
    const SVector3& p_From, const SVector3& p_To, const SVector4& p_FromColor, const SVector4& p_ToColor
) {
    if (!m_RendererSetup)
        return;

    DirectX::VertexPositionColor s_From(
        DirectX::SimpleMath::Vector3(p_From.x, p_From.y, p_From.z),
        DirectX::SimpleMath::Vector4(p_FromColor.x, p_FromColor.y, p_FromColor.z, p_FromColor.w)
    );

    DirectX::VertexPositionColor s_To(
        DirectX::SimpleMath::Vector3(p_To.x, p_To.y, p_To.z),
        DirectX::SimpleMath::Vector4(p_ToColor.x, p_ToColor.y, p_ToColor.z, p_ToColor.w)
    );

    m_PrimitiveBatch->DrawLine(s_From, s_To);
}

void DirectXTKRenderer::DrawText2D(
    const ZString& p_Text, const SVector2& p_Pos, const SVector4& p_Color, float p_Rotation/* = 0.f*/,
    float p_Scale/* = 1.f*/, TextAlignment p_Alignment /* = TextAlignment::Center*/
) {
    if (!m_RendererSetup)
        return;

    const std::string s_Text(p_Text.c_str(), p_Text.size());
    const DirectX::SimpleMath::Vector2 s_StringSize = m_Font->MeasureString(s_Text.c_str());

    DirectX::SimpleMath::Vector2 s_Origin(0.f, 0.f);

    if (p_Alignment == TextAlignment::Center)
        s_Origin.x = s_StringSize.x / 2.f;
    else if (p_Alignment == TextAlignment::Right)
        s_Origin.x = s_StringSize.x;

    m_Font->DrawString(
        m_SpriteBatch.get(),
        s_Text.c_str(),
        DirectX::SimpleMath::Vector2(p_Pos.x, p_Pos.y),
        DirectX::SimpleMath::Vector4(p_Color.x, p_Color.y, p_Color.z, p_Color.w),
        p_Rotation,
        s_Origin,
        p_Scale
    );
}

bool DirectXTKRenderer::WorldToScreen(const SVector3& p_WorldPos, SVector2& p_Out) {
    if (!m_RendererSetup)
        return false;

    const DirectX::SimpleMath::Vector4 s_World(p_WorldPos.x, p_WorldPos.y, p_WorldPos.z, 1.f);
    const DirectX::SimpleMath::Vector4 s_Projected = DirectX::XMVector4Transform(s_World, m_ViewProjection);

    if (s_Projected.w <= 0.000001f)
        return false;

    const float s_InvertedZ = 1.f / s_Projected.w;
    const DirectX::SimpleMath::Vector3 s_FinalProjected(
        s_Projected.x * s_InvertedZ, s_Projected.y * s_InvertedZ, s_Projected.z * s_InvertedZ
    );

    p_Out.x = (1.f + s_FinalProjected.x) * 0.5f * m_WindowWidth;
    p_Out.y = (1.f - s_FinalProjected.y) * 0.5f * m_WindowHeight;

    return true;
}

bool DirectXTKRenderer::ScreenToWorld(const SVector2& p_ScreenPos, SVector3& p_WorldPosOut, SVector3& p_DirectionOut) {
    if (!m_RendererSetup)
        return false;

    const auto s_CurrentCamera = Functions::GetCurrentCamera->Call();

    if (!s_CurrentCamera)
        return false;

    auto s_CameraTrans = s_CurrentCamera->GetWorldMatrix();

    auto s_ScreenPos = DirectX::SimpleMath::Vector3(
        (2.0f * p_ScreenPos.x) / m_WindowWidth - 1.0f, 1.0f - (2.0f * p_ScreenPos.y) / m_WindowHeight, 1.f
    );
    auto s_RayClip = DirectX::SimpleMath::Vector4(s_ScreenPos.x, s_ScreenPos.y, 0.f, 1.f);

    DirectX::SimpleMath::Vector4 s_RayEye = DirectX::XMVector4Transform(s_RayClip, m_Projection.Invert());
    s_RayEye.z = -1.f;
    s_RayEye.w = 0.f;

    DirectX::SimpleMath::Vector4 s_RayWorld = DirectX::XMVector4Transform(s_RayEye, m_View.Invert());
    s_RayWorld.Normalize();

    p_WorldPosOut.x = s_CameraTrans.Trans.x + s_RayWorld.x;
    p_WorldPosOut.y = s_CameraTrans.Trans.y + s_RayWorld.y;
    p_WorldPosOut.z = s_CameraTrans.Trans.z + s_RayWorld.z;

    p_DirectionOut.x = s_RayWorld.x;
    p_DirectionOut.y = s_RayWorld.y;
    p_DirectionOut.z = s_RayWorld.z;

    return true;
}

void DirectXTKRenderer::DrawBox3D(const SVector3& p_Min, const SVector3& p_Max, const SVector4& p_Color) {
    SVector3 s_Corners[] = {
        SVector3(p_Min.x, p_Min.y, p_Min.z),
        SVector3(p_Min.x, p_Max.y, p_Min.z),
        SVector3(p_Max.x, p_Max.y, p_Min.z),
        SVector3(p_Max.x, p_Min.y, p_Min.z),
        SVector3(p_Max.x, p_Max.y, p_Max.z),
        SVector3(p_Min.x, p_Max.y, p_Max.z),
        SVector3(p_Min.x, p_Min.y, p_Max.z),
        SVector3(p_Max.x, p_Min.y, p_Max.z),
    };

    DrawLine3D(s_Corners[0], s_Corners[1], p_Color, p_Color);
    DrawLine3D(s_Corners[1], s_Corners[2], p_Color, p_Color);
    DrawLine3D(s_Corners[2], s_Corners[3], p_Color, p_Color);
    DrawLine3D(s_Corners[3], s_Corners[0], p_Color, p_Color);

    DrawLine3D(s_Corners[4], s_Corners[5], p_Color, p_Color);
    DrawLine3D(s_Corners[5], s_Corners[6], p_Color, p_Color);
    DrawLine3D(s_Corners[6], s_Corners[7], p_Color, p_Color);
    DrawLine3D(s_Corners[7], s_Corners[4], p_Color, p_Color);

    DrawLine3D(s_Corners[1], s_Corners[5], p_Color, p_Color);
    DrawLine3D(s_Corners[0], s_Corners[6], p_Color, p_Color);

    DrawLine3D(s_Corners[2], s_Corners[4], p_Color, p_Color);
    DrawLine3D(s_Corners[3], s_Corners[7], p_Color, p_Color);
}

inline SVector3 XMVecToSVec3(const DirectX::XMVECTOR& p_Vec) {
    return SVector3(DirectX::XMVectorGetX(p_Vec), DirectX::XMVectorGetY(p_Vec), DirectX::XMVectorGetZ(p_Vec));
}

void DirectXTKRenderer::DrawOBB3D(
    const SVector3& p_Min, const SVector3& p_Max, const SMatrix& p_Transform, const SVector4& p_Color
) {
    const auto s_Transform = *reinterpret_cast<DirectX::FXMMATRIX*>(&p_Transform);

    DirectX::XMVECTOR s_Corners[] = {
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Min.x, p_Min.y, p_Min.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Min.x, p_Max.y, p_Min.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Max.x, p_Max.y, p_Min.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Max.x, p_Min.y, p_Min.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Max.x, p_Max.y, p_Max.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Min.x, p_Max.y, p_Max.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Min.x, p_Min.y, p_Max.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Max.x, p_Min.y, p_Max.z), s_Transform),
    };

    DrawLine3D(XMVecToSVec3(s_Corners[0]), XMVecToSVec3(s_Corners[1]), p_Color, p_Color);
    DrawLine3D(XMVecToSVec3(s_Corners[1]), XMVecToSVec3(s_Corners[2]), p_Color, p_Color);
    DrawLine3D(XMVecToSVec3(s_Corners[2]), XMVecToSVec3(s_Corners[3]), p_Color, p_Color);
    DrawLine3D(XMVecToSVec3(s_Corners[3]), XMVecToSVec3(s_Corners[0]), p_Color, p_Color);

    DrawLine3D(XMVecToSVec3(s_Corners[4]), XMVecToSVec3(s_Corners[5]), p_Color, p_Color);
    DrawLine3D(XMVecToSVec3(s_Corners[5]), XMVecToSVec3(s_Corners[6]), p_Color, p_Color);
    DrawLine3D(XMVecToSVec3(s_Corners[6]), XMVecToSVec3(s_Corners[7]), p_Color, p_Color);
    DrawLine3D(XMVecToSVec3(s_Corners[7]), XMVecToSVec3(s_Corners[4]), p_Color, p_Color);

    DrawLine3D(XMVecToSVec3(s_Corners[1]), XMVecToSVec3(s_Corners[5]), p_Color, p_Color);
    DrawLine3D(XMVecToSVec3(s_Corners[0]), XMVecToSVec3(s_Corners[6]), p_Color, p_Color);

    DrawLine3D(XMVecToSVec3(s_Corners[2]), XMVecToSVec3(s_Corners[4]), p_Color, p_Color);
    DrawLine3D(XMVecToSVec3(s_Corners[3]), XMVecToSVec3(s_Corners[7]), p_Color, p_Color);
}

void DirectXTKRenderer::DrawBoundingQuads(const SVector3& p_Min, const SVector3& p_Max, const SMatrix& p_Transform, const SVector4& p_Color) {
    // Transform matrix for the bounding box
    const auto s_Transform = *reinterpret_cast<DirectX::FXMMATRIX*>(&p_Transform);

    // Define the 8 corners of the bounding box in local space, similar to DrawOBB3D
    DirectX::XMVECTOR s_Corners[] = {
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Min.x, p_Min.y, p_Min.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Min.x, p_Max.y, p_Min.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Max.x, p_Max.y, p_Min.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Max.x, p_Min.y, p_Min.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Max.x, p_Max.y, p_Max.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Min.x, p_Max.y, p_Max.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Min.x, p_Min.y, p_Max.z), s_Transform),
        DirectX::XMVector3Transform(DirectX::SimpleMath::Vector3(p_Max.x, p_Min.y, p_Max.z), s_Transform),
    };

    // Lambda to draw a quad as two triangles using SVector3
    auto drawQuad = [&](int i0, int i1, int i2, int i3) {
        SVector3 v0(s_Corners[i0].m128_f32[0], s_Corners[i0].m128_f32[1], s_Corners[i0].m128_f32[2]);
        SVector3 v1(s_Corners[i1].m128_f32[0], s_Corners[i1].m128_f32[1], s_Corners[i1].m128_f32[2]);
        SVector3 v2(s_Corners[i2].m128_f32[0], s_Corners[i2].m128_f32[1], s_Corners[i2].m128_f32[2]);
        SVector3 v3(s_Corners[i3].m128_f32[0], s_Corners[i3].m128_f32[1], s_Corners[i3].m128_f32[2]);

        DrawTriangle3D(v0, p_Color, v1, p_Color, v2, p_Color);
        DrawTriangle3D(v0, p_Color, v2, p_Color, v3, p_Color);
        };

    // Draw each face of the bounding box as quads
    drawQuad(0, 1, 2, 3);// Front face
    drawQuad(4, 5, 6, 7);// Back face
    drawQuad(0, 1, 5, 6);// Left face
    drawQuad(2, 3, 7, 4);// Right face
    drawQuad(1, 2, 4, 5);// Top face
    drawQuad(0, 3, 7, 6);// Bottom face
}

void Rendering::Renderers::DirectXTKRenderer::DrawTriangle3D(const SVector3& p_V1, const SVector4& p_Color1, const SVector3& p_V2, const SVector4& p_Color2, const SVector3& p_V3, const SVector4& p_Color3) {
    m_PrimitiveBatch->DrawTriangle(
        DirectX::VertexPositionColor(DirectX::SimpleMath::Vector3(p_V1.x, p_V1.y, p_V1.z), DirectX::SimpleMath::Vector4(p_Color1.x, p_Color1.y, p_Color1.z, p_Color1.w)),
        DirectX::VertexPositionColor(DirectX::SimpleMath::Vector3(p_V2.x, p_V2.y, p_V2.z), DirectX::SimpleMath::Vector4(p_Color2.x, p_Color2.y, p_Color2.z, p_Color2.w)),
        DirectX::VertexPositionColor(DirectX::SimpleMath::Vector3(p_V3.x, p_V3.y, p_V3.z), DirectX::SimpleMath::Vector4(p_Color3.x, p_Color3.y, p_Color3.z, p_Color3.w)));
}

void Rendering::Renderers::DirectXTKRenderer::DrawTriangle3D(const SVector3& p_V1, const SVector4& p_Color1, const SVector2& p_TextureCoordinates1, const SVector3& p_V2, const SVector4& p_Color2, const SVector2& p_TextureCoordinates2, const SVector3& p_V3, const SVector4& p_Color3, const SVector2& p_TextureCoordinates3) {
    m_TextBatch->DrawTriangle(
        DirectX::VertexPositionColorTexture(DirectX::SimpleMath::Vector3(p_V1.x, p_V1.y, p_V1.z), DirectX::SimpleMath::Vector4(p_Color1.x, p_Color1.y, p_Color1.z, p_Color1.w), DirectX::SimpleMath::Vector2(p_TextureCoordinates1.x, p_TextureCoordinates1.y)),
        DirectX::VertexPositionColorTexture(DirectX::SimpleMath::Vector3(p_V2.x, p_V2.y, p_V2.z), DirectX::SimpleMath::Vector4(p_Color2.x, p_Color2.y, p_Color2.z, p_Color2.w), DirectX::SimpleMath::Vector2(p_TextureCoordinates2.x, p_TextureCoordinates2.y)),
        DirectX::VertexPositionColorTexture(DirectX::SimpleMath::Vector3(p_V3.x, p_V3.y, p_V3.z), DirectX::SimpleMath::Vector4(p_Color3.x, p_Color3.y, p_Color3.z, p_Color3.w), DirectX::SimpleMath::Vector2(p_TextureCoordinates3.x, p_TextureCoordinates3.y)));
}

void DirectXTKRenderer::DrawQuad3D(
    const SVector3& p_V1,
    const SVector4& p_Color1,
    const SVector3& p_V2,
    const SVector4& p_Color2,
    const SVector3& p_V3,
    const SVector4& p_Color3,
    const SVector3& p_V4,
    const SVector4& p_Color4
) {
    m_PrimitiveBatch->DrawQuad(
        DirectX::VertexPositionColor(
            DirectX::SimpleMath::Vector3(p_V1.x, p_V1.y, p_V1.z),
            DirectX::SimpleMath::Vector4(p_Color1.x, p_Color1.y, p_Color1.z, p_Color1.w)
        ),
        DirectX::VertexPositionColor(
            DirectX::SimpleMath::Vector3(p_V2.x, p_V2.y, p_V2.z),
            DirectX::SimpleMath::Vector4(p_Color2.x, p_Color2.y, p_Color2.z, p_Color2.w)
        ),
        DirectX::VertexPositionColor(
            DirectX::SimpleMath::Vector3(p_V3.x, p_V3.y, p_V3.z),
            DirectX::SimpleMath::Vector4(p_Color3.x, p_Color3.y, p_Color3.z, p_Color3.w)
        ),
        DirectX::VertexPositionColor(
            DirectX::SimpleMath::Vector3(p_V4.x, p_V4.y, p_V4.z),
            DirectX::SimpleMath::Vector4(p_Color4.x, p_Color4.y, p_Color4.z, p_Color4.w)
        )
    );
}

void DirectXTKRenderer::DrawText3D(const std::string& text, const SMatrix& world, const SVector4& color, float scale, TextAlignment horizontalAlignment, TextAlignment verticalAlignment)
{
    DrawText3D(text.c_str(), world, color, scale, horizontalAlignment, verticalAlignment);
}

void DirectXTKRenderer::DrawText3D(const char* text, const SMatrix& world, const SVector4& color, float scale, TextAlignment horizontalAlignment, TextAlignment verticalAlignment)
{
    int textLength = -1;

    do
    {
        ++textLength;
    } while (text[textLength] != '\0');

    if (textLength > 255)
    {
        textLength = 255;
    }

    const char* baseText = text;
    int baseTextLength = textLength;
    int printableCharacterCount = 0;

    while (textLength)
    {
        if (*text == '\n')
        {
            break;
        }

        if (static_cast<unsigned char>(*text) >= 33 && static_cast<unsigned char>(*text) <= 126)
        {
            ++printableCharacterCount;
        }

        ++text;
        --textLength;
    }

    MDF_FONT::STextBoundingBox TextBoundingBox;

    MDF_FONT::CalcBoundingBox(TextBoundingBox, baseText);

    float offsetX = 0.f;
    float offsetY = 0.f;

    if (horizontalAlignment == TextAlignment::Center)
    {
        offsetX = (TextBoundingBox.m_fMaxX - TextBoundingBox.m_fMinX) * -0.5f;
    }
    else if (horizontalAlignment == TextAlignment::Right)
    {
        offsetX = (TextBoundingBox.m_fMaxX - TextBoundingBox.m_fMinX) * -1.f;
    }

    if (verticalAlignment == TextAlignment::Middle)
    {
        offsetY = (TextBoundingBox.m_fMaxY - TextBoundingBox.m_fMinY) * -0.5f;
    }
    else if (verticalAlignment == TextAlignment::Bottom)
    {
        offsetY = (TextBoundingBox.m_fMaxY - TextBoundingBox.m_fMinY) * -1.f;
    }

    const float4 translate = float4(offsetX * scale, 0.f, offsetY * scale, 1.f);
    const float4 scale2 = float4(scale, scale, scale, 1.f);
    const SMatrix offsetMatrix = SMatrix::ScaleTranslate(scale2, translate);
    const SMatrix worldMatrix = world.AffineMultiply(offsetMatrix);

    const unsigned int vertexCount = 2 * printableCharacterCount;
    std::vector<Triangle> triangles;

    triangles.reserve(vertexCount);

    float penX = 0.f;

    while (baseTextLength)
    {
        if (*baseText == '\n')
        {
            break;
        }

        if (static_cast<unsigned char>(*baseText) >= 33 && static_cast<unsigned char>(*baseText) <= 126)
        {
            static const float scale = 1.f;
            static const float penY = 0.f;
            float vertices[8];
            float textureCoordinates[8];

            MDF_FONT::RenderQuad(static_cast<unsigned int>(*baseText), scale, penX, penY, vertices, textureCoordinates);

            float4 bottomLeft = float4(vertices[0], 0.f, vertices[1], 1.f);
            float4 bottomRight = float4(vertices[2], 0.f, vertices[3], 1.f);
            float4 topRight = float4(vertices[4], 0.f, vertices[5], 1.f);
            float4 topLeft = float4(vertices[6], 0.f, vertices[7], 1.f);

            bottomLeft = worldMatrix.WVectorTransform(bottomLeft);
            bottomRight = worldMatrix.WVectorTransform(bottomRight);
            topRight = worldMatrix.WVectorTransform(topRight);
            topLeft = worldMatrix.WVectorTransform(topLeft);

            Triangle& triangle1 = triangles.emplace_back();
            Triangle& triangle2 = triangles.emplace_back();

            triangle1.vertexPosition1 = SVector3{ bottomLeft.x, bottomLeft.y, bottomLeft.z };
            triangle1.vertexPosition2 = SVector3{ bottomRight.x, bottomRight.y, bottomRight.z };
            triangle1.vertexPosition3 = SVector3{ topLeft.x, topLeft.y, topLeft.z };

            triangle2.vertexPosition1 = SVector3{ bottomRight.x, bottomRight.y, bottomRight.z };
            triangle2.vertexPosition2 = SVector3{ topRight.x, topRight.y, topRight.z };
            triangle2.vertexPosition3 = SVector3{ topLeft.x, topLeft.y, topLeft.z };

            triangle1.vertexColor1 = color;
            triangle1.vertexColor2 = color;
            triangle1.vertexColor3 = color;

            triangle2.vertexColor1 = color;
            triangle2.vertexColor2 = color;
            triangle2.vertexColor3 = color;

            triangle1.textureCoordinates1 = SVector2{ textureCoordinates[0], textureCoordinates[1] };
            triangle1.textureCoordinates2 = SVector2{ textureCoordinates[2], textureCoordinates[3] };
            triangle1.textureCoordinates3 = SVector2{ textureCoordinates[6], textureCoordinates[7] };

            triangle2.textureCoordinates1 = SVector2{ textureCoordinates[2], textureCoordinates[3] };
            triangle2.textureCoordinates2 = SVector2{ textureCoordinates[4], textureCoordinates[5] };
            triangle2.textureCoordinates3 = SVector2{ textureCoordinates[6], textureCoordinates[7] };
        }

        ++baseText;
        --baseTextLength;
    }

    for (size_t i = 0; i < triangles.size(); ++i)
    {
        DrawTriangle3D(triangles[i].vertexPosition1, triangles[i].vertexColor1, triangles[i].textureCoordinates1,
            triangles[i].vertexPosition2, triangles[i].vertexColor2, triangles[i].textureCoordinates2,
            triangles[i].vertexPosition3, triangles[i].vertexColor3, triangles[i].textureCoordinates3);
    }
}

void DirectXTKRenderer::DrawMesh(const std::vector<SVector3>& vertices, const std::vector<unsigned short>& indices, const SVector4& vertexColor) {
    std::vector<DirectX::VertexPositionColor> vertices2;

    vertices2.reserve(vertices.size());

    for (size_t i = 0; i < vertices.size(); ++i) {
        const SVector3& vertex = vertices[i];

        vertices2.push_back(DirectX::VertexPositionColor(DirectX::SimpleMath::Vector3(vertex.x, vertex.y, vertex.z),
            DirectX::SimpleMath::Vector4(vertexColor.x, vertexColor.y, vertexColor.z, vertexColor.w)));
    }

    m_PrimitiveBatch->DrawIndexed(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, indices.data(), indices.size(), vertices2.data(), vertices2.size());
}

const PrimitiveType Rendering::Renderers::DirectXTKRenderer::GetCurrentPrimitiveType() const {
    return m_CurrentPrimitiveType;
}
