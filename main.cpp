#include "stdafx.h"
#include "d3dApp.h"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

using namespace DirectX;
using Microsoft::WRL::ComPtr;

class D3DAppImpl : public D3DApp
{
public:
	D3DAppImpl(UINT width, UINT height, std::wstring name);

	virtual void OnInit() override;
	virtual void OnUpdate(const Timer& timer) override;
	virtual void OnRender(const Timer& timer) override;
	virtual void OnDestroy() override;

	virtual void OnKeyDown(UINT8 /*key*/) override;
	virtual void OnKeyUp(UINT8 /*key*/) override;

	virtual void OnMouseRawDelta(int dx, int dy) override;

private:
	static const UINT FrameCount = 2;
	static const UINT CubeCount = 10;
	static const UINT LightSourceCount = 1;

	struct Vertex
	{
		XMFLOAT3 position;
		XMFLOAT3 normal;
		XMFLOAT2 uv;
	};

	struct SceneConstantBuffer
	{
		XMFLOAT4X4 model;
		XMFLOAT4X4 view;
		XMFLOAT4X4 projection;
		XMFLOAT4X4 normalMatrix;
	};
	static_assert((sizeof(SceneConstantBuffer) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	struct LightDataConstantBuffer
	{
		XMFLOAT3 lightPosition; // 12
		float padding0;			// 4
		XMFLOAT4 lightColor;    // 16
		XMFLOAT3 cameraPos;     // 12
		float padding[53];      // 228 → total 256 bytes
	};
	static_assert((sizeof(LightDataConstantBuffer) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	struct LightSourceConstantBuffer
	{
		XMFLOAT4X4 model;
		XMFLOAT4X4 view;
		XMFLOAT4X4 projection;
		float padding[16];
	};
	static_assert((sizeof(LightSourceConstantBuffer) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	struct LightSourceDataConstantBuffer
	{
		XMFLOAT4 lightColor;
		float padding[60];
	};
	static_assert((sizeof(LightSourceDataConstantBuffer) % 256) == 0, "Constant Buffer size must be 256-byte aligned");

	// Pipeline objects.
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_heap;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12PipelineState> m_cubePipelineState;
	ComPtr<ID3D12PipelineState> m_lightSourcePipelineState;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	UINT m_rtvDescriptorSize;
	UINT m_dsvDescriptorSize;
	UINT m_heapDescriptorSize;

	// App resources.
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	ComPtr<ID3D12Resource> m_indexBuffer;
	D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
	ComPtr<ID3D12Resource> m_texture;          // diffuse
	ComPtr<ID3D12Resource> m_specularTexture;  // specular
	ComPtr<ID3D12Resource> m_cubeConstantBuffer;
	SceneConstantBuffer m_cubeConstantBufferData[CubeCount];
	ComPtr<ID3D12Resource> m_lightDataConstantBuffer; // for cube shader b1
	LightDataConstantBuffer m_lightDataConstantBufferData[CubeCount];
	ComPtr<ID3D12Resource> m_lightSourceConstantBuffer;
	LightSourceConstantBuffer m_lightSourceConstantBufferData[LightSourceCount];
	ComPtr<ID3D12Resource> m_lightSourceDataConstantBuffer;
	LightSourceDataConstantBuffer m_lightSourceDataConstantBufferData[LightSourceCount];
	ComPtr<ID3D12Resource> m_depthBuffer;
	UINT8* m_pCubeCbvDataBegin;
	UINT8* m_pLightDataCbvDataBegin;
	UINT8* m_pLightSourceCbvDataBegin;
	UINT8* m_pLightSourceDataCbvDataBegin;

	// Synchronization objects.
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValues[FrameCount];

	// Camera properties
	const double m_cameraSpeed;

	XMVECTOR m_cameraPos;
	XMVECTOR m_cameraFront;
	XMVECTOR m_cameraUp;

	// Keyboard input state
	bool m_moveForward = false;
	bool m_moveBackward = false;
	bool m_moveLeft = false;
	bool m_moveRight = false;

	// Mouse input state
	float m_yaw;
	float m_pitch;
	float m_mouseSensitivity;

	// Scene properties
	XMVECTOR m_lightColor;

	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void MoveToNextFrame();
	void WaitForGpu();
};

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);
	const bool coInitialized = SUCCEEDED(hr);

	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
	{
		return static_cast<int>(hr);
	}

	D3DAppImpl app(1280, 720, L"D3D12 Hello Window");
	const int exitCode = Win32Application::Run(&app, hInstance, nCmdShow);

	if (coInitialized)
	{
		CoUninitialize();
	}

	return exitCode;
}

D3DAppImpl::D3DAppImpl(UINT width, UINT height, std::wstring name) :
	D3DApp(width, height, name),
	m_frameIndex(0),
	m_pCubeCbvDataBegin(nullptr),
	m_pLightDataCbvDataBegin(nullptr),
	m_pLightSourceCbvDataBegin(nullptr),
	m_pLightSourceDataCbvDataBegin(nullptr),
	m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
	m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
	m_fenceValues{},
	m_rtvDescriptorSize(0),
	m_cubeConstantBufferData{},
	m_lightSourceConstantBufferData{},
	m_cameraSpeed(5.0f),
	m_cameraPos(XMVectorSet(2.2f, 1.6f, 7.0f, 1.0f)),
	m_cameraFront(XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f)),
	m_cameraUp(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)),
	m_yaw(-90.0f),
	m_pitch(0.0f),
	m_mouseSensitivity(0.1f),
	m_lightColor(XMVectorSet(1.0f, 0.0f, 0.0f, 1.0f))
{
}

void D3DAppImpl::OnInit()
{
	LoadPipeline();
	LoadAssets();
}

void D3DAppImpl::LoadPipeline()
{
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();

			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
	}
#endif

	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		ThrowIfFailed(D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		));
	}

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),
		Win32Application::GetHwnd(),
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain
	));

	// Don't need to support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// Need only one heap for both SRV and CBV
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 2 + (2 * CubeCount) + (2 * LightSourceCount);
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_heap)));

		m_heapDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

		D3D12_RESOURCE_DESC depthDesc = {};
		depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthDesc.Width = m_width;
		depthDesc.Height = m_height;
		depthDesc.DepthOrArraySize = 1;
		depthDesc.MipLevels = 1;
		depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
		depthDesc.SampleDesc.Count = 1;
		depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_CLEAR_VALUE depthClear = {};
		depthClear.Format = DXGI_FORMAT_D32_FLOAT;
		depthClear.DepthStencil.Depth = 1.0f;

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&depthDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthClear,
			IID_PPV_ARGS(&m_depthBuffer)
		));

		m_device->CreateDepthStencilView(m_depthBuffer.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

		m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	}

	// Create frame resources.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		for (UINT n = 0; n < FrameCount; n++)
		{
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);

			ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
		}
	}
}

void D3DAppImpl::LoadAssets()
{
	// Create an emoty root signature
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

		// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

		if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC); // per-object (b0)
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC); // light data (b1)

		CD3DX12_ROOT_PARAMETER1 rootParameters[3];
		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
		rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_VERTEX);
		rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_STATIC_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_ANISOTROPIC;
		sampler.MaxAnisotropy = 8;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.MipLODBias = 0;
		sampler.MaxAnisotropy = 0;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, rootSignatureFlags);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	// Create the cube pipeline state, which includes compiling and loading shaders.
	{
		UINT8* pVertexShaderData = nullptr;
		UINT8* pPixelShaderData = nullptr;
		UINT vertexShaderDataLength = 0;
		UINT pixelShaderDataLength = 0;

		ThrowIfFailed(ReadDataFromFile(GetAssetFullPath(L"cubeShaders_VSMain.cso").c_str(), &pVertexShaderData, &vertexShaderDataLength));
		ThrowIfFailed(ReadDataFromFile(GetAssetFullPath(L"cubeShaders_PSMain.cso").c_str(), &pPixelShaderData, &pixelShaderDataLength));

		// Define the vertex input layout (position, normal, uv)
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShaderData, vertexShaderDataLength);
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShaderData, pixelShaderDataLength);
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

		// Enable depth testing
		psoDesc.DepthStencilState.DepthEnable = TRUE;  // ← Change this
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;

		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;  // ← Add this

		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_cubePipelineState)));

		delete[] pVertexShaderData;
		delete[] pPixelShaderData;
	}

	// Create the light source pipeline state, which includes compiling and loading shaders.
	{
		UINT8* pVertexShaderData = nullptr;
		UINT8* pPixelShaderData = nullptr;
		UINT vertexShaderDataLength = 0;
		UINT pixelShaderDataLength = 0;

		ThrowIfFailed(ReadDataFromFile(GetAssetFullPath(L"lightSourceShaders_VSMain.cso").c_str(), &pVertexShaderData, &vertexShaderDataLength));
		ThrowIfFailed(ReadDataFromFile(GetAssetFullPath(L"lightSourceShaders_PSMain.cso").c_str(), &pPixelShaderData, &pixelShaderDataLength));

		// Define the vertex input layout for light source cubes (position only)
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(pVertexShaderData, vertexShaderDataLength);
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(pPixelShaderData, pixelShaderDataLength);
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

		// Enable depth testing
		psoDesc.DepthStencilState.DepthEnable = TRUE;  // ← Change this
		psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		psoDesc.DepthStencilState.StencilEnable = FALSE;

		psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;  // ← Add this

		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_lightSourcePipelineState)));
		delete[] pVertexShaderData;
		delete[] pPixelShaderData;
	}

	// Create the command list.
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), m_cubePipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

	// Declare staging buffers at function scope — must outlive command list execution
	ComPtr<ID3D12Resource> vertexUploadHeap;
	ComPtr<ID3D12Resource> indexUploadHeap;
	ComPtr<ID3D12Resource> textureUploadHeapDiffuse;
	ComPtr<ID3D12Resource> textureUploadHeapSpecular;

	// Create the vertex buffer.
	{
		// Create the vertex and index buffers.
		{
			Vertex vertices[] = {
				// Front face (z = -1), normal (0, 0, -1)
				  { {-1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f} },
				  { {-1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f} },
				  { { 1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f} },
				  { { 1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f} },

				  // Back face (z = 1), normal (0, 0, 1)
				  { { 1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f} },
				  { { 1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f} },
				  { {-1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f} },
				  { {-1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f} },

				  // Left face (x = -1), normal (-1, 0, 0)
				  { {-1.0f, -1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f} },
				  { {-1.0f,  1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} },
				  { {-1.0f,  1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f} },
				  { {-1.0f, -1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} },

				  // Right face (x = 1), normal (1, 0, 0)
				  { { 1.0f, -1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f} },
				  { { 1.0f,  1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f} },
				  { { 1.0f,  1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f} },
				  { { 1.0f, -1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f} },

				  // Top face (y = 1), normal (0, 1, 0)
				  { {-1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 1.0f} },
				  { {-1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f} },
				  { { 1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f} },
				  { { 1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f} },

				  // Bottom face (y = -1), normal (0, -1, 0)
				  { {-1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 1.0f} },
				  { {-1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f} },
				  { { 1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f} },
				  { { 1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f} },
			};

			UINT16 indices[] =
			{
				// Front
				 0,  1,  2,   0,  2,  3,
				 // Back
				  4,  5,  6,   4,  6,  7,
				  // Left
				   8,  9, 10,   8, 10, 11,
				   // Right
					12, 13, 14,  12, 14, 15,
					// Top
					 16, 17, 18,  16, 18, 19,
					 // Bottom
					  20, 21, 22,  20, 22, 23,
			};

			const UINT vertexBufferSize = sizeof(vertices);
			const UINT indexBufferSize = sizeof(indices);

			// 1. Create GPU-resident (DEFAULT) buffers as copy destinations
			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&m_vertexBuffer)
			));

			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&m_indexBuffer)
			));

			// 2. Create CPU-visible (UPLOAD) staging buffers
			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&vertexUploadHeap)
			));

			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&indexUploadHeap)
			));

			// 3. Copy CPU data → upload heaps
			CD3DX12_RANGE readRange(0, 0);

			UINT8* pMapped = nullptr;
			ThrowIfFailed(vertexUploadHeap->Map(0, &readRange, reinterpret_cast<void**>(&pMapped)));
			memcpy(pMapped, vertices, vertexBufferSize);
			vertexUploadHeap->Unmap(0, nullptr);

			ThrowIfFailed(indexUploadHeap->Map(0, &readRange, reinterpret_cast<void**>(&pMapped)));
			memcpy(pMapped, indices, indexBufferSize);
			indexUploadHeap->Unmap(0, nullptr);

			// 4. Record GPU copy commands: upload heap → default heap
			m_commandList->CopyBufferRegion(m_vertexBuffer.Get(), 0, vertexUploadHeap.Get(), 0, vertexBufferSize);
			m_commandList->CopyBufferRegion(m_indexBuffer.Get(), 0, indexUploadHeap.Get(), 0, indexBufferSize);

			// 5. Transition to read states
			const D3D12_RESOURCE_BARRIER barriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(
					m_vertexBuffer.Get(),
					D3D12_RESOURCE_STATE_COPY_DEST,
					D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
				CD3DX12_RESOURCE_BARRIER::Transition(
					m_indexBuffer.Get(),
					D3D12_RESOURCE_STATE_COPY_DEST,
					D3D12_RESOURCE_STATE_INDEX_BUFFER)
			};
			m_commandList->ResourceBarrier(_countof(barriers), barriers);

			// 6. Initialize buffer views (same as before)
			m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
			m_vertexBufferView.StrideInBytes = sizeof(Vertex);
			m_vertexBufferView.SizeInBytes = vertexBufferSize;

			m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
			m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
			m_indexBufferView.SizeInBytes = indexBufferSize;
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_heap->GetCPUDescriptorHandleForHeapStart());

		// Create the textures and SRVs
		{
			auto LoadTextureAndCreateSrv = [&](const std::wstring& path,
				ComPtr<ID3D12Resource>& texture,
				ComPtr<ID3D12Resource>& uploadHeap)
				{
					TexMetadata metadata = {};
					ScratchImage scratchImage;

					ThrowIfFailed(LoadFromWICFile(
						path.c_str(),
						WIC_FLAGS_FORCE_RGB,
						&metadata,
						scratchImage
					));

					D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
						metadata.format,
						static_cast<UINT64>(metadata.width),
						static_cast<UINT>(metadata.height),
						static_cast<UINT16>(metadata.arraySize),
						static_cast<UINT16>(metadata.mipLevels)
					);

					ThrowIfFailed(m_device->CreateCommittedResource(
						&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
						D3D12_HEAP_FLAG_NONE,
						&textureDesc,
						D3D12_RESOURCE_STATE_COPY_DEST,
						nullptr,
						IID_PPV_ARGS(&texture)
					));

					const UINT subresourceCount = static_cast<UINT>(scratchImage.GetImageCount());
					const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, subresourceCount);

					ThrowIfFailed(m_device->CreateCommittedResource(
						&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
						D3D12_HEAP_FLAG_NONE,
						&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
						D3D12_RESOURCE_STATE_GENERIC_READ,
						nullptr,
						IID_PPV_ARGS(&uploadHeap)
					));

					std::vector<D3D12_SUBRESOURCE_DATA> subresources;
					subresources.reserve(subresourceCount);
					const Image* images = scratchImage.GetImages();
					for (UINT i = 0; i < subresourceCount; ++i)
					{
						D3D12_SUBRESOURCE_DATA subresource = {};
						subresource.pData = images[i].pixels;
						subresource.RowPitch = static_cast<LONG_PTR>(images[i].rowPitch);
						subresource.SlicePitch = static_cast<LONG_PTR>(images[i].slicePitch);
						subresources.push_back(subresource);
					}

					UpdateSubresources(m_commandList.Get(), texture.Get(), uploadHeap.Get(), 0, 0, subresourceCount, subresources.data());

					// No barrier here — batched below after both textures are uploaded

					D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
					srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					srvDesc.Format = metadata.format;
					srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
					srvDesc.Texture2D.MostDetailedMip = 0;
					srvDesc.Texture2D.MipLevels = static_cast<UINT>(metadata.mipLevels);
					srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

					// Use captured handle directly — srvHandleOut was just a redundant copy of it
					m_device->CreateShaderResourceView(texture.Get(), &srvDesc, handle);
					handle.Offset(1, m_heapDescriptorSize);
				};

			const std::wstring diffusePath = GetAssetFullPath(L"Textures\\container.png");
			const std::wstring specularPath = GetAssetFullPath(L"Textures\\container_specular.png");

			LoadTextureAndCreateSrv(diffusePath, m_texture, textureUploadHeapDiffuse);
			LoadTextureAndCreateSrv(specularPath, m_specularTexture, textureUploadHeapSpecular);

			// Batch both COPY_DEST → PIXEL_SHADER_RESOURCE transitions in one call
			// (same principle as batching vertex + index buffer barriers)
			const D3D12_RESOURCE_BARRIER textureBarriers[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(
					m_texture.Get(),
					D3D12_RESOURCE_STATE_COPY_DEST,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(
					m_specularTexture.Get(),
					D3D12_RESOURCE_STATE_COPY_DEST,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
			};
			m_commandList->ResourceBarrier(_countof(textureBarriers), textureBarriers);
		}

		// Create the constant buffer for all cubes (b0)
		{
			const UINT constantBufferSize = sizeof(SceneConstantBuffer);
			const UINT totalConstantBufferSize = constantBufferSize * CubeCount;

			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(totalConstantBufferSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&m_cubeConstantBuffer)
			));

			// Map and initialize the constant buffer. We don't unmap this until the
			// app closes. Keeping things mapped for the lifetime of the resource is okay.
			CD3DX12_RANGE readRange(0, 0);
			ThrowIfFailed(m_cubeConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pCubeCbvDataBegin)));

			for (UINT i = 0; i < CubeCount; ++i)
			{
				// Describe and create a constant buffer view.
				D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
				cbvDesc.BufferLocation = m_cubeConstantBuffer->GetGPUVirtualAddress() + (static_cast<UINT64>(i) * constantBufferSize);
				cbvDesc.SizeInBytes = constantBufferSize;

				m_device->CreateConstantBufferView(&cbvDesc, handle);
				handle.Offset(1, m_heapDescriptorSize);
			}

			memcpy(m_pCubeCbvDataBegin, &m_cubeConstantBufferData, sizeof(m_cubeConstantBufferData));
		}

		// Create the constant buffer for light data used by cube shaders (b1)
		{
			const UINT constantBufferSize = sizeof(LightDataConstantBuffer);
			const UINT totalConstantBufferSize = constantBufferSize * CubeCount; // ← was just constantBufferSize

			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(totalConstantBufferSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&m_lightDataConstantBuffer)
			));

			CD3DX12_RANGE readRange(0, 0);
			ThrowIfFailed(m_lightDataConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pLightDataCbvDataBegin)));

			for (UINT i = 0; i < CubeCount; ++i) // ← was a single CBV
			{
				D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
				cbvDesc.BufferLocation = m_lightDataConstantBuffer->GetGPUVirtualAddress() + (static_cast<UINT64>(i) * constantBufferSize);
				cbvDesc.SizeInBytes = constantBufferSize;
				m_device->CreateConstantBufferView(&cbvDesc, handle);
				handle.Offset(1, m_heapDescriptorSize);
			}

			memcpy(m_pLightDataCbvDataBegin, m_lightDataConstantBufferData, sizeof(m_lightDataConstantBufferData));
		}

		// Create the constant buffer for light sources (b0 in light shaders)
		{
			const UINT constantBufferSize = sizeof(LightSourceConstantBuffer);
			const UINT totalConstantBufferSize = constantBufferSize * LightSourceCount;

			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(totalConstantBufferSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&m_lightSourceConstantBuffer)
			));

			// Map and initialize the constant buffer. We don't unmap this until the
			// app closes. Keeping things mapped for the lifetime of the resource is okay.
			CD3DX12_RANGE readRange(0, 0);
			ThrowIfFailed(m_lightSourceConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pLightSourceCbvDataBegin)));

			for (UINT i = 0; i < LightSourceCount; ++i)
			{
				// Describe and create a constant buffer view.
				D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
				cbvDesc.BufferLocation = m_lightSourceConstantBuffer->GetGPUVirtualAddress() + (static_cast<UINT64>(i) * constantBufferSize);
				cbvDesc.SizeInBytes = constantBufferSize;

				m_device->CreateConstantBufferView(&cbvDesc, handle);
				handle.Offset(1, m_heapDescriptorSize);
			}

			memcpy(m_pLightSourceCbvDataBegin, &m_lightSourceConstantBufferData, sizeof(m_lightSourceConstantBufferData));
		}

		// Create the constant buffer for light sources data (b1 in light shaders)
		{
			const UINT constantBufferSize = sizeof(LightSourceDataConstantBuffer);
			const UINT totalConstantBufferSize = constantBufferSize * LightSourceCount;

			ThrowIfFailed(m_device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(totalConstantBufferSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&m_lightSourceDataConstantBuffer)
			));

			// Map and initialize the constant buffer. We don't unmap this until the
			// app closes. Keeping things mapped for the lifetime of the resource is okay.
			CD3DX12_RANGE readRange(0, 0);
			ThrowIfFailed(m_lightSourceDataConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pLightSourceDataCbvDataBegin)));

			for (UINT i = 0; i < LightSourceCount; ++i)
			{
				// Describe and create a constant buffer view.
				D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
				cbvDesc.BufferLocation = m_lightSourceDataConstantBuffer->GetGPUVirtualAddress() + (static_cast<UINT64>(i) * constantBufferSize);
				cbvDesc.SizeInBytes = constantBufferSize;

				m_device->CreateConstantBufferView(&cbvDesc, handle);
				handle.Offset(1, m_heapDescriptorSize);
			}

			memcpy(m_pLightSourceDataCbvDataBegin, &m_lightSourceDataConstantBufferData, sizeof(m_lightSourceDataConstantBufferData));
		}

		// Close the command list and execute it to begin the initial GPU setup.
		ThrowIfFailed(m_commandList->Close());
		ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

		// Create synchronization objects and wait until assets have been uploaded to the GPU.
		{
			ThrowIfFailed(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
			m_fenceValues[m_frameIndex]++;

			// Create an event handle to use for frame synchronization
			m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (m_fenceEvent == nullptr)
			{
				ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
			}

			// Wait for the command list to execute; we are reusing the same command 
			// list in our main loop but for now, we just want to wait for setup to 
			// complete before continuing.
			WaitForGpu();
		}
	}
}

void D3DAppImpl::OnUpdate(const Timer& timer)
{
	static const XMFLOAT3 cubePositions[CubeCount] =
	{
		{ 0.0f,  0.0f,  0.0f },
		{ 4.0f,  10.0f, -30.0f },
		{ -3.0f, -4.4f, -5.0f },
		{ -10.8f, -4.0f, -24.6f },
		{ 4.8f, -0.16f, -7.0f },
		{ -3.4f,  6.0f, -15.0f },
		{ 2.6f, -4.0f, -5.0f },
		{ 3.5f,  4.0f, -5.5f },
		{ 4.5f,  0.5f, -3.5f },
		{ -2.6f,  2.0f, -3.0f }
	};

	static const XMFLOAT3 lightSourcePositions[LightSourceCount] =
	{
		{ 1.2f, 1.0f, -6.0f },
	};

	const float deltaTime = timer.DeltaTime();
	if (deltaTime > 0.0f)
	{
		const XMVECTOR right = XMVector3Normalize(XMVector3Cross(m_cameraFront, m_cameraUp));

		XMVECTOR moveDirection = XMVectorZero();

		if (m_moveForward)
		{
			moveDirection += m_cameraFront;
		}
		if (m_moveBackward)
		{
			moveDirection -= m_cameraFront;
		}
		if (m_moveLeft)
		{
			moveDirection += right;
		}
		if (m_moveRight)
		{
			moveDirection -= right;
		}

		if (XMVectorGetX(XMVector3LengthSq(moveDirection)) > 0.0f)
		{
			moveDirection = XMVector3Normalize(moveDirection);
			m_cameraPos += moveDirection * static_cast<float>(m_cameraSpeed) * deltaTime;
		}
	}

	XMMATRIX view = XMMatrixLookAtLH(
		m_cameraPos,
		m_cameraPos + m_cameraFront,
		m_cameraUp
	);

	XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, m_aspectRatio, 0.1f, 100.0f);

	for (UINT i = 0; i < CubeCount; ++i)
	{
		XMMATRIX mRotate = XMMatrixRotationAxis(XMVectorSet(1.0f, 0.3f, 0.5f, 1.0f), XMConvertToRadians(20.0f * i));

		XMMATRIX mTranslate = XMMatrixTranslation(
			cubePositions[i].x,
			cubePositions[i].y,
			cubePositions[i].z
		);

		XMMATRIX world = mRotate * mTranslate;

		XMStoreFloat4x4(&m_cubeConstantBufferData[i].projection, XMMatrixTranspose(proj));
		XMStoreFloat4x4(&m_cubeConstantBufferData[i].view, XMMatrixTranspose(view));
		XMStoreFloat4x4(&m_cubeConstantBufferData[i].model, XMMatrixTranspose(world));

		// Store the normal matrix (ensure your struct has a float4x4 normalMatrix field)
		XMMATRIX worldInverse = XMMatrixInverse(nullptr, world);
		XMStoreFloat4x4(&m_cubeConstantBufferData[i].normalMatrix, worldInverse);

		XMVECTOR lightPosWorld = XMLoadFloat3(&lightSourcePositions[0]);
		XMStoreFloat3(&m_lightDataConstantBufferData[i].lightPosition, lightPosWorld);
		XMStoreFloat4(&m_lightDataConstantBufferData[i].lightColor, m_lightColor);
		XMStoreFloat3(&m_lightDataConstantBufferData[i].cameraPos, m_cameraPos);
	}

	for (UINT i = 0; i < LightSourceCount; ++i)
	{
		XMMATRIX mTranslate = XMMatrixTranslation(
			lightSourcePositions[i].x,
			lightSourcePositions[i].y,
			lightSourcePositions[i].z
		);

		XMMATRIX mScale = XMMatrixScaling(0.3f, 0.3f, 0.3f);

		XMMATRIX world = mScale * mTranslate;

		XMStoreFloat4x4(&m_lightSourceConstantBufferData[i].projection, XMMatrixTranspose(proj));
		XMStoreFloat4x4(&m_lightSourceConstantBufferData[i].view, XMMatrixTranspose(view));
		XMStoreFloat4x4(&m_lightSourceConstantBufferData[i].model, XMMatrixTranspose(world));
		XMStoreFloat4(&m_lightSourceDataConstantBufferData[i].lightColor, m_lightColor);
	}

	memcpy(m_pCubeCbvDataBegin, m_cubeConstantBufferData, sizeof(m_cubeConstantBufferData));
	memcpy(m_pLightDataCbvDataBegin, &m_lightDataConstantBufferData, sizeof(m_lightDataConstantBufferData));
	memcpy(m_pLightSourceCbvDataBegin, m_lightSourceConstantBufferData, sizeof(m_lightSourceConstantBufferData));
	memcpy(m_pLightSourceDataCbvDataBegin, m_lightSourceDataConstantBufferData, sizeof(m_lightSourceDataConstantBufferData));
}

void D3DAppImpl::OnRender(const Timer& timer)
{
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Present the frame
	ThrowIfFailed(m_swapChain->Present(1, 0));

	MoveToNextFrame();
}

void D3DAppImpl::OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be cleaned up by the desctuctor
	WaitForGpu();
	CloseHandle(m_fenceEvent);
}

void D3DAppImpl::OnKeyDown(UINT8 key)
{
	switch (key)
	{
	case VK_ESCAPE:
		DestroyWindow(Win32Application::GetHwnd());
		break;
	case 'W':
		m_moveForward = true;
		break;
	case 'S':
		m_moveBackward = true;
		break;
	case 'A':
		m_moveLeft = true;
		break;
	case 'D':
		m_moveRight = true;
		break;
	default:
		break;
	}
}

void D3DAppImpl::OnKeyUp(UINT8 key)
{
	switch (key)
	{
	case 'W':
		m_moveForward = false;
		break;
	case 'S':
		m_moveBackward = false;
		break;
	case 'A':
		m_moveLeft = false;
		break;
	case 'D':
		m_moveRight = false;
		break;
	default:
		break;
	}
}

void D3DAppImpl::OnMouseRawDelta(int dx, int dy)
{
	m_yaw -= static_cast<float>(dx) * m_mouseSensitivity;
	m_pitch -= static_cast<float>(dy) * m_mouseSensitivity;

	if (m_pitch > 89.0f)
	{
		m_pitch = 89.0f;
	}
	else if (m_pitch < -89.0f)
	{
		m_pitch = -89.0f;
	}

	const float yawRadians = XMConvertToRadians(m_yaw);
	const float pitchRadians = XMConvertToRadians(m_pitch);

	const float xDir = cosf(pitchRadians) * cosf(yawRadians);
	const float yDir = sinf(pitchRadians);
	const float zDir = cosf(pitchRadians) * sinf(yawRadians);

	m_cameraFront = XMVector3Normalize(XMVectorSet(xDir, yDir, zDir, 0.0f));
}

void D3DAppImpl::PopulateCommandList()
{
	// Command list allocators can only be reset when the associated
	// command lists have finished execution on the GPU; apps should use
	// fences to determine GPU execution progress.
	ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset());

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_cubePipelineState.Get()));

	// Set necessary state
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { m_heap.Get() };
	m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	auto gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();

	CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(gpuStart, 0, m_heapDescriptorSize);
	m_commandList->SetGraphicsRootDescriptorTable(0, srvHandle);

	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// Indicate that the back buffer will be used as a render target.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

	// Set render targets with DSV (do this once!)
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	// Clear render target view
	const float clearColor[] = { 0.05f, 0.05f, 0.05f, 1.0f };
	m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	// Clear depth stencil view
	m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	m_commandList->IASetIndexBuffer(&m_indexBufferView);

	// Draw normal cubes
	m_commandList->SetPipelineState(m_cubePipelineState.Get());
	for (UINT i = 0; i < CubeCount; ++i)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle(gpuStart, 2 + i, m_heapDescriptorSize);
		m_commandList->SetGraphicsRootDescriptorTable(1, cbvHandle);

		// Light data CBVs start at index 2 + CubeCount
		CD3DX12_GPU_DESCRIPTOR_HANDLE lightDataHandle(gpuStart, 2 + CubeCount + i, m_heapDescriptorSize);
		m_commandList->SetGraphicsRootDescriptorTable(2, lightDataHandle);

		m_commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
	}

	// Draw light cubes – standalone, using lightSourceShaders
	m_commandList->SetPipelineState(m_lightSourcePipelineState.Get());

	// Light CBVs start at index 2 + CubeCount
	for (UINT i = 0; i < LightSourceCount; ++i)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle(gpuStart, 2 + (2 * CubeCount) + i, m_heapDescriptorSize);
		m_commandList->SetGraphicsRootDescriptorTable(1, cbvHandle);

		// Light data CBVs start at index 2 + CubeCount + LightSourceCount
		CD3DX12_GPU_DESCRIPTOR_HANDLE lightDataHandle(gpuStart, 2 + (2 * CubeCount) + LightSourceCount + i, m_heapDescriptorSize);
		m_commandList->SetGraphicsRootDescriptorTable(2, lightDataHandle);

		m_commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
	}

	// Indicate that the back buffer will now be used to present.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ThrowIfFailed(m_commandList->Close());
}

// Wait for pending GPU work to complete.
void D3DAppImpl::WaitForGpu()
{
	// Schedule a Signal command in the queue
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]));

	// Wait until the fence has been processed.
	ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
	WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

	// Increment the fence value for the current frame.
	m_fenceValues[m_frameIndex]++;
}

// Prepare to render the next frame.
void D3DAppImpl::MoveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

	// Update the frame index.
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
		WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
	}

	// Set the fence value for the next frame.
	m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}