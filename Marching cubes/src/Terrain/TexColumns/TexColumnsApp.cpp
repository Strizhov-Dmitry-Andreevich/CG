//***************************************************************************************ObjectCB
// TexColumnsApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"

#include "../../Common/imgui.h"
#include "../../Common/imgui_impl_dx12.h"
#include "../../Common/imgui_impl_win32.h"

#include "../../Common/Camera.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <filesystem>
#include <iostream>

#include "FrameResource.h"
#include "Terrain.h"
#include "TAATexture.h"

#include "MarchingCubes.h"
#include <DirectXTex.h>     // CPU-side DDS loading


using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "DirectXTex.lib")

const int gNumFrameResources = 6;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.

struct LodLevel
{
	MeshGeometry* Geo = nullptr; // Указатель на геометрию для этого LOD-уровня
	// Submesh данные, если Geo общее, а LODы - это разные Submesh'ы внутри него
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
	float SwitchDistance = 0.0f; // Расстояние, после которого используется этот LOD (или следующий, более низкий)
	DirectX::BoundingBox Bounds; // Локальный BoundingBox для этого LOD-уровня
	Material* LodMaterial = nullptr;
};
float mSwitchDist = 10;

struct RenderItem
{
	bool isHaveLods = true;
	std::vector<LodLevel> LodLevels; // Массив уровней детализации
	int CurrentLodIndex = 0;         // Индекс текущего активного LOD
	std::unordered_map<int, DirectX::XMFLOAT4X4> LodSavedTransforms;

	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 PrevWorld = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 BaseWorld = MathHelper::Identity4x4(); // исходная матрица
	DirectX::BoundingBox Bounds;
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();


	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;
	//MeshGeometry* mDebugGeo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
	std::string Name;
};

class TexColumnsApp : public D3DApp
{
public:
	TexColumnsApp(HINSTANCE hInstance);
	TexColumnsApp(const TexColumnsApp& rhs) = delete;
	TexColumnsApp& operator=(const TexColumnsApp& rhs) = delete;
	~TexColumnsApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateBrushCB(const GameTimer& gt);
	void UpdateTAA(const GameTimer& gt);
	void UpdateAtmosphereCB(const GameTimer& gt);

	void LoadDDSTexture(std::string name, std::wstring filename);
	void LoadDDSTexturesFromFolder(const std::wstring& folderPath);
	void LoadTextures();
	void CreateBrushTexture(CD3DX12_CPU_DESCRIPTOR_HANDLE baseDescriptorHandle, int baseOffset);
	void BuildRootSignature();
	void BuildStandMeshRootSignature();

	void BuildCsRootSignature();
	void BuildTerrainRootSignature();

	void BuildSkyRootSignature();

	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();

	void GenerateTileGeometry(const XMFLOAT3& worldPos, float tileSize, int lodLevel, std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices);
	void BuildTerrainGeometry();
	void UpdateTerrain(const GameTimer& gt);
	void InitTerrain();
	void UpdateTerrainCBs(const GameTimer& gt);

	void BuildPSOs();
	void BuildFrameResources();
	void CreateMaterial(std::string _name, int _CBIndex, int _SRVDiffIndex, int _SRVNMapIndex, int _SRVDispIndex, XMFLOAT4 _DiffuseAlbedo, XMFLOAT3 _FresnelR0, float _Roughness);
	void BuildMaterials();

	void RenderCustomMesh(std::string unique_name, std::string meshname, std::string materialName, XMMATRIX Scale, XMMATRIX Rotation, XMMATRIX Translation);
	void BuildCustomMeshGeometry(std::string name, UINT& meshVertexOffset, UINT& meshIndexOffset, UINT& prevVertSize, UINT& prevIndSize, std::vector<Vertex>& vertices, std::vector<std::uint16_t>& indices, MeshGeometry* Geo);
	void BuildAllCustomMeshes(UINT& meshVertexOffset, UINT& meshIndexOffset, UINT& prevVertSize, UINT& prevIndSize, std::vector<Vertex>& vertices, std::vector<std::uint16_t>& indices, MeshGeometry* Geo);

	void BuildMarchingCubesMesh();

	void BuildRenderItems();
	void CreateMCRenderItem();
	void UpdateMCRenderItem();//RenderMarchingCubes();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void DrawCustomMeshes(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& customMeshes);
	void DrawTilesRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<Tile*>& tiles);

	void InitImGui();
	void SetupImGui();

	bool ScreenToWorld(int screenX, int screenY, XMFLOAT3& worldPos);

	void InitTAAResources();

	void CreateTAAColorBuffer();
	void CreateTAAPrevTexture();
	void CreateTAACurrentTexture();
	void CreateVelocityBuffer();

	void CreateTAADescriptors();
	void BuildTAARootSignature();

	void GenerateTransformedHaltonSequence(float viewSizeX, float viewSizeY, XMFLOAT2* outJitters);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:
	XMMATRIX mInvViewProj;

	std::unordered_map<std::string, unsigned int>ObjectsMeshCount;

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;
	//
	std::unordered_map<std::string, int>TexOffsets;
	//
	UINT mCbvSrvDescriptorSize = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mStandMeshRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mTerrainRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mTAARootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mSkyRootSignature = nullptr;


	XMFLOAT2 jitters[16];
	int frameIndex = 0;

	std::unique_ptr<TAATexture> mTAAPrevTexture;
	std::unique_ptr<TAATexture> mTAACurrentTexture;
	std::unique_ptr<TAATexture> mTAAColorBuffer;
	std::unique_ptr<TAATexture> mTAVelocityBuffer;
	bool useTaa = true;

	UINT mTaaColorBufferRTVIndex;  // для рендера
	UINT mTaaColorBufferSRVIndex;  // для чтения в TAA

	UINT mPrevTextureSRVIndex;     // для чтения в TAA
	UINT mPrevTextureRTVIndex;     // для записи новой истории

	UINT mCurrentTextureSRVIndex;  // для чтения в TAA
	UINT mCurrentTextureRTVIndex;  // для записи новой истории

	UINT mVelocityBufferSRVIndex;  // для чтения в TAA
	UINT mVelocityBufferRTVIndex;  // для рендера velocity

	// Compute Shader PSO
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mBrushComputeRootSignature;


	Microsoft::WRL::ComPtr<ID3D12Resource> mBrushTexture;
	Microsoft::WRL::ComPtr<ID3D12Resource> mBrushTextureUpload;
	D3D12_CPU_DESCRIPTOR_HANDLE mBrushTextureSRV;
	D3D12_CPU_DESCRIPTOR_HANDLE mBrushTextureUAV;

	int mBrushTextureSRVIndex = 0;
	int mBrushTextureUAVIndex = 0;
	UINT mBrushTextureWidth = 1024;
	UINT mBrushTextureHeight = 1024;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	ComPtr<ID3D12DescriptorHeap> mImGuiSrvDescriptorHeap;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mStandCustomMeshes;
	std::vector<RenderItem*> mOpaqueRitems;
	std::vector<RenderItem*> mMCRitems;

	PassConstants mMainPassCB;
	BrushConstants mBrushCB;
	TAAConstants mTAACB;
	AtmosphereConstants mAtmosCB;
	float sunMotionSpeed=0.0;
	float sunIntensityCoof = 2.;

	MarchingCubes::NoiseParams mNoiseParams;
	bool mRegenerateMarching = false;
	bool mUseHeightTexture = false;
	UINT mMCRenderItemIndex = -1;

	float mIsovalue = 0.5f;

	POINT mLastMousePos;

	Camera mCamera;

	bool isFillModeSolid = true;
	// Disable ImGui rendering while the app runs by default.
	// Set this to true at runtime to enable the UI again.
	bool mEnableImGui = false;

	bool showTilesBoundingBox=false;

	float mScale = 1.f;
	float mTessellationFactor = 1.f;

	std::unique_ptr<Terrain> mTerrain;
	int mMaxLOD = 5;
	XMFLOAT3 terrainPos = XMFLOAT3(0.f, -100, 0.f);
	XMFLOAT3 marchingPos = XMFLOAT3(-1024.f, -100, 0.f);
	XMFLOAT3 terrainOffset = XMFLOAT3(0.f, 0.f, 0.f);
	float mTerrainSize = 1024;
	std::vector<Tile*> mVisibleTiles;

	float mCameraVertSpeed = 500;
	float mCameraHorSpeed = 500;

	/*float mTheta = 1.5f * XM_PI;
	float mPhi = 0.2f * XM_PI;
	float mRadius = 15.0f;*/

	//XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	//XMFLOAT4X4 mView = MathHelper::Identity4x4();
	//XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	int controlMode = 0; //0 - camera, 1 - terrain brush 

	//XMFLOAT3 BrushWPos;
	XMFLOAT4 BrushColor = {1.f, 1.f, 1.f, 1.f};
	float BrushRadius=30;
	float BrushFalloffRadius=40;
	int mIsPainting = 0;
	//bool mShowDebugTexture = true;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		TexColumnsApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

TexColumnsApp::TexColumnsApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

TexColumnsApp::~TexColumnsApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool TexColumnsApp::Initialize()
{

	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mCamera.SetPosition(200.0f, 150.0f, 200.0f);
	mCamera.RotateY(4.0);

	LoadTextures();

	BuildRootSignature();

	BuildSkyRootSignature();

	BuildStandMeshRootSignature();
	BuildTerrainRootSignature();
	BuildCsRootSignature();

	BuildDescriptorHeaps();
	BuildMaterials();

	BuildShadersAndInputLayout();

	InitTAAResources();
	InitTerrain();

	BuildShapeGeometry();
	BuildTerrainGeometry();

	BuildMarchingCubesMesh();

	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	InitImGui();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();
	mTAACB.blendFactor = 0.01f;

	GenerateTransformedHaltonSequence(mClientWidth, mClientHeight, jitters);

	return true;
}
void TexColumnsApp::InitTAAResources()
{
	BuildTAARootSignature();

	// Создаем текстуры
	if (!mTAAColorBuffer) CreateTAAColorBuffer();
	if (!mTAAPrevTexture) CreateTAAPrevTexture();
	if (!mTAACurrentTexture) CreateTAACurrentTexture();
	if (!mTAVelocityBuffer) CreateVelocityBuffer();
	// Создаем дескрипторы
	CreateTAADescriptors();
}

void TexColumnsApp::InitTerrain()
{
	mTerrain = std::make_unique<Terrain>();
	mTerrain->Initialize(md3dDevice.Get(), mTerrainSize, mMaxLOD, terrainPos);
}

void TexColumnsApp::InitImGui()
{
	D3D12_DESCRIPTOR_HEAP_DESC imGuiHeapDesc = {};
	imGuiHeapDesc.NumDescriptors = 1;
	imGuiHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	imGuiHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	imGuiHeapDesc.NodeMask = 0; // Or the appropriate node mask if you have multiple GPUs
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&imGuiHeapDesc, IID_PPV_ARGS(&mImGuiSrvDescriptorHeap)));
	// INITIALIZE IMGUI ////////////////////
		IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = md3dDevice.Get();
	init_info.CommandQueue = mCommandQueue.Get();
	init_info.NumFramesInFlight = gNumFrameResources;
	init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // Or your render target format.
	init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
	init_info.SrvDescriptorHeap = mImGuiSrvDescriptorHeap.Get();
	init_info.LegacySingleSrvCpuDescriptor = mImGuiSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	init_info.LegacySingleSrvGpuDescriptor = mImGuiSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	ImGui_ImplWin32_Init(mhMainWnd);
	ImGui_ImplDX12_Init(&init_info);
	////////////////////////////////////////
}

void TexColumnsApp::OnResize()
{
	D3DApp::OnResize();
	if (mTextures.size() > 0) {
		BuildDescriptorHeaps();
		// Обновляем размеры TAA текстур
		if (mTAAColorBuffer) mTAAColorBuffer->Resize(mClientWidth, mClientHeight);
		else CreateTAAColorBuffer();


		if (mTAAPrevTexture) mTAAPrevTexture->Resize(mClientWidth, mClientHeight);
		else CreateTAAPrevTexture();

		if (mTAACurrentTexture) mTAACurrentTexture->Resize(mClientWidth, mClientHeight);
		else CreateTAACurrentTexture();

		if (mTAVelocityBuffer) mTAVelocityBuffer->Resize(mClientWidth, mClientHeight);
		else CreateVelocityBuffer();

		CreateTAADescriptors();
	}

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, mCamera.cameraFarZ);

}

void TexColumnsApp::Update(const GameTimer& gt)
{

	OnKeyboardInput(gt);

	// Cycle through the circular frame resource array.

	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}



	AnimateMaterials(gt);

	UpdateTerrain(gt);

	UpdateObjectCBs(gt);
	UpdateTerrainCBs(gt);

	UpdateMainPassCB(gt);

	UpdateAtmosphereCB(gt);

	UpdateBrushCB(gt);
	UpdateMaterialCBs(gt);

	UpdateTAA(gt);

	
	SetupImGui();
}
void TexColumnsApp::SetupImGui()
{
    // If ImGui is disabled, skip building and rendering UI for this frame.
    if (!mEnableImGui)
    {
        // Make sure we don't start a new ImGui frame when disabled.
        return;
    }

}

void TexColumnsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	if ((btnState & MK_LBUTTON) != 0)
	{
		//do left click
	}

	SetCapture(mhMainWnd);
}

void TexColumnsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void TexColumnsApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if (!ImGui::GetIO().WantCaptureMouse)
	{
		if ((btnState & MK_RBUTTON) != 0)
		{
			// Make each pixel correspond to a quarter of a degree.
			float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
			float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

			mCamera.Pitch(dy);
			mCamera.RotateY(dx);
		}
		//Control mode HERE
		switch (controlMode)
		{
		case 0:

			break;
		case 1:
			if ((btnState & MK_RBUTTON) == 0)
			{
				mIsPainting = (btnState & MK_LBUTTON) == 0 ? 0 : 1;
				XMFLOAT3 worldPos;
				if (ScreenToWorld(x, y, worldPos))
				{
					mBrushCB.BrushWPos = worldPos;
					// Отладочный вывод выбранной позиции
					std::string debugMsg = "Decal placed at: X=" +
						std::to_string(worldPos.x) +
						", Y=" + std::to_string(worldPos.y) +
						", Z=" + std::to_string(worldPos.z) +
						"\n";
					OutputDebugStringA(debugMsg.c_str());
				}
				else
				{
					OutputDebugStringA("Decal hidden: click missed the plane\n");
				}


			}
			break;
		default:
			break;
		}
		

		mLastMousePos.x = x;
		mLastMousePos.y = y;
	
	}
}

void TexColumnsApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

	if (GetAsyncKeyState('W') & 0x8000)
	{
		if (shiftPressed) mCamera.MoveUp(mCameraVertSpeed * dt);
		else mCamera.Walk(mCameraHorSpeed * dt);
	}

	if (GetAsyncKeyState('S') & 0x8000)
	{
		if (shiftPressed) mCamera.MoveUp(-mCameraVertSpeed * dt);
		else mCamera.Walk(-mCameraHorSpeed * dt);
	}

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-mCameraHorSpeed * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(mCameraHorSpeed * dt);

	mCamera.UpdateViewMatrix();
}


bool TexColumnsApp::ScreenToWorld(int screenX, int screenY, XMFLOAT3& worldPos)
{
	// 1. Нормализованные координаты экрана [-1, 1]
	float nx = (2.0f * static_cast<float>(screenX)) / mClientWidth - 1.0f;
	float ny = 1.0f - (2.0f * static_cast<float>(screenY)) / mClientHeight;

	// 2. Создаем луч в clip space
	XMVECTOR rayStart = XMVectorSet(nx, ny, 0.0f, 1.0f);
	XMVECTOR rayEnd = XMVectorSet(nx, ny, 1.0f, 1.0f);

	// 3. Получаем обратную матрицу ViewProj
	//XMMATRIX viewProj = XMMatrixMultiply(mCamera.GetView(), mCamera.GetProj());
	XMMATRIX invViewProj = mInvViewProj;//XMMatrixInverse(nullptr, viewProj);

	// 4. Преобразуем в мировые координаты
	rayStart = XMVector3TransformCoord(rayStart, invViewProj);
	rayEnd = XMVector3TransformCoord(rayEnd, invViewProj);

	XMFLOAT3 startPos, endPos;
	XMStoreFloat3(&startPos, rayStart);
	XMStoreFloat3(&endPos, rayEnd);

	// 5. Направление луча
	XMVECTOR rayDir = XMVectorSubtract(rayEnd, rayStart);
	rayDir = XMVector3Normalize(rayDir);
	
	// Проверяем пересечение луча с AABB террейна
	float distance;

	std::string debugMsg;;

	XMVECTOR planeNormal = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR planePoint = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

	XMVECTOR diff = XMVectorSubtract(planePoint, rayStart);
	float numerator = XMVectorGetX(XMVector3Dot(diff, planeNormal));
	float denominator = XMVectorGetX(XMVector3Dot(rayDir, planeNormal));

	debugMsg = "Plane intersection calc: numerator=" + std::to_string(numerator) +
		", denominator=" + std::to_string(denominator) + "\n";
	OutputDebugStringA(debugMsg.c_str());

	if (fabs(denominator) > 1e-6f)
	{
		float t = numerator / denominator;

		if (t >= 0.0f)
		{
			XMVECTOR intersection = XMVectorAdd(rayStart, XMVectorScale(rayDir, t));
			XMFLOAT3 intersectPos;
			XMStoreFloat3(&intersectPos, intersection);

			// Проверяем границы террейна в плоскости XZ
			if (intersectPos.x >= terrainPos.x && intersectPos.x <= terrainPos.x + mTerrainSize &&
				intersectPos.z >= terrainPos.z && intersectPos.z <= terrainPos.z + mTerrainSize)
			{
				worldPos = intersectPos;
				debugMsg = "HIT: Projected to terrain plane at Y=" + std::to_string(intersectPos.y) + "\n";
				OutputDebugStringA(debugMsg.c_str());
				return true;
			}
		}
	}

	OutputDebugStringA("MISS: No intersection with terrain bounds\n");
	return false;
}

void TexColumnsApp::AnimateMaterials(const GameTimer& gt)
{

}

void TexColumnsApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{

		
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			e->PrevWorld = e->World;

			if (e->Name == "Guard2")
			{
				// Загружаем текущую матрицу
				XMMATRIX currentWorld = XMLoadFloat4x4(&e->World);

				// Извлекаем компоненты (поворот/масштаб/позиция)
				XMVECTOR scale, rotation, translation;
				XMMatrixDecompose(&scale, &rotation, &translation, currentWorld);

				// Сохраняем начальную позицию при первом запуске
				static XMFLOAT3 initialPosition;
				static bool firstTime = true;

				if (firstTime)
				{
					XMStoreFloat3(&initialPosition, translation);
					firstTime = false;
				}

				// Синусоидальное движение относительно начальной позиции
				float verticalOffset = sin(gt.TotalTime() * 1.0f) * 20.0f;

				// Новая позиция:
				float newX = initialPosition.x;
				float newY = initialPosition.y + verticalOffset;
				float newZ = initialPosition.z;

				e->PrevWorld = e->World;

				// Создаем новую матрицу с сохранением масштаба и поворота
				XMMATRIX newWorld = XMMatrixAffineTransformation(
					scale,                          // масштаб
					XMVectorZero(),                 // центр вращения
					rotation,                        // поворот
					XMVectorSet(newX, newY, newZ, 1.0f) // новая позиция
				);
				XMStoreFloat4x4(&e->World, newWorld);
				e->NumFramesDirty = gNumFrameResources;
			}

			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX prevworld = XMLoadFloat4x4(&e->PrevWorld);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);


			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.PrevWorld, XMMatrixTranspose(prevworld));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));


			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void TexColumnsApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void TexColumnsApp::UpdateMainPassCB(const GameTimer& gt)
{

	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();
	//static XMMATRIX lastViewProj = XMMatrixIdentity();

	//XMStoreFloat4x4(&mMainPassCB.PrevViewProj , XMMatrixTranspose(lastViewProj));

	//TODO:
	XMMATRIX proj_jittered = proj;
	if (useTaa)
	{
		proj_jittered.r[2].m128_f32[0] += jitters[frameIndex].x;
		proj_jittered.r[2].m128_f32[1] += jitters[frameIndex].y;
	}

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    mInvViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMMATRIX viewProj_jittered = XMMatrixMultiply(view, proj_jittered);
	//lastViewProj = viewProj_jittered;

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));

	mMainPassCB.PrevViewProj =mMainPassCB.ViewProj;

	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(mInvViewProj));

	XMStoreFloat4x4(&mMainPassCB.JitteredViewProj, XMMatrixTranspose(viewProj_jittered));

	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = mCamera.cameraFarZ;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	//mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

	//mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	//mMainPassCB.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };

	//mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	//mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };

	//mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	//mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	mMainPassCB.AmbientLight = { 0.13f, 0.14f, 0.16f, 1.0f };

	mMainPassCB.Lights[0].Direction = { -mAtmosCB.SunDirection.x, -mAtmosCB.SunDirection.y, -mAtmosCB.SunDirection.z };
	mMainPassCB.Lights[0].Strength = {
		mAtmosCB.SunColor.x * mAtmosCB.SunIntensity,
		mAtmosCB.SunColor.y * mAtmosCB.SunIntensity,
		mAtmosCB.SunColor.z * mAtmosCB.SunIntensity
	};
	//mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	//mMainPassCB.Lights[0].Strength = { 1.f, 1.f, 1.f };

	mMainPassCB.Lights[1].Strength = { 0,0,0 };
	mMainPassCB.Lights[2].Strength = { 0,0,0 };

	mMainPassCB.gScale = mScale;
	mMainPassCB.gTessellationFactor = mTessellationFactor;

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void TexColumnsApp::UpdateAtmosphereCB(const GameTimer& gt)
{
	static float sunAngle = 0.0f;

	// Вращаем солнце 
	sunAngle += gt.DeltaTime() * sunMotionSpeed;

	// Вычисляем позицию солнца на сфере
	// Используем круговое движение в плоскости XZ с наклоном для эффекта заката
	float sunX = sin(sunAngle) * 0.8f;
	float sunY = cos(sunAngle) * 0.5f + 0.3f; 
	float sunZ = cos(sunAngle) * 0.8f;

	// Нормализуем направление
	XMVECTOR sunDir = XMVector3Normalize(XMVectorSet(sunX, sunY, sunZ, 0.0f));
	XMStoreFloat3(&mAtmosCB.SunDirection, sunDir);

	// Вычисляем высоту солнца (Y-компонента направления)
	float sunHeight = mAtmosCB.SunDirection.y;


	if (sunHeight > 0.0f)
	{
		mAtmosCB.SunIntensity = sunIntensityCoof * sunHeight;
	}
	else
	{

		mAtmosCB.SunIntensity = 0.0f;
	}
	auto currAtmosCB = mCurrFrameResource->AtmosphereCB.get();
	currAtmosCB->CopyData(0, mAtmosCB);
}

void TexColumnsApp::UpdateTAA(const GameTimer& gt)
{
	auto TaaCB = mCurrFrameResource->TAACB.get();
	TaaCB->CopyData(0, mTAACB);
}

void TexColumnsApp::UpdateBrushCB(const GameTimer& gt)
{
	mBrushCB.BrushRadius = BrushRadius;
	mBrushCB.BrushFalofRadius = BrushFalloffRadius;
	mBrushCB.BrushColors = BrushColor;
	mBrushCB.isBrushMode = controlMode == 1 ? 1 : 0;
	mBrushCB.isPainting = mIsPainting == 1 ? 1 : 0;

	auto currBrushCB = mCurrFrameResource->BrushCB.get();
	currBrushCB->CopyData(0, mBrushCB);
}

void TexColumnsApp::UpdateTerrainCBs(const GameTimer& gt)
{
	auto currTileCB = mCurrFrameResource->TerrainCB.get();
	for (auto& tile : mTerrain->GetAllTiles())
	{
	    TileConstants tileConstants;

		tileConstants.TilePosition = tile->worldPos;
		tileConstants.gTerrainOffset = terrainOffset;
		tileConstants.TileSize = tile->tileSize;
		tileConstants.mapSize = mTerrain->mWorldSize;
		tileConstants.hScale = mTerrain->mHeightScale;

		tileConstants.showBoundingBox = showTilesBoundingBox ? 1 : 0;

		currTileCB->CopyData(tile->tileIndex, tileConstants);

		tile->NumFramesDirty--;
	}
}

void TexColumnsApp::UpdateTerrain(const GameTimer& gt)
{
	if (!mTerrain)
		return;


	mTerrain->Update(mCamera.GetPosition3f(), mCamera.GetFrustum());
}

void TexColumnsApp::LoadDDSTexture(std::string name, std::wstring filename)
{
	auto tex = std::make_unique<Texture>();
	tex->Filename = filename;
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), tex->Filename.c_str(),
		tex->Resource, tex->UploadHeap));
	mTextures[name] = std::move(tex);
}

void TexColumnsApp::LoadDDSTexturesFromFolder(const std::wstring& folderPath)
{
	namespace fs = std::filesystem;

	try
	{
		// Проверяем существование папки
		if (!fs::exists(folderPath) || !fs::is_directory(folderPath))
		{
			std::wstring errorMsg = L"Folder does not exist: " + folderPath + L"\n";
			OutputDebugStringW(errorMsg.c_str());
			return;
		}

		// Получаем имя последней папки
		std::wstring lastFolderName = fs::path(folderPath).filename().wstring();
		if (lastFolderName.empty())
		{
			// Если путь заканчивается на слэш, берем предыдущую папку
			lastFolderName = fs::path(folderPath).parent_path().filename().wstring();
		}

		OutputDebugStringW((L"Loading DDS textures from: " + folderPath + L"\n").c_str());
		OutputDebugStringW((L"Using folder prefix: " + lastFolderName + L"\n").c_str());

		int loadedCount = 0;
		for (const auto& entry : fs::directory_iterator(folderPath))
		{
			if (entry.is_regular_file())
			{
				// Получаем расширение файла
				std::wstring extension = entry.path().extension().wstring();
				// Преобразуем к нижнему регистру для сравнения
				std::wstring extensionLower = extension;
				std::transform(extensionLower.begin(), extensionLower.end(),
					extensionLower.begin(), ::towlower);

				// Проверяем только .dds файлы
				if (extensionLower == L".dds")
				{
					std::wstring filePath = entry.path().wstring();
					std::wstring fileName = entry.path().filename().wstring();
					// Получаем имя без расширения
					std::wstring fileNameWithoutExt = entry.path().stem().wstring();
					// Создаем имя: "Папка/текстура"
					std::string textureName =
						std::string(lastFolderName.begin(), lastFolderName.end()) + "/" +
						std::string(fileNameWithoutExt.begin(), fileNameWithoutExt.end());

					// Пропускаем если уже загружена
					if (mTextures.find(textureName) != mTextures.end())
					{
						//OutputDebugStringW((L"Texture already loaded: " + textureName.c_str() + L"\n").c_str());
						continue;
					}

					try
					{
						// Загружаем текстуру
						LoadDDSTexture(textureName, filePath);
						// Сохраняем в TexOffsets (если используется)
						if (TexOffsets.find(textureName) == TexOffsets.end())
						{
							static int textureCounter = 0;
							TexOffsets[textureName] = textureCounter++;
						}
						loadedCount++;
						// Логируем
						char debugMsg[256];
						sprintf_s(debugMsg, "Loaded: %s\n", textureName.c_str());
						OutputDebugStringA(debugMsg);
					}
					catch (...)
					{
						std::wstring errorMsg = L"Failed to load: " + fileName + L"\n";
						OutputDebugStringW(errorMsg.c_str());
					}
				}
			}
		}
		char summary[256];
		sprintf_s(summary, "Loaded %d DDS textures\n", loadedCount);
		OutputDebugStringA(summary);
	}
	catch (const fs::filesystem_error& e)
	{
		char errorMsg[512];
		sprintf_s(errorMsg, "Filesystem error: %s\n", e.what());
		OutputDebugStringA(errorMsg);
	}
}

void TexColumnsApp::LoadTextures()
{
	char msg[256];
	sprintf_s(msg, "Loading textures...\n");
	OutputDebugStringA(msg);
	//LoadDDSTexture("disp_placeholder", L"../../Textures/terrain_disp.dds");

	LoadDDSTexture("stoneTex", L"../../Textures/stone.dds");
	LoadDDSTexture("stoneNorm", L"../../Textures/stone_nmap.dds");
	LoadDDSTexture("stonetDisp", L"../../Textures/stone_disp.dds");

	LoadDDSTexture("terrainDiff", L"../../Textures/terrain_diff.dds");
	LoadDDSTexture("terrainNorm", L"../../Textures/terrain_norm.dds");
	LoadDDSTexture("terrainDisp", L"../../Textures/terrain_disp.dds");

	LoadDDSTexture("rock_diff", L"../../Textures/rock_diff.dds");
	LoadDDSTexture("rock_norm", L"../../Textures/rock_norm.dds");

	LoadDDSTexturesFromFolder(L"../../Textures/Guard/");
	LoadDDSTexturesFromFolder(L"../../Textures/Maxwell/");

	LoadDDSTexture("default", L"../../Textures/default.dds");
	LoadDDSTexture("default_normal", L"../../Textures/default_normal.dds");
	LoadDDSTexture("default_height", L"../../Textures/default_height.dds");
}



void TexColumnsApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE diffuseRange;
	diffuseRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // ��������� �������� � �������� t0

	CD3DX12_DESCRIPTOR_RANGE normalRange;
	normalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);  // ���������� ����� � �������� t1

	CD3DX12_DESCRIPTOR_RANGE dispMap;
	dispMap.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);  // Dispmap � �������� t2

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[6];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &diffuseRange, D3D12_SHADER_VISIBILITY_ALL);      // t0
	slotRootParameter[1].InitAsDescriptorTable(1, &normalRange, D3D12_SHADER_VISIBILITY_ALL);       // t1  
	slotRootParameter[2].InitAsDescriptorTable(1, &dispMap, D3D12_SHADER_VISIBILITY_ALL);           // t2

	slotRootParameter[3].InitAsConstantBufferView(0); // b0 
	slotRootParameter[4].InitAsConstantBufferView(1); // b1 
	slotRootParameter[5].InitAsConstantBufferView(2); // b2 

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(6, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void TexColumnsApp::BuildStandMeshRootSignature()
{
	/*
	Root Signature Layout:
	Slot 0: Diffuse texture (t0)
	Slot 1: Normal texture (t1)
	Slot 2: PerObject CBV (b0)
	Slot 3: Pass CBV (b1)
	Slot 4: Material CBV (b2)
	*/

	CD3DX12_DESCRIPTOR_RANGE diffuseRange;
	diffuseRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 - diffuse

	CD3DX12_DESCRIPTOR_RANGE normalRange;
	normalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);  // t1 - normal

	// 5 слотов (2 текстуры + 3 CBV)
	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	// Диффузная текстура
	slotRootParameter[0].InitAsDescriptorTable(1, &diffuseRange, D3D12_SHADER_VISIBILITY_PIXEL);

	// Нормальная текстура  
	slotRootParameter[1].InitAsDescriptorTable(1, &normalRange, D3D12_SHADER_VISIBILITY_PIXEL);

	// Константные буферы
	slotRootParameter[2].InitAsConstantBufferView(0); // b0 - per object (World, TexTransform)
	slotRootParameter[3].InitAsConstantBufferView(1); // b1 - per frame (View, Proj, Lights, etc.)
	slotRootParameter[4].InitAsConstantBufferView(2); // b2 - per material (DiffuseAlbedo, FresnelR0, Roughness, texture indices)

	auto staticSamplers = GetStaticSamplers();

	// Описание root signature
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// Сериализация
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	// Создание root signature
	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mStandMeshRootSignature.GetAddressOf())));
}

//TERREAIN ROOT SIGNATURE
void TexColumnsApp::BuildTerrainRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE TerrainDiffuseRange;
	TerrainDiffuseRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);  // Terrain diff t0

	CD3DX12_DESCRIPTOR_RANGE TerrainNormalRange;
	TerrainNormalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);  // Terrain norm t1

	CD3DX12_DESCRIPTOR_RANGE TerrainDispMap;
	TerrainDispMap.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);  // Terrain Disp (HeightMap) t2

	// ===== ДОБАВЛЕНО: SRV для текстуры кисти =====
	CD3DX12_DESCRIPTOR_RANGE BrushTextureRange;
	BrushTextureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);  // Brush texture t3

	// УВЕЛИЧИТЬ массив до 9 параметров (было 8)
	CD3DX12_ROOT_PARAMETER slotRootParameter[9];

	// SRV текстуры
	slotRootParameter[0].InitAsDescriptorTable(1, &TerrainDiffuseRange, D3D12_SHADER_VISIBILITY_PIXEL);  // t0
	slotRootParameter[1].InitAsDescriptorTable(1, &TerrainNormalRange, D3D12_SHADER_VISIBILITY_PIXEL);   // t1
	slotRootParameter[2].InitAsDescriptorTable(1, &TerrainDispMap, D3D12_SHADER_VISIBILITY_ALL);         // t2
	slotRootParameter[3].InitAsDescriptorTable(1, &BrushTextureRange, D3D12_SHADER_VISIBILITY_PIXEL);    // t3 <-- НОВОЕ

	// CBV
	slotRootParameter[4].InitAsConstantBufferView(0); // b0 - cbPerObject
	slotRootParameter[5].InitAsConstantBufferView(1); // b1 - cbPass
	slotRootParameter[6].InitAsConstantBufferView(2); // b2 - cbMaterial
	slotRootParameter[7].InitAsConstantBufferView(3); // b3 - cbTerrainTile
	slotRootParameter[8].InitAsConstantBufferView(4); // b4 - cbBrush

	auto staticSamplers = GetStaticSamplers();

	// Увеличить количество параметров до 9
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(9, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mTerrainRootSignature.GetAddressOf())));

	OutputDebugStringA("Terrain root signature created with brush texture slot (t3)\n");
}

void TexColumnsApp::BuildCsRootSignature()
{
	// 1. SRV для карты высот (t0)
	CD3DX12_DESCRIPTOR_RANGE srvTable;
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

	// 2. UAV для текстуры кисти (u0)
	CD3DX12_DESCRIPTOR_RANGE uavTable;
	uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // u0

	// 3. Root параметры
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// b0: BrushCB
	slotRootParameter[0].InitAsConstantBufferView(0);

	// b1: TerrainCB  
	slotRootParameter[1].InitAsConstantBufferView(1);

	// t0: Карта высот (SRV)
	slotRootParameter[2].InitAsDescriptorTable(1, &srvTable, D3D12_SHADER_VISIBILITY_ALL);

	// u0: Текстура кисти (UAV)
	slotRootParameter[3].InitAsDescriptorTable(1, &uavTable, D3D12_SHADER_VISIBILITY_ALL);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;

	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		&serializedRootSig, &errorBlob);

	if (errorBlob != nullptr)
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());

	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mBrushComputeRootSignature.GetAddressOf())));
}

void TexColumnsApp::BuildTAARootSignature()
{
	// Отдельные диапазоны для каждой текстуры
	CD3DX12_DESCRIPTOR_RANGE colorRange;
	colorRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 - Current frame

	CD3DX12_DESCRIPTOR_RANGE historyRange;
	historyRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1 - History

	CD3DX12_DESCRIPTOR_RANGE velocityRange;
	velocityRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // t2 - Velocity

	// 4 параметра: 3 таблицы + 1 CBV
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Параметр 0: Таблица с Color
	slotRootParameter[0].InitAsDescriptorTable(1, &colorRange);

	// Параметр 1: Таблица с History  
	slotRootParameter[1].InitAsDescriptorTable(1, &historyRange);

	// Параметр 2: Таблица с Velocity
	slotRootParameter[2].InitAsDescriptorTable(1, &velocityRange);

	// Параметр 3: CBV с параметрами TAA (b0)
	slotRootParameter[3].InitAsConstantBufferView(0);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		4, slotRootParameter,
		(UINT)GetStaticSamplers().size(),
		GetStaticSamplers().data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);


	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mTAARootSignature.GetAddressOf())));
}

void TexColumnsApp::BuildSkyRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	// b1 = PassCB (view/proj etc.)
	slotRootParameter[0].InitAsConstantBufferView(0);

	// b0 = AtmosphereCB
	slotRootParameter[1].InitAsConstantBufferView(1);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		2,
		slotRootParameter,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;

	ThrowIfFailed(D3D12SerializeRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf()));

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mSkyRootSignature.GetAddressOf())));
}

void TexColumnsApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	UINT numTAASRVs = 4;
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = mTextures.size() + 2 + numTAASRVs; // Все текстуры + SRV кисти + UAV кисти
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	char msg[256];
	sprintf_s(msg, "Building descriptor heap with %d descriptors\n", srvHeapDesc.NumDescriptors);
	OutputDebugStringA(msg);

	// Вывод информации о текстурах перед созданием
	sprintf_s(msg, "Total textures in mTextures: %zu\n", mTextures.size());
	OutputDebugStringA(msg);

	int texIndex = 0;
	for (const auto& tex : mTextures) {
		sprintf_s(msg, "Texture %d: %s (has resource: %s)\n",
			texIndex++,
			tex.first.c_str(),
			(tex.second && tex.second->Resource) ? "YES" : "NO");
		OutputDebugStringA(msg);
	}

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

	int offset = 0;

	// Создаем дескрипторы для всех обычных текстур
	OutputDebugStringA("\nCreating SRV descriptors:\n");
	for (const auto& tex : mTextures) {
		if (!tex.second || !tex.second->Resource) {
			sprintf_s(msg, "Skipping texture %s: missing resource\n", tex.first.c_str());
			OutputDebugStringA(msg);
			continue;
		}

		auto text = tex.second->Resource;
		srvDesc.Format = text->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = text->GetDesc().MipLevels;

		// Сохраняем информацию о создаваемом дескрипторе
		sprintf_s(msg, "  Offset %d: %s (Format: %d, Mips: %d)\n",
			offset, tex.first.c_str(), srvDesc.Format, srvDesc.Texture2D.MipLevels);
		OutputDebugStringA(msg);

		md3dDevice->CreateShaderResourceView(text.Get(), &srvDesc, hDescriptor);

		TexOffsets[tex.first] = offset;
		offset++;

		// Смещаем дескриптор только после создания
		hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	}

	// Создаем текстуру кисти после всех обычных текстур
	OutputDebugStringA("\nCreating brush texture descriptors:\n");

	sprintf_s(msg, "%d", offset);
	OutputDebugStringA(msg);

	CreateBrushTexture(hDescriptor, offset);

	sprintf_s(msg, "%d", offset);
	OutputDebugStringA(msg);

	// === ВЫВОД ИНФОРМАЦИИ О СОЗДАННОЙ КУЧЕ ===
	OutputDebugStringA("\n=== DESCRIPTOR HEAP SUMMARY ===\n");

	// 1. Вывод всех TexOffsets
	sprintf_s(msg, "TexOffsets map contains %zu entries:\n", TexOffsets.size());
	OutputDebugStringA(msg);

	for (const auto& offsetEntry : TexOffsets) {
		sprintf_s(msg, "  %s -> index %d\n", offsetEntry.first.c_str(), offsetEntry.second);
		OutputDebugStringA(msg);
	}

	// 2. Проверка наличия критических текстур террейна
	OutputDebugStringA("\nCritical terrain textures check:\n");

	const char* terrainTextures[] = { "terrainDiff", "terrainNorm", "terrainDisp" };
	for (const char* texName : terrainTextures) {
		if (TexOffsets.find(texName) == TexOffsets.end()) {
			sprintf_s(msg, "  WARNING: %s NOT FOUND in TexOffsets!\n", texName);
			OutputDebugStringA(msg);
		}
		else {
			sprintf_s(msg, "  OK: %s found at index %d\n", texName, TexOffsets[texName]);
			OutputDebugStringA(msg);
		}
	}

	// 3. Проверка индексов кисти
	OutputDebugStringA("\nBrush texture indices:\n");
	if (mBrushTextureSRVIndex >= 0) {
		sprintf_s(msg, "  Brush SRV index: %d\n", mBrushTextureSRVIndex);
		OutputDebugStringA(msg);
	}
	else {
		OutputDebugStringA("  WARNING: Brush SRV index not set!\n");
	}

	if (mBrushTextureUAVIndex >= 0) {
		sprintf_s(msg, "  Brush UAV index: %d\n", mBrushTextureUAVIndex);
		OutputDebugStringA(msg);
	}
	else {
		OutputDebugStringA("  WARNING: Brush UAV index not set!\n");
	}

	// 4. Вывод дескрипторного размера
	sprintf_s(msg, "\nDescriptor size: %u bytes\n", mCbvSrvDescriptorSize);
	OutputDebugStringA(msg);

	// 5. Проверка связности индексов
	OutputDebugStringA("\nIndex continuity check:\n");
	if (offset != mTextures.size()) {
		sprintf_s(msg, "  WARNING: offset (%d) != mTextures.size() (%zu)\n",
			offset, mTextures.size());
		OutputDebugStringA(msg);
	}
	else {
		sprintf_s(msg, "  OK: offset matches texture count\n");
		OutputDebugStringA(msg);
	}

	sprintf_s(msg, "Total descriptors created: %d\n", offset + 2); // +2 для SRV и UAV кисти
	OutputDebugStringA(msg);

	OutputDebugStringA("=== END DESCRIPTOR HEAP SUMMARY ===\n\n");
}

void TexColumnsApp::CreateTAADescriptors()
{
	char msg[256];
	sprintf_s(msg, "\n=== Creating TAA Descriptors ===\n");
	OutputDebugStringA(msg);

	// Получаем текущее количество дескрипторов для вычисления индексов
	UINT currentSRVCount = (UINT)mTextures.size()-1 + 2; // обычные текстуры + SRV кисти + UAV кисти - brushCanvas (1 текстура, 2 индекса)

	// Выделяем индексы в SRV хипе
	mTaaColorBufferSRVIndex = currentSRVCount;
	mPrevTextureSRVIndex = mTaaColorBufferSRVIndex + 1;
	mCurrentTextureSRVIndex = mPrevTextureSRVIndex + 1;
	mVelocityBufferSRVIndex = mCurrentTextureSRVIndex + 1;

	sprintf_s(msg, "  SRV Indices:\n");
	OutputDebugStringA(msg);
	sprintf_s(msg, "    Color Buffer SRV: %d\n", mTaaColorBufferSRVIndex);
	OutputDebugStringA(msg);
	sprintf_s(msg, "    Prev Texture SRV: %d\n", mPrevTextureSRVIndex);
	OutputDebugStringA(msg);
	sprintf_s(msg, "    Current Texture SRV: %d\n", mCurrentTextureSRVIndex);
	OutputDebugStringA(msg);
	sprintf_s(msg, "    Velocity SRV: %d\n", mVelocityBufferSRVIndex);
	OutputDebugStringA(msg);

	// Выделяем индексы в RTV хипе
	mTaaColorBufferRTVIndex = SwapChainBufferCount;      // после back buffers
	mPrevTextureRTVIndex = mTaaColorBufferRTVIndex + 1;
	mCurrentTextureRTVIndex = mPrevTextureRTVIndex + 1;
	mVelocityBufferRTVIndex = mCurrentTextureRTVIndex + 1;

	sprintf_s(msg, "  RTV Indices:\n");
	OutputDebugStringA(msg);
	sprintf_s(msg, "    Color Buffer RTV: %d\n", mTaaColorBufferRTVIndex);
	OutputDebugStringA(msg);
	sprintf_s(msg, "    Prev Texture RTV: %d\n", mPrevTextureRTVIndex);
	OutputDebugStringA(msg);
	sprintf_s(msg, "    Current Texture RTV: %d\n", mCurrentTextureRTVIndex);
	OutputDebugStringA(msg);
	sprintf_s(msg, "    Velocity RTV: %d\n", mVelocityBufferRTVIndex);
	OutputDebugStringA(msg);

	// ============ 1. Color Buffer (mTAAColorBuffer) ============
	// RTV для Color Buffer
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Format = mBackBufferFormat;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		mTaaColorBufferRTVIndex,
		mRtvDescriptorSize);
	md3dDevice->CreateRenderTargetView(
		mTAAColorBuffer->GetResource(),
		&rtvDesc,
		rtvHandle);
	mTAAColorBuffer->SetRTVHandle(rtvHandle);

	// SRV для Color Buffer
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = mBackBufferFormat;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		mTaaColorBufferSRVIndex,
		mCbvSrvDescriptorSize);
	md3dDevice->CreateShaderResourceView(
		mTAAColorBuffer->GetResource(),
		&srvDesc,
		srvHandle);
	mTAAColorBuffer->SetSRVHandle(srvHandle);

	// ============ 2. Prev Texture (mTAAPrevTexture) ============
	// RTV для Prev Texture
	CD3DX12_CPU_DESCRIPTOR_HANDLE prevRtvHandle(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		mPrevTextureRTVIndex,
		mRtvDescriptorSize);
	md3dDevice->CreateRenderTargetView(
		mTAAPrevTexture->GetResource(),
		&rtvDesc,
		prevRtvHandle);
	mTAAPrevTexture->SetRTVHandle(prevRtvHandle);

	// SRV для Prev Texture
	CD3DX12_CPU_DESCRIPTOR_HANDLE prevSrvHandle(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		mPrevTextureSRVIndex,
		mCbvSrvDescriptorSize);
	md3dDevice->CreateShaderResourceView(
		mTAAPrevTexture->GetResource(),
		&srvDesc,
		prevSrvHandle);
	mTAAPrevTexture->SetSRVHandle(prevSrvHandle);

	// ============ 3. Current Texture (mTAACurrentTexture) ============
	// RTV для Current Texture
	CD3DX12_CPU_DESCRIPTOR_HANDLE currentRtvHandle(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		mCurrentTextureRTVIndex,
		mRtvDescriptorSize);
	md3dDevice->CreateRenderTargetView(
		mTAACurrentTexture->GetResource(),
		&rtvDesc,
		currentRtvHandle);
	mTAACurrentTexture->SetRTVHandle(currentRtvHandle);

	// SRV для Current Texture
	CD3DX12_CPU_DESCRIPTOR_HANDLE currentSrvHandle(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		mCurrentTextureSRVIndex,
		mCbvSrvDescriptorSize);
	md3dDevice->CreateShaderResourceView(
		mTAACurrentTexture->GetResource(),
		&srvDesc,
		currentSrvHandle);
	mTAACurrentTexture->SetSRVHandle(currentSrvHandle);

	// ============ 4. Velocity Buffer (mTAVelocityBuffer) ============
	// RTV для Velocity
	D3D12_RENDER_TARGET_VIEW_DESC velRtvDesc = {};
	velRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	velRtvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	velRtvDesc.Texture2D.MipSlice = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE velRtvHandle(
		mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
		mVelocityBufferRTVIndex,
		mRtvDescriptorSize);
	md3dDevice->CreateRenderTargetView(
		mTAVelocityBuffer->GetResource(),
		&velRtvDesc,
		velRtvHandle);
	mTAVelocityBuffer->SetRTVHandle(velRtvHandle);

	// SRV для Velocity
	D3D12_SHADER_RESOURCE_VIEW_DESC velSrvDesc = {};
	velSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	velSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	velSrvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	velSrvDesc.Texture2D.MostDetailedMip = 0;
	velSrvDesc.Texture2D.MipLevels = 1;

	CD3DX12_CPU_DESCRIPTOR_HANDLE velSrvHandle(
		mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		mVelocityBufferSRVIndex,
		mCbvSrvDescriptorSize);
	md3dDevice->CreateShaderResourceView(
		mTAVelocityBuffer->GetResource(),
		&velSrvDesc,
		velSrvHandle);
	mTAVelocityBuffer->SetSRVHandle(velSrvHandle);

	// Дополнительно проверьте формат текстуры
	D3D12_RESOURCE_DESC desc = mTAVelocityBuffer->GetResource()->GetDesc();
	sprintf_s(msg, "Velocity Texture - Format: %d, Width: %d, Height: %d\n",
		desc.Format, desc.Width, desc.Height);
	OutputDebugStringA(msg);

	sprintf_s(msg, "TAA Descriptors created successfully\n");
	OutputDebugStringA(msg);
}

void TexColumnsApp::CreateBrushTexture(CD3DX12_CPU_DESCRIPTOR_HANDLE baseDescriptorHandle, int baseOffset)
{
	// 1. Создаем текстуру с UAV флагом
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mBrushTextureWidth;
	texDesc.Height = mBrushTextureHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&defaultHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&mBrushTexture)));

	// 2. Создаем SRV для текстуры кисти
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	// SRV идет сразу после всех обычных текстур
	mBrushTextureSRV = baseDescriptorHandle;
	md3dDevice->CreateShaderResourceView(mBrushTexture.Get(), &srvDesc, mBrushTextureSRV);

	// 3. Смещаем дескриптор для UAV
	CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle = baseDescriptorHandle;
	uavHandle.Offset(1, mCbvSrvDescriptorSize);

	// 4. Создаем UAV для текстуры кисти
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = texDesc.Format;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;
	uavDesc.Texture2D.PlaneSlice = 0;

	mBrushTextureUAV = uavHandle;
	md3dDevice->CreateUnorderedAccessView(mBrushTexture.Get(), nullptr, &uavDesc, mBrushTextureUAV);

	// 5. Добавляем текстуру в mTextures для управления ресурсами
	auto brushTex = std::make_unique<Texture>();
	brushTex->Name = "brushCanvas";
	brushTex->Resource = mBrushTexture;
	brushTex->Filename = L"";

	// Сохраняем индексы для быстрого доступа
	mBrushTextureSRVIndex = baseOffset;
	mBrushTextureUAVIndex = baseOffset + 1;

	mTextures["brushCanvas"] = std::move(brushTex);

	// 6. Инициализируем текстуру черным цветом
	//InitializeBrushTexture();
}

void TexColumnsApp::CreateTAAColorBuffer()
{

	mTAAColorBuffer = std::make_unique<TAATexture>(
		md3dDevice.Get(),
		mBackBufferFormat,           // Тот же формат что и back buffer
		mClientWidth,
		mClientHeight,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);
	OutputDebugStringA("Color texture created\n");
}

void TexColumnsApp::CreateTAAPrevTexture()
{
	mTAAPrevTexture = std::make_unique<TAATexture>(
		md3dDevice.Get(),
		mBackBufferFormat,
		mClientWidth,
		mClientHeight,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET  // RTV для записи истории
	);
	OutputDebugStringA("Prev history texture created\n");
}

void TexColumnsApp::CreateTAACurrentTexture()
{
	mTAACurrentTexture = std::make_unique<TAATexture>(
		md3dDevice.Get(),
		mBackBufferFormat,
		mClientWidth,
		mClientHeight,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET  // RTV для записи истории
	);
	OutputDebugStringA("Current history texture created\n");
}

void TexColumnsApp::CreateVelocityBuffer()
{
	mTAVelocityBuffer = std::make_unique<TAATexture>(
		md3dDevice.Get(),
		DXGI_FORMAT_R16G16_FLOAT,    // 16-bit достаточно для velocity
		mClientWidth,
		mClientHeight,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);
	OutputDebugStringA("Velocity texture created\n");

}

void TexColumnsApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["opaqueVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaqueHS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "HS", "hs_5_1");
	mShaders["opaqueDS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "DS", "ds_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["wirePS"] = d3dUtil::CompileShader(L"Shaders\\Wireframe.hlsl", nullptr, "WirePS", "ps_5_1");

	mShaders["terrainVS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["terrainPS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["wireTerrPS"] = d3dUtil::CompileShader(L"Shaders\\Terrain.hlsl", nullptr, "WirePS", "ps_5_1"); 

	mShaders["brushCS"] = d3dUtil::CompileShader(L"Shaders\\Brush.hlsl", nullptr, "BrushCS", "cs_5_1");

	mShaders["StandMeshVS"] = d3dUtil::CompileShader(L"Shaders\\MeshStandard.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["StandMeshPS"] = d3dUtil::CompileShader(L"Shaders\\MeshStandard.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["wireStandMeshPS"] = d3dUtil::CompileShader(L"Shaders\\MeshStandard.hlsl", nullptr, "WirePS", "ps_5_1");

	mShaders["TaaVS"] = d3dUtil::CompileShader(L"Shaders\\TAA.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["TaaPS"] = d3dUtil::CompileShader(L"Shaders\\TAA.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["skyVS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["MCVS"] = d3dUtil::CompileShader(L"Shaders\\MarchingCubes.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["MCPS"] = d3dUtil::CompileShader(L"Shaders\\MarchingCubes.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["MCWirePS"] = d3dUtil::CompileShader(L"Shaders\\MarchingCubes.hlsl", nullptr, "WirePS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void TexColumnsApp::BuildPSOs()
{
	OutputDebugStringA("=== Building PSOs ===\n");
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc; //HeightMaps!!!! //TODO rename

	//
	// PSO for tesselated objects.
	//
	{
		ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
		opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
		opaquePsoDesc.pRootSignature = mRootSignature.Get();
		opaquePsoDesc.VS =
		{
			reinterpret_cast<BYTE*>(mShaders["opaqueVS"]->GetBufferPointer()),
			mShaders["opaqueVS"]->GetBufferSize()
		};
		opaquePsoDesc.HS =
		{
			reinterpret_cast<BYTE*>(mShaders["opaqueHS"]->GetBufferPointer()),
			mShaders["opaqueHS"]->GetBufferSize()
		};
		opaquePsoDesc.DS =
		{
			reinterpret_cast<BYTE*>(mShaders["opaqueDS"]->GetBufferPointer()),
			mShaders["opaqueDS"]->GetBufferSize()
		};
		opaquePsoDesc.PS =
		{
			reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
			mShaders["opaquePS"]->GetBufferSize()
		};
		opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

		opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;//D3D12_FILL_MODE_SOLID; //D3D12_FILL_MODE_WIREFRAME; //wire solid

		opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		opaquePsoDesc.SampleMask = UINT_MAX;
		opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
		opaquePsoDesc.NumRenderTargets = 2;
		opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
		opaquePsoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;    // Velocity
		opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
		opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
		opaquePsoDesc.DSVFormat = mDepthStencilFormat;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));
	}

	//PSO for WIREFRAME
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframePsoDesc = opaquePsoDesc;
		wireframePsoDesc.PS =
		{
			reinterpret_cast<BYTE*>(mShaders["wirePS"]->GetBufferPointer()),
			mShaders["wirePS"]->GetBufferSize()
		};
		wireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&wireframePsoDesc, IID_PPV_ARGS(&mPSOs["wireframe"])));
	}
	//
    // PSO for meshes (diffuse + normal)
   //
	{
		OutputDebugStringA("Creating Standard Model PSO...\n");
		D3D12_GRAPHICS_PIPELINE_STATE_DESC standardPsoDesc;

		ZeroMemory(&standardPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
		standardPsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
		standardPsoDesc.pRootSignature = mStandMeshRootSignature.Get();

		standardPsoDesc.VS =
		{
			reinterpret_cast<BYTE*>(mShaders["StandMeshVS"]->GetBufferPointer()),
			mShaders["StandMeshVS"]->GetBufferSize()
		};
		standardPsoDesc.HS = { nullptr, 0 };
		standardPsoDesc.DS = { nullptr, 0 };

		standardPsoDesc.PS =
		{
			reinterpret_cast<BYTE*>(mShaders["StandMeshPS"]->GetBufferPointer()),
			mShaders["StandMeshPS"]->GetBufferSize()
		};

		standardPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		standardPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		standardPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		standardPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		standardPsoDesc.SampleMask = UINT_MAX;
		standardPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		standardPsoDesc.NumRenderTargets = 2;
		standardPsoDesc.RTVFormats[0] = mBackBufferFormat;
		standardPsoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
		standardPsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
		standardPsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
		standardPsoDesc.DSVFormat = mDepthStencilFormat;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
			&standardPsoDesc, IID_PPV_ARGS(&mPSOs["standardMesh"])));

		// PSO для wireframe стандартных моделей
		D3D12_GRAPHICS_PIPELINE_STATE_DESC wireStandardPsoDesc = standardPsoDesc;
		wireStandardPsoDesc.PS =
		{
			reinterpret_cast<BYTE*>(mShaders["wireStandMeshPS"]->GetBufferPointer()),
			mShaders["wireStandMeshPS"]->GetBufferSize()
		};
		wireStandardPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
			&wireStandardPsoDesc, IID_PPV_ARGS(&mPSOs["wireStandardMesh"])));

		OutputDebugStringA("Creating Compute PSO...\n");
	}
	// PSO for Marching Cubes mesh (triplanar pixel shader)
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC mcPsoDesc;
		ZeroMemory(&mcPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
		mcPsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
		mcPsoDesc.pRootSignature = mStandMeshRootSignature.Get(); // same root sig as Meshes

		mcPsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["MCVS"]->GetBufferPointer()),
						 mShaders["MCVS"]->GetBufferSize() };
		mcPsoDesc.HS = { nullptr, 0 };
		mcPsoDesc.DS = { nullptr, 0 };
		mcPsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["MCPS"]->GetBufferPointer()),
						 mShaders["MCPS"]->GetBufferSize() };

		mcPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		mcPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		mcPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		mcPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		mcPsoDesc.SampleMask = UINT_MAX;
		mcPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		mcPsoDesc.NumRenderTargets = 2;
		mcPsoDesc.RTVFormats[0] = mBackBufferFormat;
		mcPsoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT; // velocity
		mcPsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
		mcPsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
		mcPsoDesc.DSVFormat = mDepthStencilFormat;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
			&mcPsoDesc, IID_PPV_ARGS(&mPSOs["mc"])));

		// Wireframe variant
		D3D12_GRAPHICS_PIPELINE_STATE_DESC mcWirePsoDesc = mcPsoDesc;
		mcWirePsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["MCWirePS"]->GetBufferPointer()),
							 mShaders["MCWirePS"]->GetBufferSize() };
		mcWirePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
			&mcWirePsoDesc, IID_PPV_ARGS(&mPSOs["mcWire"])));
	}

	// 2. Compute PSO
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc;
		ZeroMemory(&computePsoDesc, sizeof(D3D12_COMPUTE_PIPELINE_STATE_DESC));

		computePsoDesc.pRootSignature = mBrushComputeRootSignature.Get();

		computePsoDesc.CS =
		{
			reinterpret_cast<BYTE*>(mShaders["brushCS"]->GetBufferPointer()),
			mShaders["brushCS"]->GetBufferSize()
		};
		computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
		computePsoDesc.NodeMask = 0;

		ThrowIfFailed(md3dDevice->CreateComputePipelineState(
			&computePsoDesc, IID_PPV_ARGS(&mPSOs["brushCompute"])));
	}
	//
	// PSO for Terrain.
	//
	{
		OutputDebugStringA("Creating Terrain PSO...\n");
		D3D12_GRAPHICS_PIPELINE_STATE_DESC terrainPsoDesc;

		ZeroMemory(&terrainPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
		terrainPsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
		terrainPsoDesc.pRootSignature = mTerrainRootSignature.Get();
		terrainPsoDesc.VS =
		{
			reinterpret_cast<BYTE*>(mShaders["terrainVS"]->GetBufferPointer()),
			mShaders["terrainVS"]->GetBufferSize()
		};
		terrainPsoDesc.PS =
		{
			reinterpret_cast<BYTE*>(mShaders["terrainPS"]->GetBufferPointer()),
			mShaders["terrainPS"]->GetBufferSize()
		};
		terrainPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		terrainPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;

		terrainPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		terrainPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		terrainPsoDesc.SampleMask = UINT_MAX;
		terrainPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		terrainPsoDesc.NumRenderTargets = 2;
		terrainPsoDesc.RTVFormats[0] = mBackBufferFormat;
		terrainPsoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;

		terrainPsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
		terrainPsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
		terrainPsoDesc.DSVFormat = mDepthStencilFormat;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&terrainPsoDesc, IID_PPV_ARGS(&mPSOs["terrain"])));

		//PSO for wireframe terrain
		OutputDebugStringA("Creating Wire Terrain PSO...\n");
		D3D12_GRAPHICS_PIPELINE_STATE_DESC terrWirePsoDesc = terrainPsoDesc;
		terrWirePsoDesc.PS =
		{
			reinterpret_cast<BYTE*>(mShaders["wireTerrPS"]->GetBufferPointer()),
			mShaders["wireTerrPS"]->GetBufferSize()
		};
		terrWirePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&terrWirePsoDesc, IID_PPV_ARGS(&mPSOs["wireTerrain"])));

	}
	//PSO for TAA
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC taaPsoDesc;

		ZeroMemory(&taaPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
		taaPsoDesc.InputLayout = { nullptr, 0 };

		taaPsoDesc.pRootSignature = mTAARootSignature.Get();
		taaPsoDesc.VS = { mShaders["TaaVS"]->GetBufferPointer(), mShaders["TaaVS"]->GetBufferSize() };
		taaPsoDesc.PS = { mShaders["TaaPS"]->GetBufferPointer(), mShaders["TaaPS"]->GetBufferSize() };
		taaPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		taaPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		taaPsoDesc.DepthStencilState.DepthEnable = false;
		taaPsoDesc.DepthStencilState.StencilEnable = false;
		taaPsoDesc.SampleMask = UINT_MAX;
		taaPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		taaPsoDesc.NumRenderTargets = 2;
		taaPsoDesc.RTVFormats[0] = mBackBufferFormat;   // BackBuffer (SV_Target0)
		taaPsoDesc.RTVFormats[1] = mBackBufferFormat;   // History    (SV_Target1)
		taaPsoDesc.RTVFormats[2] = DXGI_FORMAT_UNKNOWN; // no third target
		taaPsoDesc.SampleDesc.Count = 1;
		taaPsoDesc.SampleDesc.Quality = 0;
		taaPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
			&taaPsoDesc,
			IID_PPV_ARGS(&mPSOs["TAA"])));
	}

	//PSO for Sky
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

		psoDesc.InputLayout = { nullptr, 0 }; // fullscreen triangle
		psoDesc.pRootSignature = mSkyRootSignature.Get();
		psoDesc.VS =
		{
			reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
			mShaders["skyVS"]->GetBufferSize()
		};
		psoDesc.PS =
		{
			reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
			mShaders["skyPS"]->GetBufferSize()
		};

		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		//psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		//psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = mBackBufferFormat;  // или HDR format если используешь HDR
		psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;

		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
			&psoDesc,
			IID_PPV_ARGS(&mPSOs["sky"])));
	}

	OutputDebugStringA("PSOs Created.\n");
}

void TexColumnsApp::BuildFrameResources()
{
	FlushCommandQueue();
	mFrameResources.clear();
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), (UINT)mTerrain->GetAllTiles().size(), 1
		));
	}
	mCurrFrameResourceIndex = 0;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();
	for (auto& ri : mAllRitems)
	{
		ri->NumFramesDirty = gNumFrameResources;
	}
	for (auto& kv : mMaterials)
	{
		kv.second->NumFramesDirty = gNumFrameResources;
	}
	for (auto& t : mTerrain->GetAllTiles())
	{
		t->NumFramesDirty = gNumFrameResources;
	}
}

void TexColumnsApp::BuildCustomMeshGeometry(std::string name, UINT& meshVertexOffset, UINT& meshIndexOffset, UINT& prevVertSize, UINT& prevIndSize, std::vector<Vertex>& vertices, std::vector<std::uint16_t>& indices, MeshGeometry* Geo)
{
	std::vector<GeometryGenerator::MeshData> meshDatas; //структура для хранения вершин и индексов

	// Создаем инстанс импортера.
	Assimp::Importer importer;
	// Читаем файл с постпроцессингом: триангуляция, флип UV (если нужно) и генерация нормалей.
	const aiScene* scene = importer.ReadFile("../../Models/" + name + ".obj",
		aiProcess_Triangulate |
		aiProcess_ConvertToLeftHanded |
		aiProcess_FlipUVs |
		aiProcess_GenNormals |
		aiProcess_CalcTangentSpace);
	if (!scene || !scene->mRootNode)
	{
		std::cerr << "Assimp error: " << importer.GetErrorString() << std::endl;
	}
	unsigned int nMeshes = scene->mNumMeshes;
	ObjectsMeshCount[name] = nMeshes;

	for (int i = 0; i < scene->mNumMeshes; i++)
	{
		GeometryGenerator::MeshData meshData;
		aiMesh* mesh = scene->mMeshes[i];

		// Подготовка контейнеров для вершин и индексов.
		std::vector<GeometryGenerator::Vertex> vertices;
		std::vector<std::uint16_t> indices;

		// Проходим по всем вершинам и копируем данные.
		for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
		{
			GeometryGenerator::Vertex v;

			v.Position.x = mesh->mVertices[i].x;
			v.Position.y = mesh->mVertices[i].y;
			v.Position.z = mesh->mVertices[i].z;

			if (mesh->HasNormals())
			{
				v.Normal.x = mesh->mNormals[i].x;
				v.Normal.y = mesh->mNormals[i].y;
				v.Normal.z = mesh->mNormals[i].z;
			}

			if (mesh->HasTextureCoords(0))
			{
				v.TexC.x = mesh->mTextureCoords[0][i].x;
				v.TexC.y = mesh->mTextureCoords[0][i].y;
			}
			else
			{
				v.TexC = XMFLOAT2(0.0f, 0.0f);
			}
			if (mesh->HasTangentsAndBitangents())
			{
				v.TangentU.x = mesh->mTangents[i].x;
				v.TangentU.y = mesh->mTangents[i].y;
				v.TangentU.z = mesh->mTangents[i].z;

			}

			// Если необходимо, можно обработать тангенты и другие атрибуты.
			vertices.push_back(v);
		}
		// Проходим по всем граням для формирования индексов.
		for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
		{
			aiFace face = mesh->mFaces[i];
			// Убедимся, что грань треугольная.
			if (face.mNumIndices != 3) continue;
			indices.push_back(static_cast<std::uint16_t>(face.mIndices[0]));
			indices.push_back(static_cast<std::uint16_t>(face.mIndices[1]));
			indices.push_back(static_cast<std::uint16_t>(face.mIndices[2]));
		}

		// Заполняем meshData. Здесь тебе нужно адаптировать под свою структуру:
		meshData.Vertices = vertices;
		meshData.Indices32.resize(indices.size());
		for (size_t j = 0; j < indices.size(); ++j)
			meshData.Indices32[j] = indices[j];

		aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
		aiString texturePath;

		aiString texPath;

		meshData.matName = scene->mMaterials[mesh->mMaterialIndex]->GetName().C_Str();
		// Если требуется, можно выполнить дополнительные операции, например, нормализацию, вычисление тангенсов и т.д.
		meshDatas.push_back(meshData);
	}
	for (int k = 0; k < scene->mNumMaterials; k++)
	{
		aiString texPath;

		//Diffuse
		std::string diffuseName = "";
		if (scene->mMaterials[k]->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
		{
			diffuseName = std::string(texPath.C_Str());
			diffuseName = diffuseName.substr(0, diffuseName.length() - 4); //file extension
			std::cout << "DIFFUSE: " << diffuseName << "\n";
		}
		else
		{
			// Если диффузной карты нет, используем заглушку
			diffuseName = "default";
			std::cout << "DIFFUSE: default (using fallback)\n";
		}

		//Normal
		std::string normalName = "";
		bool hasNormalMap = false;

		if (scene->mMaterials[k]->GetTexture(aiTextureType_NORMALS, 0, &texPath) == AI_SUCCESS ||
			scene->mMaterials[k]->GetTexture(aiTextureType_HEIGHT, 0, &texPath) == AI_SUCCESS)
		{
			normalName = std::string(texPath.C_Str());
			normalName = normalName.substr(0, normalName.length() - 4);
			hasNormalMap = true;
			std::cout << "NORMAL: " << normalName << "\n";
		}
		else
		{
			// !!! ЕСЛИ НОРМАЛЬНОЙ КАРТЫ НЕТ, ИСПОЛЬЗУЕМ ЗАГЛУШКУ !!!
			normalName = "default_normal";  // Имя заглушки
			hasNormalMap = false;
			std::cout << "NORMAL: default_normal (using fallback)\n";
		}

		// Displacement
		//std::string displacementName = "";
		//if (scene->mMaterials[k]->GetTexture(aiTextureType_DISPLACEMENT, 0, &texPath) == AI_SUCCESS)
		//{
		//	displacementName = std::string(texPath.C_Str());
		//	displacementName = displacementName.substr(0, displacementName.length() - 4);
		//	std::cout << "DISPLACEMENT: " << displacementName << "\n";
		//}

		// Проверяем наличие смещений в TexOffsets
		int diffuseIndex = TexOffsets.find(diffuseName) != TexOffsets.end() ? TexOffsets[diffuseName] : 0;
		int normalIndex = TexOffsets.find(normalName) != TexOffsets.end() ? TexOffsets[normalName] : 0;
		int displacementIndex = -1;//TexOffsets.find(displacementName) != TexOffsets.end() ? TexOffsets[displacementName] : 0;

		// Создаем материал со всеми текстурами
		CreateMaterial(
			scene->mMaterials[k]->GetName().C_Str(),
			k,
			diffuseIndex,
			normalIndex,
			displacementIndex,
			XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
			XMFLOAT3(0.05f, 0.05f, 0.05f),
			0.3f
		);
	}

	UINT totalMeshSize = 0;
	UINT k = vertices.size();
	std::vector<std::pair<GeometryGenerator::MeshData, SubmeshGeometry>>meshSubmeshes;
	for (auto mesh : meshDatas)
	{
		meshVertexOffset = meshVertexOffset + prevVertSize;
		prevVertSize = mesh.Vertices.size();
		totalMeshSize += mesh.Vertices.size();

		meshIndexOffset = meshIndexOffset + prevIndSize;
		prevIndSize = mesh.Indices32.size();
		SubmeshGeometry meshSubmesh;
		meshSubmesh.IndexCount = (UINT)mesh.Indices32.size();
		meshSubmesh.StartIndexLocation = meshIndexOffset;
		meshSubmesh.BaseVertexLocation = meshVertexOffset;

		BoundingBox::CreateFromPoints(meshSubmesh.Bounds,
			mesh.Vertices.size(),
			&mesh.Vertices[0].Position,
			sizeof(Vertex)
		);
		GeometryGenerator::MeshData m = mesh;
		meshSubmeshes.push_back(std::make_pair(m, meshSubmesh));
	}
	/////////
	/////
	for (auto mesh : meshDatas)
	{
		for (size_t i = 0; i < mesh.Vertices.size(); ++i, ++k)
		{
			vertices.push_back(Vertex(mesh.Vertices[i].Position, mesh.Vertices[i].Normal, mesh.Vertices[i].TexC, mesh.Vertices[i].TangentU));
		}
	}
	////////

	///////
	for (auto mesh : meshDatas)
	{
		indices.insert(indices.end(), std::begin(mesh.GetIndices16()), std::end(mesh.GetIndices16()));
	}
	///////
	Geo->MultiDrawArgs[name] = meshSubmeshes;
}

void TexColumnsApp::BuildAllCustomMeshes(UINT& meshVertexOffset, UINT& meshIndexOffset, UINT& prevVertSize, UINT& prevIndSize, std::vector<Vertex>& vertices, std::vector<std::uint16_t>& indices, MeshGeometry* Geo)
{
	BuildCustomMeshGeometry("Guard", meshVertexOffset, meshIndexOffset, prevVertSize, prevIndSize, vertices, indices, Geo);
	BuildCustomMeshGeometry("maxwell", meshVertexOffset, meshIndexOffset, prevVertSize, prevIndSize, vertices, indices, Geo);
}

void TexColumnsApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(10.0f, 10.0f, 2, 2);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	UINT meshVertexOffset = cylinderVertexOffset; // Обновлено смещение
	UINT meshIndexOffset = cylinderIndexOffset;   // Обновлено смещение
	UINT prevIndSize = (UINT)cylinder.Indices32.size(); // Обновлено
	UINT prevVertSize = (UINT)cylinder.Vertices.size(); // Обновлено

	BuildAllCustomMeshes(meshVertexOffset, meshIndexOffset, prevVertSize, prevIndSize, vertices, indices, geo.get());

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void TexColumnsApp::GenerateTileGeometry(const XMFLOAT3& worldPos, float tileSize, int lodLevel, std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices)
{
	int minResolution = 8;    

	int resolution = (minResolution << lodLevel);
	resolution = std::min(resolution, minResolution);
	vertices.clear();
	indices.clear();

	//float scale = 1;

	float stepSize = (tileSize) / (resolution - 1);
	float curtainDepth = 50;

	//main vertices
	for (int z = 0; z < resolution; z++)
	{
		for (int x = 0; x < resolution; x++)
		{
			Vertex vertex;
			vertex.Pos = XMFLOAT3(worldPos.x + x * stepSize, worldPos.y, worldPos.z + z * stepSize);
			vertex.TexC = XMFLOAT2((float)x / (resolution - 1), (float)z / (resolution - 1));
			vertex.Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
			vertex.Tangent = XMFLOAT3(1.0f, 0.0f, 0.0f);
			vertices.push_back(vertex);
		}
	}

	int mainVertexCount = static_cast<int>(vertices.size());

	//curtain vertices
	// Left (x = 0)
	for (int z = 0; z < resolution; z++)
	{
		Vertex vertex = vertices[z * resolution];
		vertex.Pos.y = worldPos.y -curtainDepth;
		vertices.push_back(vertex);
	}

	// right (x = resolution-1)  
	for (int z = 0; z < resolution; z++)
	{
		Vertex vertex = vertices[z * resolution + (resolution - 1)];
		vertex.Pos.y = worldPos.y -curtainDepth;
		vertices.push_back(vertex);
	}

	// bottom (z = 0)
	for (int x = 0; x < resolution ; x++)
	{
		Vertex vertex = vertices[0 * resolution + x];
		vertex.Pos.y = worldPos.y -curtainDepth;
		vertices.push_back(vertex);
	}

	// top(z = resolution-1)
	for (int x = 0; x < resolution ; x++)
	{
		Vertex vertex = vertices[(resolution - 1) * resolution + x];
		vertex.Pos.y = worldPos.y -curtainDepth;
		vertices.push_back(vertex);
	}

	//Indices
	for (int z = 0; z < resolution - 1; z++)
	{
		for (int x = 0; x < resolution - 1; x++)
		{
			UINT topLeft = z * resolution + x;
			UINT topRight = topLeft + 1;
			UINT bottomLeft = (z + 1) * resolution + x;
			UINT bottomRight = bottomLeft + 1;

			indices.push_back(topLeft);
			indices.push_back(bottomLeft);
			indices.push_back(topRight);

			indices.push_back(topRight);
			indices.push_back(bottomLeft);
			indices.push_back(bottomRight);
		}
	}

	// curtain indices
	int leftCurtainStart = mainVertexCount;
	int rightCurtainStart = leftCurtainStart + resolution;
	int bottomCurtainStart = rightCurtainStart + resolution;
	int topCurtainStart = bottomCurtainStart + resolution;

	//left
	for (int z = 0; z < resolution - 1; z++)
	{
		UINT edge1 = z * resolution;
		UINT edge2 = (z + 1) * resolution;
		UINT curtain1 = leftCurtainStart + z;
		UINT curtain2 = leftCurtainStart + z + 1;

		indices.push_back(edge1);
		indices.push_back(curtain1);
		indices.push_back(edge2);

		indices.push_back(edge2);
		indices.push_back(curtain1);
		indices.push_back(curtain2);
	}

	// right 
	for (int z = 0; z < resolution - 1; z++)
	{
		UINT edge1 = z * resolution + (resolution - 1);
		UINT edge2 = (z + 1) * resolution + (resolution - 1);
		UINT curtain1 = rightCurtainStart + z;
		UINT curtain2 = rightCurtainStart + z + 1;

		indices.push_back(edge1);
		indices.push_back(edge2);
		indices.push_back(curtain1);

		indices.push_back(edge2);
		indices.push_back(curtain2);
		indices.push_back(curtain1);
	}

	// bottom
	for (int x = 0; x < resolution - 1; x++)
	{
		UINT edge1 = x;
		UINT edge2 = x + 1;
		UINT curtain1 = bottomCurtainStart + x;
		UINT curtain2 = bottomCurtainStart + x+1;

		indices.push_back(edge1);
		indices.push_back(edge2);
		indices.push_back(curtain1);

		indices.push_back(edge2);
		indices.push_back(curtain2);
		indices.push_back(curtain1);
	}

	// top
	for (int x = 0; x < resolution - 1; x++)
	{
		UINT edge1 = (resolution - 1) * resolution + x;
		UINT edge2 = (resolution - 1) * resolution + x + 1;
		UINT curtain1 = topCurtainStart + x;
		UINT curtain2 = topCurtainStart + x+1;

		indices.push_back(edge1);
		indices.push_back(curtain1);
		indices.push_back(edge2);

		indices.push_back(edge2);
		indices.push_back(curtain1);
		indices.push_back(curtain2);
	}
}
void TexColumnsApp::BuildTerrainGeometry()
{
	auto terrainGeo = std::make_unique<MeshGeometry>();
	terrainGeo->Name = "terrainGeo";

	auto& allTiles = mTerrain->GetAllTiles();

	std::vector<Vertex> allVertices;
	std::vector<std::uint32_t> allIndices;

	for (int tileIdx = 0; tileIdx < allTiles.size(); tileIdx++)
	{
		auto& tile = allTiles[tileIdx];

		std::vector<Vertex> tileVertices;
		std::vector<std::uint32_t> tileIndices;

		GenerateTileGeometry(tile->worldPos, tile->tileSize, tile->lodLevel, tileVertices, tileIndices);

		// ������� ������� �� ���������� ��� ����������� ������
		UINT baseVertex = static_cast<int>(allVertices.size());
		for (auto& index : tileIndices)
		{
			index += baseVertex;
		}

		// ��������� submesh
		SubmeshGeometry submesh;
		submesh.IndexCount = (UINT)tileIndices.size();
		submesh.StartIndexLocation = (UINT)allIndices.size();
		submesh.BaseVertexLocation = 0;

		std::string submeshName = "tile_" + std::to_string(tileIdx) + "_LOD_" + std::to_string(tile->lodLevel);
		terrainGeo->DrawArgs[submeshName] = submesh;

		// ��������� � ����� �������
		allVertices.insert(allVertices.end(), tileVertices.begin(), tileVertices.end());
		allIndices.insert(allIndices.end(), tileIndices.begin(), tileIndices.end());

	}

	const UINT vbByteSize = (UINT)allVertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)allIndices.size() * sizeof(std::uint32_t);

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &terrainGeo->VertexBufferCPU));
	CopyMemory(terrainGeo->VertexBufferCPU->GetBufferPointer(), allVertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &terrainGeo->IndexBufferCPU));
	CopyMemory(terrainGeo->IndexBufferCPU->GetBufferPointer(), allIndices.data(), ibByteSize);

	terrainGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		allVertices.data(), vbByteSize,
		terrainGeo->VertexBufferUploader);

	terrainGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		allIndices.data(), ibByteSize,
		terrainGeo->IndexBufferUploader);

	terrainGeo->VertexByteStride = sizeof(Vertex);
	terrainGeo->VertexBufferByteSize = vbByteSize;
	terrainGeo->IndexFormat = DXGI_FORMAT_R32_UINT;
	terrainGeo->IndexBufferByteSize = ibByteSize;

	mGeometries[terrainGeo->Name] = std::move(terrainGeo);
}

void TexColumnsApp::BuildMarchingCubesMesh()
{

	//Height map

	DirectX::ScratchImage scratchImage;
	HRESULT hr = DirectX::LoadFromDDSFile(
		L"../../Textures/stone_disp.dds",
		DirectX::DDS_FLAGS_NONE,
		nullptr,
		scratchImage);

	if (FAILED(hr))
	{
		OutputDebugStringA("BuildMarchingCubesMesh: Failed to load stone_disp.dds\n");
		return;
	}


	DirectX::ScratchImage decompressed;
	const DirectX::Image* srcImage = scratchImage.GetImage(0, 0, 0);

	if (DirectX::IsCompressed(srcImage->format))
	{
		hr = DirectX::Decompress(
			*srcImage,
			DXGI_FORMAT_R8G8B8A8_UNORM,   // intermediate uncompressed format
			decompressed);

		if (FAILED(hr))
		{
			OutputDebugStringA("BuildMarchingCubesMesh: Decompress failed\n");
			return;
		}
		srcImage = decompressed.GetImage(0, 0, 0);

		char dbg[128];
		sprintf_s(dbg, "BuildMarchingCubesMesh: Decompressed to format %u, size %ux%u\n",
			(UINT)srcImage->format, (UINT)srcImage->width, (UINT)srcImage->height);
		OutputDebugStringA(dbg);
	}

	DirectX::ScratchImage converted;
	hr = DirectX::Convert(
		*srcImage,
		DXGI_FORMAT_R32_FLOAT,
		DirectX::TEX_FILTER_DEFAULT,
		DirectX::TEX_THRESHOLD_DEFAULT,
		converted);

	if (FAILED(hr))
	{
		OutputDebugStringA("BuildMarchingCubesMesh: Convert to R32_FLOAT failed\n");
		return;
	}

	const DirectX::Image* img = converted.GetImage(0, 0, 0);
	const float* data = reinterpret_cast<const float*>(img->pixels);
	int                   texW = (int)img->width;
	int                   texH = (int)img->height;

	char dbgFmt[128];
	sprintf_s(dbgFmt, "BuildMarchingCubesMesh: Heightmap %dx%d, format %u\n",
		texW, texH, (UINT)img->format);
	OutputDebugStringA(dbgFmt);


	const int   fieldX = 64;
	const int   fieldY = 48;
	const int   fieldZ = 64;
	const float plateauScale = 18;
	const float cellSize = 16.0f;   // → total X/Z extent = 128 * 8 = 1024


	MarchingCubes::ScalarField field;
	if (mUseHeightTexture)
	{
		 field = MarchingCubes::HeightmapToScalarField(
			data, texW, texH,
			fieldX, fieldY, fieldZ,
			plateauScale,
			cellSize);
	}
	else
	{
		field = MarchingCubes::PerlinScalarField(
			fieldX, fieldY, fieldZ, cellSize, mNoiseParams);
	}

	// ── Run marching cubes ────────────────────────────────────────────────
	auto mcResult = MarchingCubes::Polygonize(field, mIsovalue);
	//MarchingCubes::ComputeSmoothNormals(mcResult);
	auto& mcVerts = mcResult.Vertices;
	auto& mcIndices = mcResult.Indices;

	if (mcVerts.empty())
	{
		OutputDebugStringA("BuildMarchingCubesMesh: MC produced no geometry\n");
		return;
	}

	// Safety cap: if somehow the mesh is still too large, shrink it rather than crash.
	// 2M vertices × 44 bytes = 88 MB — comfortable on any modern GPU.
	const size_t kMaxVertices = 2'000'000;
	if (mcVerts.size() > kMaxVertices)
	{
		char warn[128];
		sprintf_s(warn, "BuildMarchingCubesMesh: WARNING — capping %zu verts to %zu\n",
			mcVerts.size(), kMaxVertices);
		OutputDebugStringA(warn);
		mcVerts.resize(kMaxVertices);
		// Re-align indices to the capped vertex buffer
		size_t maxIdx = kMaxVertices;
		size_t triEnd = 0;
		for (size_t i = 0; i + 2 < mcIndices.size(); i += 3)
			if (mcIndices[i] < maxIdx && mcIndices[i + 1] < maxIdx && mcIndices[i + 2] < maxIdx)
				triEnd = i + 3;
		mcIndices.resize(triEnd);
	}

	char dbgMsg[128];
	sprintf_s(dbgMsg, "MC mesh: %zu vertices, %zu triangles\n",
		mcVerts.size(), mcIndices.size() / 3);
	OutputDebugStringA(dbgMsg);


	// ── Upload to a dedicated MeshGeometry ───────────────────────────────

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "mcGeo";

	const UINT vbBytes = (UINT)(mcVerts.size() * sizeof(Vertex));
	const UINT ibBytes = (UINT)(mcIndices.size() * sizeof(uint32_t));

	ThrowIfFailed(D3DCreateBlob(vbBytes, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), mcVerts.data(), vbBytes);

	ThrowIfFailed(D3DCreateBlob(ibBytes, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), mcIndices.data(), ibBytes);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
		md3dDevice.Get(), mCommandList.Get(),
		mcVerts.data(), vbBytes, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
		md3dDevice.Get(), mCommandList.Get(),
		mcIndices.data(), ibBytes, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbBytes;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibBytes;

	SubmeshGeometry plateau;
	plateau.IndexCount = (UINT)mcIndices.size();
	plateau.StartIndexLocation = 0;
	plateau.BaseVertexLocation = 0;
	geo->DrawArgs["plateau"] = plateau;

	mGeometries["mcGeo"] = std::move(geo);
}

void TexColumnsApp::CreateMaterial(std::string _name, int _CBIndex, int _SRVDiffIndex, int _SRVNMapIndex, int _SRVDispIndex, XMFLOAT4 _DiffuseAlbedo, XMFLOAT3 _FresnelR0, float _Roughness)
{

	auto material = std::make_unique<Material>();
	material->Name = _name;
	material->MatCBIndex = _CBIndex;
	material->DiffuseSrvHeapIndex = _SRVDiffIndex;
	material->NormalSrvHeapIndex = _SRVNMapIndex;
	if (_SRVDispIndex>=0) material->DisplacementSrvHeapIndex = _SRVDispIndex;
	material->DiffuseAlbedo = _DiffuseAlbedo;
	material->FresnelR0 = _FresnelR0;
	material->Roughness = _Roughness;
	mMaterials[_name] = std::move(material);
}

void TexColumnsApp::BuildMaterials()
{
	CreateMaterial("stone0", 0, TexOffsets["stoneTex"], TexOffsets["stoneNorm"], TexOffsets["stonetDisp"], XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f);
	CreateMaterial("terrain", 0, TexOffsets["terrainDiff"], TexOffsets["terrainNorm"], TexOffsets["terrainDisp"], XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT3(0.05f, 0.05f, 0.05f), 0.3f);

	CreateMaterial("mc_plateau",0, TexOffsets["rock_diff"], TexOffsets["rock_norm"], TexOffsets["default_height"], XMFLOAT4(0.60f, 0.55f, 0.45f, 1.0f), XMFLOAT3(0.04f, 0.04f, 0.04f), 0.8f);
}


void TexColumnsApp::BuildRenderItems()
{
	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	gridRitem->ObjCBIndex = 0;
	gridRitem->Mat = mMaterials["stone0"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));

	// All the render items are opaque.
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());

	//Terrain tiles
	std::vector<std::shared_ptr<Tile>>& allTiles = mTerrain->GetAllTiles();

	for (auto& tile : allTiles)
	{
		auto renderItem = std::make_unique<RenderItem>();
		renderItem->World = MathHelper::Identity4x4();

		renderItem->TexTransform = MathHelper::Identity4x4();
		renderItem->ObjCBIndex = static_cast<int>(mAllRitems.size());
		renderItem->Mat = mMaterials["terrain"].get();
		renderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		int lodIndex = tile->lodLevel;
		std::string lodName = "tile_" + std::to_string(tile->tileIndex) + "_LOD_" + std::to_string(lodIndex);
		renderItem->Geo = mGeometries["terrainGeo"].get();
		renderItem->IndexCount = renderItem->Geo->DrawArgs[lodName].IndexCount;
		renderItem->StartIndexLocation = renderItem->Geo->DrawArgs[lodName].StartIndexLocation;
		renderItem->BaseVertexLocation = renderItem->Geo->DrawArgs[lodName].BaseVertexLocation;

		tile->renderItemIndex = static_cast<int>(mAllRitems.size());
		mAllRitems.push_back(std::move(renderItem));
	}

	RenderCustomMesh("Guard", "Guard", "", XMMatrixScaling(3, 3, 3), XMMatrixRotationRollPitchYaw(3.14, 0, 3.14), XMMatrixTranslation(100, 160, 100));
	RenderCustomMesh("Guard2", "Guard", "", XMMatrixScaling(3, 3, 3), XMMatrixRotationRollPitchYaw(3.14, 0, 3.14), XMMatrixTranslation(50, 160, 100));
	RenderCustomMesh("maxwell", "maxwell", "", XMMatrixScaling(1., 1., 1.), XMMatrixRotationRollPitchYaw(3.14, 0, 3.14), XMMatrixTranslation(75, 100, 110));

	CreateMCRenderItem();
}
void TexColumnsApp::UpdateMCRenderItem()
{
	// Ищем существующий MC рендер айтем
	for (auto& item : mAllRitems)
	{
		if (item && item->Name == "mc_plateau")
		{
			// Обновляем ссылку на новую геометрию
			item->Geo = mGeometries["mcGeo"].get();
			item->IndexCount = mGeometries["mcGeo"]->DrawArgs["plateau"].IndexCount;
			item->NumFramesDirty = gNumFrameResources;

			OutputDebugStringA("MC render item updated to use new geometry\n");
			return;
		}
	}

	// Если не нашли - создаем новый
	CreateMCRenderItem();
}

void TexColumnsApp::CreateMCRenderItem()
{
	auto mcRitem = std::make_unique<RenderItem>();
	mcRitem->Name = "mc_plateau";
	mcRitem->ObjCBIndex = (UINT)mAllRitems.size();

	XMMATRIX mcWorld = XMMatrixTranslation(marchingPos.x, marchingPos.y, marchingPos.z);
	XMStoreFloat4x4(&mcRitem->World, mcWorld);
	XMStoreFloat4x4(&mcRitem->PrevWorld, mcWorld);
	XMStoreFloat4x4(&mcRitem->TexTransform, XMMatrixIdentity());

	mcRitem->Geo = mGeometries["mcGeo"].get();
	mcRitem->Mat = mMaterials["mc_plateau"].get();
	mcRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mcRitem->IndexCount = mGeometries["mcGeo"]->DrawArgs["plateau"].IndexCount;
	mcRitem->StartIndexLocation = 0;
	mcRitem->BaseVertexLocation = 0;

	mAllRitems.push_back(std::move(mcRitem));

	// Обновляем список MC ритемов
	mMCRitems.clear();
	for (auto& item : mAllRitems)
	{
		if (item && item->Name == "mc_plateau")
		{
			mMCRitems.push_back(item.get());
		}
	}

	OutputDebugStringA("MC render item created\n");
}

void TexColumnsApp::RenderCustomMesh(std::string unique_name, std::string meshname, std::string materialName, XMMATRIX Scale, XMMATRIX Rotation, XMMATRIX Translation)
{
	for (int i = 0; i < ObjectsMeshCount[meshname]; i++)
	{
		auto rItem = std::make_unique<RenderItem>();
		std::string textureFile;
		rItem->Name = unique_name;
		XMStoreFloat4x4(&rItem->TexTransform, XMMatrixScaling(1, 1., 1.));
		XMStoreFloat4x4(&rItem->World, Scale * Rotation * Translation);
		rItem->ObjCBIndex = mAllRitems.size();
		rItem->Geo = mGeometries["shapeGeo"].get();
		rItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		std::string matname = rItem->Geo->MultiDrawArgs[meshname][i].first.matName;
		std::cout << " mat : " << matname << "\n";
		std::cout << unique_name << " " << matname << "\n";
		if (materialName != "") matname = materialName;
		rItem->Mat = mMaterials[matname].get();
		rItem->IndexCount = rItem->Geo->MultiDrawArgs[meshname][i].second.IndexCount;
		rItem->StartIndexLocation = rItem->Geo->MultiDrawArgs[meshname][i].second.StartIndexLocation;
		rItem->BaseVertexLocation = rItem->Geo->MultiDrawArgs[meshname][i].second.BaseVertexLocation;
		mAllRitems.push_back(std::move(rItem));
		mStandCustomMeshes.push_back(mAllRitems[mAllRitems.size() - 1].get());
		//mOpaqueRitems.push_back(mAllRitems[mAllRitems.size() - 1].get());
	}
	BuildFrameResources();
}


void TexColumnsApp::DrawCustomMeshes(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& customMeshes)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();
	auto passCB = mCurrFrameResource->PassCB->Resource();

	// For each render item...
	for (size_t i = 0; i < customMeshes.size(); ++i)
	{
		auto ri = customMeshes[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		// �������� ������� ���������� ����
		CD3DX12_GPU_DESCRIPTOR_HANDLE baseHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

		// ������������� ������������� ������� ��� �������
		CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseHandle = baseHandle;
		diffuseHandle.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(0, diffuseHandle);  // t0 - diffuse

		CD3DX12_GPU_DESCRIPTOR_HANDLE normalHandle = baseHandle;
		normalHandle.Offset(ri->Mat->NormalSrvHeapIndex, mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(1, normalHandle);   // t1 - normal

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(2, objCBAddress);  // b0 - per object
		cmdList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());
		cmdList->SetGraphicsRootConstantBufferView(4, matCBAddress);  // b1 - per material

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void TexColumnsApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();
	auto passCB = mCurrFrameResource->PassCB->Resource();




	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		// �������� ������� ���������� ����
		CD3DX12_GPU_DESCRIPTOR_HANDLE baseHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

		// ������������� ������������� ������� ��� �������
		CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseHandle = baseHandle;
		diffuseHandle.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(0, diffuseHandle);  // t0 - diffuse

		CD3DX12_GPU_DESCRIPTOR_HANDLE normalHandle = baseHandle;
		normalHandle.Offset(ri->Mat->NormalSrvHeapIndex, mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(1, normalHandle);   // t1 - normal

		CD3DX12_GPU_DESCRIPTOR_HANDLE dispHandle = baseHandle;
		dispHandle.Offset(ri->Mat->DisplacementSrvHeapIndex, mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(2, dispHandle);     // t2 - displacement

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(3, objCBAddress);  // b0 - per object
		// Slot 4 = b1 = cbPass — must be set before any draw
		cmdList->SetGraphicsRootConstantBufferView(4, passCB->GetGPUVirtualAddress());
		cmdList->SetGraphicsRootConstantBufferView(5, matCBAddress);  // b1 - per material

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void TexColumnsApp::DrawTilesRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<Tile*>& tiles)
{

	char debugMsg[256];

	sprintf_s(debugMsg, "TexOffsets: diff=%d, norm=%d, disp=%d\n",
		TexOffsets["terrainDiff"],
		TexOffsets["terrainNorm"],
		TexOffsets["terrainDisp"]);
	OutputDebugStringA(debugMsg);

	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));
	UINT terrCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(TileConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();


	for (auto& tile : tiles)
	{
		auto ri = mAllRitems[tile->renderItemIndex].get();
		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE baseHandle(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

		// t0 - Terrain diffuse
		CD3DX12_GPU_DESCRIPTOR_HANDLE terrainDiffHandle = baseHandle;
		terrainDiffHandle.Offset(TexOffsets["terrainDiff"], mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(0, terrainDiffHandle);

		// t1 - Terrain normal
		CD3DX12_GPU_DESCRIPTOR_HANDLE terrainNormHandle = baseHandle;
		terrainNormHandle.Offset(TexOffsets["terrainNorm"], mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(1, terrainNormHandle);

		// t2 - Terrain displacement
		CD3DX12_GPU_DESCRIPTOR_HANDLE terrainDispHandle = baseHandle;
		terrainDispHandle.Offset(TexOffsets["terrainDisp"], mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(2, terrainDispHandle);

		// === t3 - Brush texture (НОВОЕ) ===
		CD3DX12_GPU_DESCRIPTOR_HANDLE brushTextureHandle = baseHandle;
		brushTextureHandle.Offset(mBrushTextureSRVIndex, mCbvSrvDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(3, brushTextureHandle);

		// CBV b0 (корневой индекс 4) - cbPerObject
		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(4, objCBAddress);

		// CBV b1 (корневой индекс 5) - cbPass - устанавливается в Draw()
		// CBV b2 (корневой индекс 6) - cbMaterial
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(6, matCBAddress);

		// CBV b3 (корневой индекс 7) - cbTerrainTile
		auto terrCB = mCurrFrameResource->TerrainCB->Resource();
		cmdList->SetGraphicsRootConstantBufferView(7, terrCB->GetGPUVirtualAddress() + tile->tileIndex * terrCBByteSize);

		// CBV b4 (корневой индекс 8) - cbBrush
		auto brushCB = mCurrFrameResource->BrushCB->Resource();
		cmdList->SetGraphicsRootConstantBufferView(8, brushCB->GetGPUVirtualAddress());

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}


void TexColumnsApp::Draw(const GameTimer& gt)
{
	static int frameCount = 0;
	frameCount++;

	char debugMsg[256];

	try
	{
		auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
		HRESULT hr = cmdListAlloc->Reset();
		if (FAILED(hr)) ThrowIfFailed(hr);

		if (isFillModeSolid)
			hr = mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get());
		else
			hr = mCommandList->Reset(cmdListAlloc.Get(), mPSOs["wireframe"].Get());
		if (FAILED(hr)) ThrowIfFailed(hr);

		mCommandList->RSSetViewports(1, &mScreenViewport);
		mCommandList->RSSetScissorRects(1, &mScissorRect);

		// ============ STEP 1: SCENE -> COLOR + VELOCITY ============
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			mTAAColorBuffer->GetResource(),
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));

		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			mTAVelocityBuffer->GetResource(),
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));

		CD3DX12_CPU_DESCRIPTOR_HANDLE colorRTV(
			mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
			mTaaColorBufferRTVIndex, mRtvDescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE velocityRTV(
			mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
			mVelocityBufferRTVIndex, mRtvDescriptorSize);
		CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(DepthStencilView());


		D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { colorRTV, velocityRTV };
		mCommandList->OMSetRenderTargets(2, rtvs, false, &dsv);
		mCommandList->ClearRenderTargetView(colorRTV, Colors::Black, 0, nullptr);
		float clearVel[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		mCommandList->ClearRenderTargetView(velocityRTV, clearVel, 0, nullptr);

		mCommandList->ClearDepthStencilView(dsv,
			D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);


		// ===== SKY PASS (before geometry) =====

		// Sky writes to color buffer only — 1 RTV, no velocity, no depth.
		// This must match NumRenderTargets=1 declared in the sky PSO.
		mCommandList->OMSetRenderTargets(1, &colorRTV, false, nullptr);

		mCommandList->SetPipelineState(mPSOs["sky"].Get());

		mCommandList->SetGraphicsRootSignature(mSkyRootSignature.Get());
		mCommandList->SetGraphicsRootConstantBufferView(
			0, mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
		mCommandList->SetGraphicsRootConstantBufferView(
			1, mCurrFrameResource->AtmosphereCB->Resource()->GetGPUVirtualAddress());

		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		mCommandList->DrawInstanced(3, 1, 0, 0);

		// Restore both RTVs + DSV for geometry passes that follow.
		D3D12_CPU_DESCRIPTOR_HANDLE rtvsBoth[2] = { colorRTV, velocityRTV };
		mCommandList->OMSetRenderTargets(2, rtvsBoth, false, &dsv);

		// ===== END SKY PASS =====

		ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
		mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		// Restore correct PSO before opaque geometry
		if (isFillModeSolid)
			mCommandList->SetPipelineState(mPSOs["opaque"].Get());
		else
			mCommandList->SetPipelineState(mPSOs["wireframe"].Get());

		// Opaque items — descriptor heap is already set, no need to call it again
		mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
		DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

		
		// Custom meshes (MeshStandard.hlsl writes real velocity)
		mCommandList->SetGraphicsRootSignature(mStandMeshRootSignature.Get());

		if (isFillModeSolid)
			mCommandList->SetPipelineState(mPSOs["standardMesh"].Get());
		else
			mCommandList->SetPipelineState(mPSOs["wireStandardMesh"].Get());
		if (!mStandCustomMeshes.empty())
			DrawCustomMeshes(mCommandList.Get(), mStandCustomMeshes);

		if (mRegenerateMarching)
		{
			BuildMarchingCubesMesh();
			UpdateMCRenderItem();

			mRegenerateMarching = false;
		}

		// ── MC plateau (triplanar shader, same root sig as Meshes) ──────
		if (!mMCRitems.empty())
		{
			if (isFillModeSolid)
				mCommandList->SetPipelineState(mPSOs["mc"].Get());
			else
				mCommandList->SetPipelineState(mPSOs["mcWire"].Get());

			// Root signature is already set to mStandMeshRootSignature from
			// the meshes pass above — no need to set it again.
			DrawCustomMeshes(mCommandList.Get(), mMCRitems);
		}

		// Terrain
		auto passCB = mCurrFrameResource->PassCB->Resource();
		mVisibleTiles.clear();
		mVisibleTiles = mTerrain->GetVisibleTiles();
		if (!mVisibleTiles.empty())
		{
			if (isFillModeSolid)
				mCommandList->SetPipelineState(mPSOs["terrain"].Get());
			else
				mCommandList->SetPipelineState(mPSOs["wireTerrain"].Get());
			mCommandList->SetGraphicsRootSignature(mTerrainRootSignature.Get());
			mCommandList->SetGraphicsRootConstantBufferView(5, passCB->GetGPUVirtualAddress());
			DrawTilesRenderItems(mCommandList.Get(), mVisibleTiles);
		}



		// ---- Frame 1 bootstrap: seed both history buffers with first frame ----
		// After this block, Prev and Current are back in COMMON.
		if (frameCount == 1)
		{
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAAColorBuffer->GetResource(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE));

			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAAPrevTexture->GetResource(),
				D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAACurrentTexture->GetResource(),
				D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));

			mCommandList->CopyResource(mTAAPrevTexture->GetResource(), mTAAColorBuffer->GetResource());
			mCommandList->CopyResource(mTAACurrentTexture->GetResource(), mTAAColorBuffer->GetResource());

			// Return EVERYTHING to COMMON so the TAA pass below can start fresh
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAAPrevTexture->GetResource(),
				D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON));
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAACurrentTexture->GetResource(),
				D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON));
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAAColorBuffer->GetResource(),
				D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
		}

		// ============ STEP 2: TAA RESOLVE PASS ============
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			mTAAColorBuffer->GetResource(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			mTAVelocityBuffer->GetResource(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

		CD3DX12_RESOURCE_BARRIER backBufferBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
			CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		mCommandList->ResourceBarrier(1, &backBufferBarrier);

		mCommandList->SetPipelineState(mPSOs["TAA"].Get());
		mCommandList->SetGraphicsRootSignature(mTAARootSignature.Get());

		// Ping-pong. frameIndex has NOT been incremented yet.
		// Both textures are guaranteed to be in COMMON here (seeded above on frame 1,
		// returned to COMMON at end-of-frame on every subsequent frame).
		UINT historySRVIndex;
		UINT historyRTVIndex;

		if (frameIndex % 2 == 0)
		{
			// Read Prev (SRV), write Current (RTV)
			historySRVIndex = mPrevTextureSRVIndex;
			historyRTVIndex = mCurrentTextureRTVIndex;
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAAPrevTexture->GetResource(),
				D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAACurrentTexture->GetResource(),
				D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));
		}
		else
		{
			// Read Current (SRV), write Prev (RTV)
			historySRVIndex = mCurrentTextureSRVIndex;
			historyRTVIndex = mPrevTextureRTVIndex;
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAACurrentTexture->GetResource(),
				D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAAPrevTexture->GetResource(),
				D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET));
		}

		// Bind TAA inputs
		mCommandList->SetGraphicsRootDescriptorTable(0, CD3DX12_GPU_DESCRIPTOR_HANDLE(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			mTaaColorBufferSRVIndex, mCbvSrvDescriptorSize));
		mCommandList->SetGraphicsRootDescriptorTable(1, CD3DX12_GPU_DESCRIPTOR_HANDLE(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			historySRVIndex, mCbvSrvDescriptorSize));
		mCommandList->SetGraphicsRootDescriptorTable(2, CD3DX12_GPU_DESCRIPTOR_HANDLE(
			mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			mVelocityBufferSRVIndex, mCbvSrvDescriptorSize));
		mCommandList->SetGraphicsRootConstantBufferView(3,
			mCurrFrameResource->TAACB->Resource()->GetGPUVirtualAddress());

		// Bind RTVs: backbuffer (Target0) + history write (Target1)
		rtvs[0] = CurrentBackBufferView();
		rtvs[1] = CD3DX12_CPU_DESCRIPTOR_HANDLE(
			mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
			historyRTVIndex, mRtvDescriptorSize);
		mCommandList->OMSetRenderTargets(2, rtvs, false, nullptr);
		mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::Black, 0, nullptr);

		// Fullscreen triangle - no vertex buffer, TAA VS uses SV_VertexID
		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		mCommandList->DrawInstanced(3, 1, 0, 0);

		// ============ COMPUTE SHADER (brush painting) ============
		if (mIsPainting)
		{
			mCommandList->SetPipelineState(mPSOs["brushCompute"].Get());
			mCommandList->SetComputeRootSignature(mBrushComputeRootSignature.Get());

			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mBrushTexture.Get(),
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

			mCommandList->SetComputeRootConstantBufferView(0,
				mCurrFrameResource->BrushCB->Resource()->GetGPUVirtualAddress());

			if (!mVisibleTiles.empty())
			{
				auto tile = mVisibleTiles[0];
				UINT sz = d3dUtil::CalcConstantBufferByteSize(sizeof(TileConstants));
				mCommandList->SetComputeRootConstantBufferView(1,
					mCurrFrameResource->TerrainCB->Resource()->GetGPUVirtualAddress()
					+ tile->tileIndex * sz);
			}
			mCommandList->SetComputeRootDescriptorTable(2, CD3DX12_GPU_DESCRIPTOR_HANDLE(
				mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
				TexOffsets["terrainDisp"], mCbvSrvDescriptorSize));
			mCommandList->SetComputeRootDescriptorTable(3, CD3DX12_GPU_DESCRIPTOR_HANDLE(
				mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
				mBrushTextureUAVIndex, mCbvSrvDescriptorSize));

			mCommandList->Dispatch(
				(UINT)ceil(mBrushTextureWidth / 16.0f),
				(UINT)ceil(mBrushTextureHeight / 16.0f), 1);

			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mBrushTexture.Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
		}

		// ============ IMGUI ============
		if (mEnableImGui)
		{
			ID3D12DescriptorHeap* imguiHeaps[] = { mImGuiSrvDescriptorHeap.Get() };
			mCommandList->SetDescriptorHeaps(_countof(imguiHeaps), imguiHeaps);
			ImGui::Render();
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());
		}

		// ============ END-OF-FRAME: return all TAA resources to COMMON ============
		// Use the SAME frameIndex value as the ping-pong above (not yet incremented).
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			mTAAColorBuffer->GetResource(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			mTAVelocityBuffer->GetResource(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));

		if (frameIndex % 2 == 0)
		{
			// Prev was SRV, Current was RTV
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAAPrevTexture->GetResource(),
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAACurrentTexture->GetResource(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON));
		}
		else
		{
			// Current was SRV, Prev was RTV
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAACurrentTexture->GetResource(),
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
				mTAAPrevTexture->GetResource(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON));
		}

		// Increment AFTER all barriers that branch on it
		frameIndex = (frameIndex + 1) % 16;

		backBufferBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
			CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		mCommandList->ResourceBarrier(1, &backBufferBarrier);

		hr = mCommandList->Close();
		if (FAILED(hr)) ThrowIfFailed(hr);

		ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

		hr = mSwapChain->Present(0, 0);
		if (FAILED(hr)) ThrowIfFailed(hr);

		mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
		mCurrFrameResource->Fence = ++mCurrentFence;
		hr = mCommandQueue->Signal(mFence.Get(), mCurrentFence);
		if (FAILED(hr)) ThrowIfFailed(hr);
	}
	catch (...)
	{
		throw;
	}
}



void TexColumnsApp::GenerateTransformedHaltonSequence(float viewSizeX, float viewSizeY, XMFLOAT2* outJitters)
{
	const XMFLOAT2 HaltonPoints[16] =
	{
		XMFLOAT2(0.500000f, 0.333333f),
		XMFLOAT2(0.250000f, 0.666667f),
		XMFLOAT2(0.750000f, 0.111111f),
		XMFLOAT2(0.125000f, 0.444444f),
		XMFLOAT2(0.625000f, 0.777778f),
		XMFLOAT2(0.375000f, 0.222222f),
		XMFLOAT2(0.875000f, 0.555556f),
		XMFLOAT2(0.062500f, 0.888889f),
		XMFLOAT2(0.562500f, 0.037037f),
		XMFLOAT2(0.312500f, 0.370370f),
		XMFLOAT2(0.812500f, 0.703704f),
		XMFLOAT2(0.187500f, 0.148148f),
		XMFLOAT2(0.687500f, 0.481481f),
		XMFLOAT2(0.437500f, 0.814815f),
		XMFLOAT2(0.937500f, 0.259259f),
		XMFLOAT2(0.031250f, 0.592593f)
	};

	for (int i = 0; i < 16; ++i)
	{
		outJitters[i].x = ((HaltonPoints[i].x - 0.5f) / viewSizeX) * 2.0f;
		outJitters[i].y = ((HaltonPoints[i].y - 0.5f) / viewSizeY) * 2.0f;
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> TexColumnsApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

