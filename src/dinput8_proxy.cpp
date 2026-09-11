#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#ifndef XR_NO_PROTOTYPES
#define XR_NO_PROTOTYPES
#endif

#include <Windows.h>
#include <dinput.h>
#include <Xinput.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <dxgi.h>
#include <wincodec.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include "p12_projection.h"
#include "p13_ui_classifier.h"
#include "p14_shader_registry.h"
#include "p15_movement.h"
#include "p15_pixel_camera.h"
#include "p17_camera_fields.h"
#include "p20_ik.h"
#include "p41_physical.h"
#include "p26_grip.h"
#include "p22_roomscale.h"
#include "p31_capture_math.h"
#include "p31_activation.h"
#include "p31_spawn.h"
#include "p31_lcd_compositor.h"
#include "p32_cleanup.h"
#include "p46_screen.h"
#include "p36_visibility.h"

namespace {

constexpr const char* kBuildId = "OUTLAST2VR-NATIVE-VIEW-BED-CULL-PF17-20260910";
constexpr float kMouseCountsPerRadian = 920.0f;
constexpr float kMaxAcceptedHeadDeltaRadians = 0.35f;

// OpenXR runtimes may return UNORM, UNORM_SRGB, or TYPELESS for the same
// 32-bit color family even when the requested swapchain format is identical.
// D3D11 permits copies between compatible members of that family, while an
// exact enum comparison silently produced zero XR layers on VDXR.
static int P13ColorFormatFamily(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return 1;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return 2;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_UNORM: return 3;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM: return 4;
        default: return 0;
    }
}

static bool P13CompatibleColorFormat(DXGI_FORMAT source, DXGI_FORMAT destination) {
    return source == destination ||
        (P13ColorFormatFamily(source) != 0 &&
         P13ColorFormatFamily(source) == P13ColorFormatFamily(destination));
}

HMODULE g_realDInput = nullptr;
std::once_flag g_realOnce;
std::mutex g_logMutex;
std::atomic<bool> g_getProcHookInstalled{false};
std::atomic<bool> g_dxgiInterceptSeen{false};

using GetProcAddressFn = FARPROC (WINAPI*)(HMODULE, LPCSTR);
GetProcAddressFn g_realGetProcAddress = nullptr;

using CreateDXGIFactoryFn = HRESULT (WINAPI*)(REFIID, void**);
CreateDXGIFactoryFn g_realCreateDXGIFactory = nullptr;
CreateDXGIFactoryFn g_realCreateDXGIFactory1 = nullptr;

// P7A: Present is too late to discover UE3's scene depth. Track the game's
// full-resolution depth-stencil view when ID3D11Device::CreateDepthStencilView
// creates it, then keep a reference so the compositor can read it at Present.
using CreateDepthStencilViewFn = HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*, ID3D11Resource*, const D3D11_DEPTH_STENCIL_VIEW_DESC*, ID3D11DepthStencilView**);
CreateDepthStencilViewFn g_realCreateDepthStencilView = nullptr;
std::atomic<bool> g_depthDeviceHookInstalled{false};
std::atomic<UINT> g_expectedDepthWidth{0};
std::atomic<UINT> g_expectedDepthHeight{0};
std::mutex g_earlyDepthMutex;
ID3D11DepthStencilView* g_earlyFullResDsv = nullptr;
DXGI_FORMAT g_earlyDepthViewFormat = DXGI_FORMAT_UNKNOWN;
std::atomic<bool> g_earlyDepthLogged{false};
std::atomic<bool> g_p11cPipelineRebuildRequested{false};
std::atomic<uint64_t> g_p11cDepthGeneration{0};


void Log(const char* fmt, ...);
std::wstring ModuleDir(); // forward declaration: used by the early P10 GPU shader setup

// P11E: targeted UE3 main-view frustum culling bypass.
//
// Static analysis of the user's exact Outlast2.exe found two sibling primitive-
// visibility loops in the renderer at RVAs 0x8F86xx and 0x8F88xx.  Both test a
// primitive bounding sphere against View.ViewFrustum (+0x650), optionally test
// its AABB, and branch to the primitive-rejected path when either intersection
// returns false.  P11D tried to make the CPU culling camera follow the HMD, but
// that game's AOLHero virtual is not the final visible camera and it killed the
// working renderer-level tracking.  P11E therefore goes back to P11C's proven
// HMD renderer transform. PF14 extends the original four frustum branches
// with two maximum-distance and two view-state result branches, verified in
// the same primitive loops. PF16A also covers the parent scene-octree reject
// at 0x8F91CE. Without that ninth site an entire node could be discarded before
// any of the already-patched per-primitive tests were reached. The minimum-
// distance branches remain intact.
//
// We deliberately do NOT patch FConvexVolume::IntersectBox/Sphere globally;
// those helpers are also used by lights/shadows.  Every byte is validated against
// the exact shipping executable before anything is changed.  If even one site
// differs, the bypass is skipped instead of guessing.
struct P11ECullPatchSite {
    uintptr_t rva;
    std::vector<uint8_t> expected;
    std::vector<uint8_t> original{};
    bool applied = false;
    const char* label;
};

std::array<P11ECullPatchSite, 9> g_p11eCullSites{{
    {0x008F8702u, {0x0F,0x84,0xA8,0x00,0x00,0x00}, {}, false, "visibility-A sphere reject"},
    {0x008F8725u, {0x0F,0x84,0x85,0x00,0x00,0x00}, {}, false, "visibility-A box reject"},
    {0x008F8A6Bu, {0x0F,0x84,0xAE,0x00,0x00,0x00}, {}, false, "visibility-B sphere reject"},
    {0x008F8A91u, {0x0F,0x84,0x88,0x00,0x00,0x00}, {}, false, "visibility-B box reject"},
    // Renderer-side squared maximum-distance rejects. Preserve the following
    // minimum-distance tests, which can select near/far representations.
    {0x008F8690u, {0x77,0x0D}, {}, false, "visibility-A maximum distance reject"},
    {0x008F896Eu, {0x77,0x0D}, {}, false, "visibility-B maximum distance reject"},
    // Result of the view-state visibility/history function at 0x90D0B0,
    // including its precomputed visibility bitset path. Keep the function call
    // and output bookkeeping; prevent its stale-camera rejection of geometry.
    {0x008F8773u, {0x75,0x3B}, {}, false, "visibility-A cached visibility reject"},
    {0x008F8AE7u, {0x75,0x36}, {}, false, "visibility-B cached visibility reject"},
    // The renderer first tests each parent octree node against all active
    // views. If every test misses, this short jump skips adding that node and
    // none of its primitives can reach the eight checks above. Fall through
    // to the existing add-node path instead; the final GPU depth test remains.
    {0x008F91CEu, {0xEB,0x16}, {}, false, "scene-octree parent reject"},
}};
std::atomic<bool> g_p11eCullBypassInstalled{false};

static bool P11EValidateExeImage(unsigned char*& baseOut, size_t& imageSizeOut) {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!base) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
    baseOut = base;
    imageSizeOut = nt->OptionalHeader.SizeOfImage;
    return true;
}

static void P11ERestoreFrustumCullBypass() {
    if (!g_p11eCullBypassInstalled.exchange(false, std::memory_order_acq_rel)) return;
    unsigned char* base = nullptr;
    size_t imageSize = 0;
    if (!P11EValidateExeImage(base, imageSize)) return;
    for (auto& site : g_p11eCullSites) {
        if (!site.applied || site.rva + site.original.size() > imageSize) continue;
        auto* at = base + site.rva;
        DWORD oldProtect = 0;
        if (VirtualProtect(at, site.original.size(), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memcpy(at, site.original.data(), site.original.size());
            FlushInstructionCache(GetCurrentProcess(), at, site.original.size());
            DWORD ignored = 0;
            VirtualProtect(at, site.original.size(), oldProtect, &ignored);
        }
        site.applied = false;
    }
}

static bool P11EInstallFrustumCullBypass() {
    if (g_p11eCullBypassInstalled.load(std::memory_order_acquire)) return true;
    unsigned char* base = nullptr;
    size_t imageSize = 0;
    if (!P11EValidateExeImage(base, imageSize)) {
        Log("P11E FRUSTUM CULLING BYPASS PATCH=skipped: Outlast2.exe PE validation failed.");
        return false;
    }

    // Validate all sites before touching any code so a different executable can
    // never receive a partial/guessed patch.
    for (auto& site : g_p11eCullSites) {
        if (site.rva + site.expected.size() > imageSize) {
            Log("P11E FRUSTUM CULLING BYPASS PATCH=skipped: %s RVA outside image.", site.label);
            return false;
        }
        auto* at = base + site.rva;
        if (std::memcmp(at, site.expected.data(), site.expected.size()) != 0) {
            Log("P11E FRUSTUM CULLING BYPASS PATCH=skipped: byte signature mismatch at %s RVA=0x%llX.",
                site.label, static_cast<unsigned long long>(site.rva));
            return false;
        }
        site.original.assign(at, at + site.expected.size());
    }

    for (auto& site : g_p11eCullSites) {
        const std::vector<uint8_t> kNops(site.expected.size(), 0x90);
        auto* at = base + site.rva;
        DWORD oldProtect = 0;
        if (!VirtualProtect(at, kNops.size(), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            Log("P11E FRUSTUM CULLING BYPASS PATCH=failed: VirtualProtect at %s.", site.label);
            // Restore any earlier site in this batch.
            for (auto& rollback : g_p11eCullSites) {
                if (!rollback.applied) continue;
                auto* r = base + rollback.rva;
                DWORD rold = 0;
                if (VirtualProtect(r, rollback.original.size(), PAGE_EXECUTE_READWRITE, &rold)) {
                    std::memcpy(r, rollback.original.data(), rollback.original.size());
                    FlushInstructionCache(GetCurrentProcess(), r, rollback.original.size());
                    DWORD ignored = 0;
                    VirtualProtect(r, rollback.original.size(), rold, &ignored);
                }
                rollback.applied = false;
            }
            return false;
        }
        std::memcpy(at, kNops.data(), kNops.size());
        FlushInstructionCache(GetCurrentProcess(), at, kNops.size());
        DWORD ignored = 0;
        VirtualProtect(at, kNops.size(), oldProtect, &ignored);
        site.applied = true;
    }
    g_p11eCullBypassInstalled.store(true, std::memory_order_release);
    Log("PF16A RENDERER CULLING PATCH=installed: 9 validated sites; parent scene-octree + primitive frustum + renderer maximum distance + cached/precomputed visibility rejection bypassed; minimum distance and global shadow helpers preserved.");
    return true;
}

bool IsDepthFamilyResourceFormat(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_D16_UNORM:
        case DXGI_FORMAT_R16_UNORM:
            return true;
        default:
            return false;
    }
}

HRESULT STDMETHODCALLTYPE HookCreateDepthStencilView(ID3D11Device* self, ID3D11Resource* resource,
                                                       const D3D11_DEPTH_STENCIL_VIEW_DESC* desc,
                                                       ID3D11DepthStencilView** outView) {
    auto original = g_realCreateDepthStencilView;
    if (!original) return E_FAIL;
    HRESULT hr = original(self, resource, desc, outView);
    if (FAILED(hr) || !resource || !outView || !*outView) return hr;

    ID3D11Texture2D* tex = nullptr;
    if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex))) && tex) {
        D3D11_TEXTURE2D_DESC td{};
        tex->GetDesc(&td);
        tex->Release();
        const UINT expectedW = g_expectedDepthWidth.load(std::memory_order_relaxed);
        const UINT expectedH = g_expectedDepthHeight.load(std::memory_order_relaxed);
        const bool fullRes = expectedW && expectedH && td.Width == expectedW && td.Height == expectedH;
        if (fullRes && IsDepthFamilyResourceFormat(td.Format) && td.SampleDesc.Count == 1) {
            D3D11_DEPTH_STENCIL_VIEW_DESC vd{};
            (*outView)->GetDesc(&vd);
            bool replacedResource = false;
            {
                std::lock_guard<std::mutex> lock(g_earlyDepthMutex);
                if (!g_earlyFullResDsv) {
                    (*outView)->AddRef();
                    g_earlyFullResDsv = *outView;
                    g_earlyDepthViewFormat = vd.Format;
                    g_p11cDepthGeneration.fetch_add(1, std::memory_order_relaxed);
                    if (!g_earlyDepthLogged.exchange(true)) {
                        Log("P7A EARLY FULL-RES DEPTH DSV CAPTURED: %ux%u resourceFmt=%u viewFmt=%u.",
                            td.Width, td.Height, static_cast<unsigned>(td.Format), static_cast<unsigned>(vd.Format));
                    }
                } else {
                    ID3D11Resource* oldRes = nullptr;
                    ID3D11Resource* newRes = nullptr;
                    g_earlyFullResDsv->GetResource(&oldRes);
                    (*outView)->GetResource(&newRes);
                    const bool sameResource = oldRes && newRes && oldRes == newRes;
                    if (oldRes) oldRes->Release();
                    if (newRes) newRes->Release();
                    if (!sameResource) {
                        (*outView)->AddRef();
                        g_earlyFullResDsv->Release();
                        g_earlyFullResDsv = *outView;
                        g_earlyDepthViewFormat = vd.Format;
                        const auto gen = g_p11cDepthGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
                        replacedResource = true;
                        Log("P11E FULL-RES DEPTH RESOURCE RECREATED: generation=%llu %ux%u resourceFmt=%u viewFmt=%u; renderer bindings will be rediscovered.",
                            static_cast<unsigned long long>(gen), td.Width, td.Height, static_cast<unsigned>(td.Format), static_cast<unsigned>(vd.Format));
                    }
                }
            }
            if (replacedResource) g_p11cPipelineRebuildRequested.store(true, std::memory_order_release);
        }
    }
    return hr;
}

bool InstallDepthStencilCreationHook(ID3D11Device* device, UINT expectedW, UINT expectedH) {
    if (!device) return false;
    g_expectedDepthWidth.store(expectedW, std::memory_order_relaxed);
    g_expectedDepthHeight.store(expectedH, std::memory_order_relaxed);
    if (g_depthDeviceHookInstalled.load(std::memory_order_acquire)) return true;

    void*** object = reinterpret_cast<void***>(device);
    if (!object || !*object) return false;
    void** vtable = *object;
    constexpr size_t kCreateDepthStencilViewIndex = 10; // ID3D11Device vtable
    auto original = reinterpret_cast<CreateDepthStencilViewFn>(vtable[kCreateDepthStencilViewIndex]);
    if (!original) return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtable[kCreateDepthStencilViewIndex], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    g_realCreateDepthStencilView = original;
    vtable[kCreateDepthStencilViewIndex] = reinterpret_cast<void*>(&HookCreateDepthStencilView);
    FlushInstructionCache(GetCurrentProcess(), &vtable[kCreateDepthStencilViewIndex], sizeof(void*));
    DWORD ignored = 0;
    VirtualProtect(&vtable[kCreateDepthStencilViewIndex], sizeof(void*), oldProtect, &ignored);
    g_depthDeviceHookInstalled.store(true, std::memory_order_release);
    Log("P7A early ID3D11Device::CreateDepthStencilView hook installed for %ux%u scene depth.", expectedW, expectedH);
    return true;
}


// P9: renderer-level view/projection reconnaissance.
// We intentionally do not alter Outlast 2's matrices in this phase. Instead we
// observe the immediate D3D11 context's vertex-shader constant buffers and log
// stable, per-frame 4x4 matrix candidates. This tells the next phase exactly
// which UE3 buffer/slot/offset owns the world view/projection transform instead
// of guessing and crashing the renderer.
using VSSetConstantBuffersFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*);
using MapFn = HRESULT (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
using UnmapFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
using UpdateSubresourceFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT);
using GetDataFn = HRESULT (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Asynchronous*, void*, UINT, UINT);
using SetPredicationFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Predicate*, BOOL);
using OMSetRenderTargetsFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
using OMSetRenderTargetsUAVFn = void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);

VSSetConstantBuffersFn g_realVSSetConstantBuffers = nullptr;
MapFn g_realContextMap = nullptr;
UnmapFn g_realContextUnmap = nullptr;
UpdateSubresourceFn g_realUpdateSubresource = nullptr;
GetDataFn g_realContextGetData = nullptr;
SetPredicationFn g_realContextSetPredication = nullptr;
OMSetRenderTargetsFn g_realOMSetRenderTargets = nullptr;
OMSetRenderTargetsUAVFn g_realOMSetRenderTargetsUAV = nullptr;
std::atomic<bool> g_matrixProbeHooksInstalled{false};
void P13RepairContextHooks(ID3D11DeviceContext* context);
ID3D11DeviceContext* g_probeGameContext = nullptr; // borrowed; game device owns it for process lifetime
std::atomic<uint64_t> g_probeFrameCounter{0};
std::atomic<uint32_t> g_probeCandidateLogs{0};
constexpr uint32_t kMaxProbeCandidateLogs = 28;

constexpr float kP10ProjectionScale = 0.55f;
std::atomic<ID3D11Buffer*> g_p10ViewProjectionBuffer{nullptr}; // borrowed; retained by CBProbeState
std::atomic<bool> g_p10ViewProjectionValidated{false};
std::atomic<bool> g_p10GameplayActive{false};
std::atomic<uint64_t> g_p10DirectPatchCount{0};
std::atomic<bool> g_p10DirectPatchLogged{false};
std::atomic<uint64_t> g_p36OcclusionResultsForced{0};
std::atomic<bool> g_p36OcclusionFixLogged{false};
std::atomic<uint64_t> g_p36PredicationsSuppressed{0};
std::atomic<bool> g_p36PredicationFixLogged{false};

static bool P36HardwareOcclusionFixEnabled() {
    static const bool enabled = GetPrivateProfileIntW(
        L"VR", L"HardwareOcclusionFix", 1,
        (ModuleDir() + L"\\outlast2_vr_p35.ini").c_str()) != 0;
    return enabled;
}

HRESULT STDMETHODCALLTYPE HookContextGetData(ID3D11DeviceContext* self, ID3D11Asynchronous* async,
                                              void* data, UINT dataSize, UINT flags) {
    const HRESULT result = g_realContextGetData(self, async, data, dataSize, flags);
    if (result != S_OK || !async || !data ||
        !g_p10GameplayActive.load(std::memory_order_acquire) || !P36HardwareOcclusionFixEnabled()) {
        return result;
    }

    ID3D11Query* query = nullptr;
    if (FAILED(async->QueryInterface(__uuidof(ID3D11Query), reinterpret_cast<void**>(&query))) || !query) {
        return result;
    }
    D3D11_QUERY_DESC desc{};
    query->GetDesc(&desc);
    query->Release();
    if (p36visibility::ForceOcclusionVisible(desc.Query, data, dataSize)) {
        const auto forced = g_p36OcclusionResultsForced.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!g_p36OcclusionFixLogged.exchange(true, std::memory_order_acq_rel)) {
            Log("PF10 OCCLUSION: gameplay D3D11 occlusion readback is fail-visible; first hidden result repaired (count=%llu).",
                static_cast<unsigned long long>(forced));
        }
    }
    return result;
}

void STDMETHODCALLTYPE HookContextSetPredication(ID3D11DeviceContext* self, ID3D11Predicate* predicate,
                                                  BOOL predicateValue) {
    D3D11_QUERY_DESC predicateDesc{};
    if (predicate) predicate->GetDesc(&predicateDesc);
    if (predicate && predicateDesc.Query == D3D11_QUERY_OCCLUSION_PREDICATE &&
        g_p10GameplayActive.load(std::memory_order_acquire) &&
        P36HardwareOcclusionFixEnabled()) {
        // UE3 can consume an occlusion query directly on the GPU without ever
        // calling GetData. That predicate was calculated for the native camera
        // before the final VR view transform, so fail visible during gameplay.
        g_realContextSetPredication(self, nullptr, FALSE);
        const auto suppressed = g_p36PredicationsSuppressed.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!g_p36PredicationFixLogged.exchange(true, std::memory_order_acq_rel)) {
            Log("PF11 GPU PREDICATION: first gameplay visibility predicate suppressed (count=%llu); depth testing remains unchanged.",
                static_cast<unsigned long long>(suppressed));
        }
    } else {
        g_realContextSetPredication(self, predicate, predicateValue);
    }
    P13RepairContextHooks(self);
}


// P11: renderer-native HMD rotation.  These coefficients describe the current
// headset orientation relative to the orientation at gameplay entry.  They are
// expressed in camera-local Right/Up/Forward coordinates and are consumed by
// the same GPU patch that already owns Outlast 2's translated-world VP buffer.
// The game camera is no longer driven through SendInput in P11.
std::atomic<float> g_p11RightX{1.0f}, g_p11RightY{0.0f}, g_p11RightZ{0.0f};
std::atomic<float> g_p11UpX{0.0f},    g_p11UpY{1.0f},    g_p11UpZ{0.0f};
std::atomic<float> g_p11ForwardX{0.0f}, g_p11ForwardY{0.0f}, g_p11ForwardZ{1.0f};
std::atomic<bool> g_p11HeadPoseValid{false};
std::atomic<uint64_t> g_p11HeadPoseSamples{0};
std::atomic<bool> g_p11RendererHeadPatchLogged{false};

static void P11ResetRendererHeadBasis() {
    g_p11RightX.store(1.0f, std::memory_order_relaxed);
    g_p11RightY.store(0.0f, std::memory_order_relaxed);
    g_p11RightZ.store(0.0f, std::memory_order_relaxed);
    g_p11UpX.store(0.0f, std::memory_order_relaxed);
    g_p11UpY.store(1.0f, std::memory_order_relaxed);
    g_p11UpZ.store(0.0f, std::memory_order_relaxed);
    g_p11ForwardX.store(0.0f, std::memory_order_relaxed);
    g_p11ForwardY.store(0.0f, std::memory_order_relaxed);
    g_p11ForwardZ.store(1.0f, std::memory_order_relaxed);
    g_p11HeadPoseValid.store(false, std::memory_order_release);
}

using P11HeadConstants = p12::HeadConstants;
std::mutex g_p12ConstantsMutex;
P11HeadConstants g_p12Constants{};
std::atomic<uint64_t> g_p12BoundFrame{~0ull};
std::atomic<uint64_t> g_p46SceneFrame{~0ull};
// Camera-buffer discovery must never run against the startup/menu renderer.
// The menu uses a structurally valid 160-byte slot-1 buffer too, so accepting
// it early prevents the real gameplay camera from ever being patched.
std::atomic<bool> g_p46GameplayCameraDiscoveryEnabled{false};
std::atomic<uint64_t> g_p46GameplayCameraDiscoveryStartFrame{~0ull};

// MC5 state crosses the render/game-thread boundary only through atomics.  A
// stale or unavailable collision trace always fails open to the confirmed MC4
// pose, so native gameplay and the Fix6 compositor remain the fallback.
std::atomic<bool> g_p41InteractionReachReady{false};
std::atomic<uint64_t> g_p41InteractionReachTick{0};
std::atomic<float> g_p41HeadCollisionFraction{1.0f};
std::atomic<float> g_p41HandCollisionFraction[2]{{1.0f},{1.0f}};
std::atomic<uint64_t> g_p41CollisionTick{0};
std::atomic<bool> g_p41BodyCollisionEnabled{true};
std::atomic<bool> g_p41CamcorderRaised{false};
std::atomic<float> g_p41CamcorderProjectionBaseline{1.0f};
std::atomic<bool> g_p41CamcorderProjectionBaselineValid{false};
// A native interaction or camcorder button pulse temporarily gives the entire
// upper body back to Outlast so its authored animation is never overwritten.
std::atomic<uint64_t> g_p41NativeAnimationUntil{0};

static P11HeadConstants P11LoadHeadConstants() {
    P11HeadConstants c{};
    bool native=false;
    {
        std::lock_guard<std::mutex> lock(g_p12ConstantsMutex);
        native=g_p12Constants.params[2]>0.5f;
        if(native)c=g_p12Constants;
    }
    if(!native){
        c.right[0] = g_p11RightX.load(std::memory_order_relaxed);
        c.right[1] = g_p11RightY.load(std::memory_order_relaxed);
        c.right[2] = g_p11RightZ.load(std::memory_order_relaxed);
        c.up[0] = g_p11UpX.load(std::memory_order_relaxed);
        c.up[1] = g_p11UpY.load(std::memory_order_relaxed);
        c.up[2] = g_p11UpZ.load(std::memory_order_relaxed);
        c.forward[0] = g_p11ForwardX.load(std::memory_order_relaxed);
        c.forward[1] = g_p11ForwardY.load(std::memory_order_relaxed);
        c.forward[2] = g_p11ForwardZ.load(std::memory_order_relaxed);
        c.params[0] = kP10ProjectionScale;
        c.params[1] = g_p11HeadPoseValid.load(std::memory_order_acquire) ? 1.0f : 0.0f;
    }else{
        const auto now=GetTickCount64(),tick=g_p41CollisionTick.load(std::memory_order_acquire);
        const float fraction=tick&&now>=tick&&now-tick<250
            ?p41::SafeFraction(g_p41HeadCollisionFraction.load(std::memory_order_acquire)):1.0f;
        for(int i=0;i<3;++i)c.eyeOffset[i]*=fraction;
        c.eyeOffset[3]=g_p41CamcorderRaised.load(std::memory_order_acquire)&&
            g_p41CamcorderProjectionBaselineValid.load(std::memory_order_acquire)
            ?g_p41CamcorderProjectionBaseline.load(std::memory_order_acquire):0.0f;
    }
    return c;
}

static float P10Len3(float a, float b, float c) {
    return std::sqrt(a*a + b*b + c*c);
}

static bool P10LooksLikeOutlastViewProjection(const uint8_t* bytes, size_t byteCount) {
    if (!bytes || byteCount < 128) return false;
    for (size_t baseBytes : { size_t(0), size_t(64) }) {
        const float* m = reinterpret_cast<const float*>(bytes + baseBytes);
        for (int i = 0; i < 16; ++i) {
            if (!std::isfinite(m[i]) || std::fabs(m[i]) > 100000.0f) return false;
        }
        const float sx = P10Len3(m[0], m[4], m[8]);
        const float sy = P10Len3(m[1], m[5], m[9]);
        const float fw = P10Len3(m[3], m[7], m[11]);
        if (sx < 0.5f || sx > 4.0f || sy < 0.8f || sy > 6.0f) return false;
        const float aspectSignature = sy / sx;
        if (aspectSignature < 1.68f || aspectSignature > 1.87f) return false;
        if (fw < 0.88f || fw > 1.12f) return false;
        if (std::fabs(m[14]) < 1.5f || std::fabs(m[14]) > 2.5f) return false;
        const float dotRU = (m[0]*m[1] + m[4]*m[5] + m[8]*m[9]) / (sx * sy);
        if (std::fabs(dotRU) > 0.08f) return false;
    }
    return true;
}

static float P41SourceProjectionScale(const uint8_t* bytes,size_t byteCount){
    if(!P10LooksLikeOutlastViewProjection(bytes,byteCount))return 0.0f;
    float total=0.0f;
    for(size_t base:{size_t(0),size_t(64)}){
        const float* m=reinterpret_cast<const float*>(bytes+base);
        total+=P10Len3(m[0],m[4],m[8]);
    }
    const float result=total*0.5f;
    return std::isfinite(result)&&result>=0.25f&&result<=8.0f?result:0.0f;
}

static void P41ObserveCamcorderProjection(const uint8_t* bytes,size_t byteCount){
    if(!g_p41CamcorderRaised.load(std::memory_order_acquire)||
        g_p41CamcorderProjectionBaselineValid.load(std::memory_order_acquire))return;
    const float scale=P41SourceProjectionScale(bytes,byteCount);
    if(scale<=0.0f)return;
    g_p41CamcorderProjectionBaseline.store(scale,std::memory_order_release);
    g_p41CamcorderProjectionBaselineValid.store(true,std::memory_order_release);
    Log("MC5 CAMCORDER ZOOM BASELINE: native source projection %.5f captured; headset keeps OpenXR FOV and now follows relative game zoom.",scale);
}

static void P11ApplyRendererPatch(uint8_t* bytes, size_t byteCount) {
    p12::PatchCameraBytes(bytes, byteCount, P11LoadHeadConstants());
}
std::mutex g_p10GpuMutex;
ID3D11Buffer* g_p10GpuSource = nullptr;
ID3D11Buffer* g_p10GpuScratch = nullptr;
ID3D11Buffer* g_p10GpuShadowBuffer = nullptr;
ID3D11UnorderedAccessView* g_p10GpuUav = nullptr;
ID3D11ComputeShader* g_p10GpuPatchCS = nullptr;
ID3D11Buffer* g_p11HeadConstantsCB = nullptr;
uint64_t g_p10GpuShadowFrame = ~0ull;
std::atomic<uint64_t> g_p13SourceRevision{0};
std::atomic<uint64_t> g_p13CachedRevision{~0ull};
std::atomic<bool> g_p10GpuPatchLogged{false};

static void P10ReleaseGpuResourcesLocked() {
    if (g_p10GpuSource) { g_p10GpuSource->Release(); g_p10GpuSource = nullptr; }
    if (g_p10GpuUav) { g_p10GpuUav->Release(); g_p10GpuUav = nullptr; }
    if (g_p10GpuPatchCS) { g_p10GpuPatchCS->Release(); g_p10GpuPatchCS = nullptr; }
    if (g_p11HeadConstantsCB) { g_p11HeadConstantsCB->Release(); g_p11HeadConstantsCB = nullptr; }
    if (g_p10GpuScratch) { g_p10GpuScratch->Release(); g_p10GpuScratch = nullptr; }
    if (g_p10GpuShadowBuffer) { g_p10GpuShadowBuffer->Release(); g_p10GpuShadowBuffer = nullptr; }
    g_p10GpuShadowFrame = ~0ull;
}

static bool P10CreateGpuPatchResourcesLocked(ID3D11DeviceContext* context, ID3D11Buffer* source) {
    if (!context || !source) return false;
    if (g_p10GpuSource == source && g_p10GpuScratch && g_p10GpuShadowBuffer && g_p10GpuUav && g_p10GpuPatchCS) return true;
    P10ReleaseGpuResourcesLocked();

    D3D11_BUFFER_DESC srcDesc{};
    source->GetDesc(&srcDesc);
    if (srcDesc.ByteWidth != 160 || (srcDesc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0) return false;

    ID3D11Device* dev = nullptr;
    source->GetDevice(&dev);
    if (!dev) return false;

    D3D11_BUFFER_DESC scratchDesc{};
    scratchDesc.ByteWidth = 160;
    scratchDesc.Usage = D3D11_USAGE_DEFAULT;
    scratchDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    scratchDesc.CPUAccessFlags = 0;
    scratchDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    HRESULT hr = dev->CreateBuffer(&scratchDesc, nullptr, &g_p10GpuScratch);
    if (FAILED(hr) || !g_p10GpuScratch) { dev->Release(); P10ReleaseGpuResourcesLocked(); return false; }

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 40;
    uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    hr = dev->CreateUnorderedAccessView(g_p10GpuScratch, &uavDesc, &g_p10GpuUav);
    if (FAILED(hr) || !g_p10GpuUav) { dev->Release(); P10ReleaseGpuResourcesLocked(); return false; }

    D3D11_BUFFER_DESC shadowDesc{};
    shadowDesc.ByteWidth = 160;
    shadowDesc.Usage = D3D11_USAGE_DEFAULT;
    shadowDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = dev->CreateBuffer(&shadowDesc, nullptr, &g_p10GpuShadowBuffer);
    if (FAILED(hr) || !g_p10GpuShadowBuffer) { dev->Release(); P10ReleaseGpuResourcesLocked(); return false; }

    D3D11_BUFFER_DESC headDesc{};
    headDesc.ByteWidth = sizeof(P11HeadConstants);
    headDesc.Usage = D3D11_USAGE_DEFAULT;
    headDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    headDesc.CPUAccessFlags = 0;
    hr = dev->CreateBuffer(&headDesc, nullptr, &g_p11HeadConstantsCB);
    if (FAILED(hr) || !g_p11HeadConstantsCB) { dev->Release(); P10ReleaseGpuResourcesLocked(); return false; }

    using D3DCompileFn = HRESULT (WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
        const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
        UINT, UINT, ID3DBlob**, ID3DBlob**);
    HMODULE compiler = LoadLibraryW((ModuleDir() + L"\\d3dcompiler_46.dll").c_str());
    if (!compiler) compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = compiler ? reinterpret_cast<D3DCompileFn>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
    if (!compile) {
        if (compiler) FreeLibrary(compiler);
        dev->Release();
        P10ReleaseGpuResourcesLocked();
        return false;
    }

    const char* csSource = p12::kPatchShader;
    ID3DBlob* csBlob = nullptr;
    ID3DBlob* errors = nullptr;
    hr = compile(csSource, std::strlen(csSource), "P12_GPU_NativeEyeProjection", nullptr, nullptr,
                 "CSMain", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &csBlob, &errors);
    if (errors) {
        if (FAILED(hr)) Log("P11E GPU renderer-head CS compile error: %s", static_cast<const char*>(errors->GetBufferPointer()));
        errors->Release();
    }
    if (FAILED(hr) || !csBlob) {
        if (csBlob) csBlob->Release();
        FreeLibrary(compiler);
        dev->Release();
        P10ReleaseGpuResourcesLocked();
        return false;
    }
    hr = dev->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &g_p10GpuPatchCS);
    csBlob->Release();
    FreeLibrary(compiler);
    if (FAILED(hr) || !g_p10GpuPatchCS) { dev->Release(); P10ReleaseGpuResourcesLocked(); return false; }

    source->AddRef();
    g_p10GpuSource = source;
    dev->Release();
    return true;
}


// P11B: keep the original UE3 camera buffer pristine and switch the patched
// shadow buffer in/out when the output-merger changes depth targets. P11A made
// the decision only when VS slot 1 was rebound. UE3 often binds the camera CB
// while a shadow DSV is still active, then switches to the main scene DSV
// without rebinding the CB; that made visible HMD tracking disappear even
// though the log said the renderer patch was active.
enum class P11ADepthPassKind { Unknown, MainScene, OtherDepth };
std::atomic<bool> g_p11aMainSceneGateLogged{false};
std::atomic<bool> g_p11aShadowPassSkipLogged{false};
std::atomic<bool> g_p11bMainTransitionLogged{false};
std::atomic<bool> g_p11bShadowRestoreLogged{false};
std::atomic<ID3D11Buffer*> g_p11cReplacementCandidate{nullptr};
std::atomic<uint64_t> g_p11cReplacementCandidateLastFrame{~0ull};
std::atomic<uint32_t> g_p11cReplacementCandidateFrames{0};
std::atomic<uint64_t> g_p11cValidatedSourceLastBindFrame{0};
std::atomic<bool> g_p11cDynamicRebindLogged{false};

static P11ADepthPassKind P11AClassifyDepthStencilView(ID3D11DepthStencilView* currentDsv) {
    if (!currentDsv) return P11ADepthPassKind::Unknown;
    ID3D11Resource* res = nullptr;
    currentDsv->GetResource(&res);
    if (!res) return P11ADepthPassKind::Unknown;
    ID3D11Texture2D* tex = nullptr;
    const HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex));
    res->Release();
    if (FAILED(hr) || !tex) return P11ADepthPassKind::OtherDepth;
    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);
    tex->Release();
    const UINT expectedW = g_expectedDepthWidth.load(std::memory_order_relaxed);
    const UINT expectedH = g_expectedDepthHeight.load(std::memory_order_relaxed);
    const bool fullRes = expectedW && expectedH && td.Width == expectedW && td.Height == expectedH;
    const bool sceneDepth = fullRes && td.SampleDesc.Count == 1 && IsDepthFamilyResourceFormat(td.Format);
    return sceneDepth ? P11ADepthPassKind::MainScene : P11ADepthPassKind::OtherDepth;
}

static P11ADepthPassKind P11AClassifyDepthPass(ID3D11DeviceContext* context) {
    if (!context) return P11ADepthPassKind::Unknown;
    ID3D11RenderTargetView* currentRtv = nullptr;
    ID3D11DepthStencilView* currentDsv = nullptr;
    context->OMGetRenderTargets(1, &currentRtv, &currentDsv);
    if (currentRtv) currentRtv->Release();
    const P11ADepthPassKind pass = P11AClassifyDepthStencilView(currentDsv);
    if (currentDsv) currentDsv->Release();
    return pass;
}

static ID3D11Buffer* P10BuildGpuShadowIfNeeded(ID3D11DeviceContext* context, ID3D11Buffer* source) {
    if (!context || context != g_probeGameContext || !source) return nullptr;
    if (!g_p10ViewProjectionValidated.load(std::memory_order_acquire) ||
        !g_p10GameplayActive.load(std::memory_order_relaxed) ||
        source != g_p10ViewProjectionBuffer.load(std::memory_order_acquire)) return nullptr;

    if (P11LoadHeadConstants().params[2]<0.5f) return nullptr;
    const uint64_t frame = g_probeFrameCounter.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_p10GpuMutex);
    if (!P10CreateGpuPatchResourcesLocked(context, source)) return nullptr;
    const uint64_t revision=g_p13SourceRevision.load();
    if (g_p10GpuShadowFrame == frame && g_p13CachedRevision.load()==revision) return g_p10GpuShadowBuffer;

    context->CopyResource(g_p10GpuScratch, source);

    ID3D11ComputeShader* oldCS = nullptr;
    ID3D11ClassInstance* oldClasses[D3D11_SHADER_MAX_INTERFACES]{};
    UINT oldClassCount = D3D11_SHADER_MAX_INTERFACES;
    context->CSGetShader(&oldCS, oldClasses, &oldClassCount);
    ID3D11UnorderedAccessView* oldUav = nullptr;
    context->CSGetUnorderedAccessViews(0, 1, &oldUav);
    ID3D11Buffer* oldCsCb = nullptr;
    context->CSGetConstantBuffers(0, 1, &oldCsCb);

    const P11HeadConstants head = P11LoadHeadConstants();
    if (g_realUpdateSubresource)
        g_realUpdateSubresource(context, g_p11HeadConstantsCB, 0, nullptr, &head, 0, 0);
    else
        context->UpdateSubresource(g_p11HeadConstantsCB, 0, nullptr, &head, 0, 0);

    context->CSSetShader(g_p10GpuPatchCS, nullptr, 0);
    context->CSSetConstantBuffers(0, 1, &g_p11HeadConstantsCB);
    ID3D11UnorderedAccessView* patchUav = g_p10GpuUav;
    context->CSSetUnorderedAccessViews(0, 1, &patchUav, nullptr);
    context->Dispatch(1, 1, 1);

    ID3D11UnorderedAccessView* nullUav = nullptr;
    context->CSSetUnorderedAccessViews(0, 1, &nullUav, nullptr);
    context->CopyResource(g_p10GpuShadowBuffer, g_p10GpuScratch);

    context->CSSetUnorderedAccessViews(0, 1, &oldUav, nullptr);
    context->CSSetConstantBuffers(0, 1, &oldCsCb);
    context->CSSetShader(oldCS, oldClasses, oldClassCount);

    if (oldCsCb) oldCsCb->Release();
    if (oldUav) oldUav->Release();
    if (oldCS) oldCS->Release();
    for (UINT i = 0; i < oldClassCount; ++i) if (oldClasses[i]) oldClasses[i]->Release();

    g_p10GpuShadowFrame = frame;
    g_p13CachedRevision.store(revision);
    if (!g_p10GpuPatchLogged.exchange(true, std::memory_order_acq_rel)) {
        Log("P12 GPU NATIVE EYE PATCH ACTIVE: 160-byte VS slot-1 camera; eye rotation/translation + asymmetric headset projection; original shadow camera unchanged.");
    }
    return g_p10GpuShadowBuffer;
}

struct CBProbeState {
    UINT byteWidth = 0;
    int vsSlot = -1;
    uint64_t updates = 0;
    uint64_t framesSeen = 0;
    uint64_t lastFrame = ~0ull;
    uint64_t lastBindFrame = 0;
    bool sizeLogged = false;
    bool matrixLogged = false;
    bool readbackLogged = false;
    ID3D11Buffer* retained = nullptr; // held for the life of the probe
    ID3D11Buffer* staging = nullptr;  // CPU-readable mirror created lazily
    std::vector<uint8_t> bytes;
};
struct ActiveMapProbe {
    const void* data = nullptr;
    UINT bytes = 0;
};
std::mutex g_cbProbeMutex;
std::unordered_map<void*, CBProbeState> g_cbProbeStates;
std::unordered_map<ID3D11Resource*, ActiveMapProbe> g_activeCBMaps;

static bool ProbeFiniteMatrix(const float* m) {
    int nonZero = 0;
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(m[i]) || std::fabs(m[i]) > 1000000.0f) return false;
        if (std::fabs(m[i]) > 0.00001f) ++nonZero;
    }
    return nonZero >= 6;
}

static int ProbeMatrixScore(const float* m) {
    if (!ProbeFiniteMatrix(m)) return 0;
    int score = 1;
    const float a00 = std::fabs(m[0]);
    const float a11 = std::fabs(m[5]);
    if (a00 > 0.05f && a00 < 10.0f && a11 > 0.05f && a11 < 10.0f) score += 2;

    // Common D3D perspective matrix signatures, accepting row/column-major layouts.
    const bool perspectiveA = std::fabs(m[15]) < 0.15f && std::fabs(std::fabs(m[11]) - 1.0f) < 0.35f;
    const bool perspectiveB = std::fabs(m[15]) < 0.15f && std::fabs(std::fabs(m[14]) - 1.0f) < 0.35f;
    if (perspectiveA || perspectiveB) score += 5;

    // Common affine/view matrix signature: homogeneous term ~1 and a plausible
    // rotation basis in the upper-left 3x3.
    if (std::fabs(m[15] - 1.0f) < 0.15f) {
        const float r0 = std::sqrt(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
        const float r1 = std::sqrt(m[4]*m[4] + m[5]*m[5] + m[6]*m[6]);
        const float r2 = std::sqrt(m[8]*m[8] + m[9]*m[9] + m[10]*m[10]);
        if (r0 > 0.4f && r0 < 1.7f && r1 > 0.4f && r1 < 1.7f && r2 > 0.4f && r2 < 1.7f) score += 4;
    }
    return score;
}

static void AnalyzeCBProbeStateLocked(void* key, CBProbeState& st) {
    if (st.matrixLogged || st.vsSlot < 0 || st.framesSeen < 3 || st.bytes.size() < 64) return;
    if (g_probeCandidateLogs.load(std::memory_order_relaxed) >= kMaxProbeCandidateLogs) return;

    int bestScore = 0;
    size_t bestOffset = 0;
    for (size_t off = 0; off + 64 <= st.bytes.size(); off += 16) {
        const float* m = reinterpret_cast<const float*>(st.bytes.data() + off);
        const int score = ProbeMatrixScore(m);
        if (score > bestScore) {
            bestScore = score;
            bestOffset = off;
        }
    }
    if (bestScore < 4) return;

    const uint32_t ticket = g_probeCandidateLogs.fetch_add(1, std::memory_order_relaxed);
    if (ticket >= kMaxProbeCandidateLogs) return;
    st.matrixLogged = true;
    const float* m = reinterpret_cast<const float*>(st.bytes.data() + bestOffset);
    Log("P10 CPU MATRIX CANDIDATE #%u: cb=%p bytes=%u VSslot=%d offset=%zu score=%d frames=%llu updates=%llu",
        ticket + 1, key, st.byteWidth, st.vsSlot, bestOffset, bestScore,
        static_cast<unsigned long long>(st.framesSeen), static_cast<unsigned long long>(st.updates));
    Log("P10 CPU MATRIX #%u rows: [%.6g %.6g %.6g %.6g] [%.6g %.6g %.6g %.6g] [%.6g %.6g %.6g %.6g] [%.6g %.6g %.6g %.6g]",
        ticket + 1,
        m[0],m[1],m[2],m[3], m[4],m[5],m[6],m[7],
        m[8],m[9],m[10],m[11], m[12],m[13],m[14],m[15]);
}

static void CaptureConstantBufferBytes(ID3D11Resource* resource, const void* src, size_t srcBytes) {
    if (!resource || !src || srcBytes < 64) return;
    ID3D11Buffer* buffer = nullptr;
    if (FAILED(resource->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&buffer))) || !buffer) return;
    D3D11_BUFFER_DESC bd{};
    buffer->GetDesc(&bd);
    void* key = buffer;
    buffer->Release();
    if ((bd.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0 || bd.ByteWidth < 64 || bd.ByteWidth > 4096) return;

    const size_t n = std::min<size_t>(bd.ByteWidth, srcBytes);
    std::lock_guard<std::mutex> lock(g_cbProbeMutex);
    auto& st = g_cbProbeStates[key];
    st.byteWidth = bd.ByteWidth;
    st.updates++;
    const uint64_t frame = g_probeFrameCounter.load(std::memory_order_relaxed);
    if (st.lastFrame != frame) {
        st.lastFrame = frame;
        st.framesSeen++;
    }
    st.bytes.resize(n);
    std::memcpy(st.bytes.data(), src, n);
    AnalyzeCBProbeStateLocked(key, st);
    if(n>=160&&key==g_p10ViewProjectionBuffer.load(std::memory_order_acquire))
        P41ObserveCamcorderProjection(st.bytes.data(),st.bytes.size());
}



static void P11CProcessPipelineRebuildRequest() {
    if (!g_p11cPipelineRebuildRequested.exchange(false, std::memory_order_acq_rel)) return;
    {
        std::lock_guard<std::mutex> lock(g_p10GpuMutex);
        P10ReleaseGpuResourcesLocked();
    }
    g_p10ViewProjectionBuffer.store(nullptr, std::memory_order_release);
    g_p10ViewProjectionValidated.store(false, std::memory_order_release);
    g_p10GpuPatchLogged.store(false, std::memory_order_release);
    g_p11bMainTransitionLogged.store(false, std::memory_order_release);
    g_p11bShadowRestoreLogged.store(false, std::memory_order_release);
    g_p11aMainSceneGateLogged.store(false, std::memory_order_release);
    g_p11aShadowPassSkipLogged.store(false, std::memory_order_release);
    g_p11cReplacementCandidate.store(nullptr, std::memory_order_release);
    g_p11cReplacementCandidateFrames.store(0, std::memory_order_release);
    g_p41CamcorderProjectionBaselineValid.store(false,std::memory_order_release);
    Log("P11E RENDERER PIPELINE REBUILD DETECTED: cached VP/depth bindings invalidated; automatic rediscovery armed (AA/graphics changes supported).");
}

// Context-state swapping restores Outlast's bindings without replaying the
// VSSetConstantBuffers callback. Observe the actually restored slot-1 binding
// directly so camera discovery cannot deadlock behind the world-screen draw.
static void P46ObserveRestoredViewProjectionCandidate(ID3D11DeviceContext* context) {
    if (!context || context != g_probeGameContext ||
        !g_p46GameplayCameraDiscoveryEnabled.load(std::memory_order_acquire) ||
        g_p10ViewProjectionValidated.load(std::memory_order_acquire)) return;
    ID3D11Buffer* current = nullptr;
    context->VSGetConstantBuffers(1, 1, &current);
    if (!current) return;
    D3D11_BUFFER_DESC bd{};current->GetDesc(&bd);
    if ((bd.BindFlags & D3D11_BIND_CONSTANT_BUFFER) && bd.ByteWidth == 160) {
        const uint64_t frame=g_probeFrameCounter.load(std::memory_order_relaxed);
        bool recovered=false;
        {
            std::lock_guard<std::mutex> lock(g_cbProbeMutex);
            auto& st=g_cbProbeStates[current];st.byteWidth=bd.ByteWidth;st.vsSlot=1;st.lastBindFrame=frame;
            if(st.lastFrame!=frame){st.lastFrame=frame;++st.framesSeen;}
            if(!st.retained){st.retained=current;current=nullptr;}
            if(!st.sizeLogged){st.sizeLogged=true;recovered=true;}
        }
        if(recovered)Log("P46 RESTORED CAMERA CANDIDATE: direct VS slot-1 observation recovered a persistent 160-byte binding.");
    }
    if(current)current->Release();
}

static void P9BReadBackBoundConstantBuffers() {
    auto* context = g_probeGameContext;
    if (!context || !g_p46GameplayCameraDiscoveryEnabled.load(std::memory_order_acquire) ||
        g_p10ViewProjectionValidated.load(std::memory_order_acquire)) return;
    const uint64_t frame = g_probeFrameCounter.load(std::memory_order_relaxed);
    const uint64_t sceneFrame = g_p46SceneFrame.load(std::memory_order_acquire);
    // Only sample a restored binding after the immediately preceding game
    // frame actually drew through the full-resolution scene depth target.
    // This excludes logo/menu/loading cameras while retaining context-state
    // recovery for the world-screen renderer.
    if (sceneFrame != frame && (sceneFrame == ~0ull || sceneFrame + 1 != frame)) return;
    P46ObserveRestoredViewProjectionCandidate(context);
    // Once native gameplay begins, validate the first observed scene binding
    // immediately. Startup/menu discovery is already blocked above, so the old
    // 30-frame sampling delay is unnecessary during this short handoff window.
    // Keep the sparse cadence only for later recovery attempts.
    const uint64_t discoveryStart = g_p46GameplayCameraDiscoveryStartFrame.load(std::memory_order_acquire);
    const bool freshGameplayHandoff = discoveryStart != ~0ull &&
        frame >= discoveryStart && frame - discoveryStart <= 120;
    if (!freshGameplayHandoff && (frame < 60 || (frame % 30) != 0)) return;

    struct Item { void* key; ID3D11Buffer* src; ID3D11Buffer* staging; UINT bytes; int slot; uint64_t lastBindFrame; };
    std::vector<Item> items;
    {
        std::lock_guard<std::mutex> lock(g_cbProbeMutex);
        for (auto& kv : g_cbProbeStates) {
            auto& st = kv.second;
            if (st.vsSlot != 1 || !st.retained || st.byteWidth != 160 ||
                st.lastBindFrame < g_p46GameplayCameraDiscoveryStartFrame.load(std::memory_order_acquire)) continue;
            if (!st.staging) {
                ID3D11Device* dev = nullptr;
                st.retained->GetDevice(&dev);
                if (!dev) continue;
                D3D11_BUFFER_DESC bd{};
                st.retained->GetDesc(&bd);
                bd.Usage = D3D11_USAGE_STAGING;
                bd.BindFlags = 0;
                bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                bd.MiscFlags = 0;
                bd.StructureByteStride = 0;
                ID3D11Buffer* staging = nullptr;
                HRESULT hr = dev->CreateBuffer(&bd, nullptr, &staging);
                dev->Release();
                if (FAILED(hr) || !staging) continue;
                st.staging = staging;
            }
            if (frame > st.lastBindFrame + 8) continue;
            items.push_back({kv.first, st.retained, st.staging, st.byteWidth, st.vsSlot, st.lastBindFrame});
        }
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.lastBindFrame > b.lastBindFrame; });
    if (items.size() > 4) items.resize(4);

    for (const auto& item : items) {
        context->CopyResource(item.staging, item.src);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = context->Map(item.staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr) || !mapped.pData) continue;
        std::vector<uint8_t> snapshot(item.bytes);
        std::memcpy(snapshot.data(), mapped.pData, item.bytes);
        context->Unmap(item.staging, 0);

        if (item.slot == 1 && item.bytes == 160 && P10LooksLikeOutlastViewProjection(snapshot.data(), snapshot.size())) {
            P41ObserveCamcorderProjection(snapshot.data(),snapshot.size());
            ID3D11Buffer* expected = nullptr;
            if (g_p10ViewProjectionBuffer.compare_exchange_strong(expected, item.src, std::memory_order_acq_rel)) {
                Log("P11E EXACT VIEW/PROJECTION BUFFER VALIDATED: cb=%p VSslot=1 bytes=160. Startup validation complete; recurring GPU readback now disabled.", item.src);
            }
            if (g_p10ViewProjectionBuffer.load(std::memory_order_acquire) == item.src) {
                g_p10ViewProjectionValidated.store(true, std::memory_order_release);
                g_p11cValidatedSourceLastBindFrame.store(frame, std::memory_order_relaxed);
                g_p11cDynamicRebindLogged.store(false, std::memory_order_release);
            }
        }
        if (g_p10ViewProjectionValidated.load(std::memory_order_acquire)) break;

        struct Candidate { int score; size_t off; };
        std::vector<Candidate> candidates;
        for (size_t off = 0; off + 64 <= snapshot.size(); off += 16) {
            const float* m = reinterpret_cast<const float*>(snapshot.data() + off);
            int score = ProbeMatrixScore(m);
            if (score >= 3) candidates.push_back({score, off});
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.off < b.off;
        });
        if (candidates.size() > 3) candidates.resize(3);

        if (!candidates.empty()) {
            for (size_t rank = 0; rank < candidates.size(); ++rank) {
                const auto& c = candidates[rank];
                const float* m = reinterpret_cast<const float*>(snapshot.data() + c.off);
                Log("P10 GPU MATRIX SAMPLE frame=%llu cb=%p VSslot=%d bytes=%u rank=%zu offset=%zu score=%d rows: [%.6g %.6g %.6g %.6g] [%.6g %.6g %.6g %.6g] [%.6g %.6g %.6g %.6g] [%.6g %.6g %.6g %.6g]",
                    static_cast<unsigned long long>(frame), item.key, item.slot, item.bytes, rank + 1, c.off, c.score,
                    m[0],m[1],m[2],m[3], m[4],m[5],m[6],m[7], m[8],m[9],m[10],m[11], m[12],m[13],m[14],m[15]);
            }
        } else {
            bool shouldLog = false;
            {
                std::lock_guard<std::mutex> lock(g_cbProbeMutex);
                auto it = g_cbProbeStates.find(item.key);
                if (it != g_cbProbeStates.end() && !it->second.readbackLogged) {
                    it->second.readbackLogged = true;
                    shouldLog = true;
                }
            }
            if (shouldLog) Log("P10 GPU READBACK OK but no matrix-like 4x4 block: cb=%p VSslot=%d bytes=%u frame=%llu",
                               item.key, item.slot, item.bytes, static_cast<unsigned long long>(frame));
        }
    }
}


static void P11BRefreshCameraBindingForDepthPass(ID3D11DeviceContext* self, ID3D11DepthStencilView* dsv) {
    if (!self || self != g_probeGameContext || !g_realVSSetConstantBuffers) return;
    const P11ADepthPassKind pass = P11AClassifyDepthStencilView(dsv);
    if(pass==P11ADepthPassKind::MainScene){
        // Scene presence is independent of which camera buffer was previously
        // validated. Fix 2 tied these together, so one wrong menu candidate
        // made a real level look like a loading screen forever.
        g_p46SceneFrame.store(g_probeFrameCounter.load(),std::memory_order_release);
        P46ObserveRestoredViewProjectionCandidate(self);
    }
    if (!g_p10ViewProjectionValidated.load(std::memory_order_acquire)) return;

    ID3D11Buffer* source = g_p10ViewProjectionBuffer.load(std::memory_order_acquire);
    if (!source) return;

    ID3D11Buffer* current = nullptr;
    self->VSGetConstantBuffers(1, 1, &current);
    if (!current) return;

    ID3D11Buffer* shadowSnapshot = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_p10GpuMutex);
        shadowSnapshot = g_p10GpuShadowBuffer;
        if (shadowSnapshot) shadowSnapshot->AddRef();
    }

    if(!g_p10GameplayActive.load(std::memory_order_relaxed)){
        if(shadowSnapshot&&current==shadowSnapshot)g_realVSSetConstantBuffers(self,1,1,&source);
        if(shadowSnapshot)shadowSnapshot->Release();current->Release();return;
    }
    if (pass == P11ADepthPassKind::MainScene) {
        const bool cameraBinding = current == source || (shadowSnapshot && current == shadowSnapshot);
        if (cameraBinding) {
            if (ID3D11Buffer* patched = P10BuildGpuShadowIfNeeded(self, source)) {
                if (current != patched) g_realVSSetConstantBuffers(self, 1, 1, &patched);
                g_p12BoundFrame.store(g_probeFrameCounter.load(), std::memory_order_release);
                if (!g_p11bMainTransitionLogged.exchange(true, std::memory_order_acq_rel)) {
                    Log("P11E MAIN-SCENE DSV TRANSITION PATCHED: main scene became active after CB bind; renderer HMD buffer forced onto VS slot 1.");
                }
            }
        }
    } else {
        // No depth target is also outside the validated scene. Do not leak
        // the geometry camera into full-screen post-process or Scaleform passes.
        if (shadowSnapshot && current == shadowSnapshot) {
            ID3D11Buffer* original = source;
            g_realVSSetConstantBuffers(self, 1, 1, &original);
            if (!g_p11bShadowRestoreLogged.exchange(true, std::memory_order_acq_rel)) {
                Log("P11E SHADOW/DEPTH DSV TRANSITION RESTORED ORIGINAL: HMD-patched VS slot 1 removed before non-main depth rendering.");
            }
        }
    }

    if (shadowSnapshot) shadowSnapshot->Release();
    current->Release();
}

void P37CameraTargetBound(ID3D11DeviceContext* ctx,UINT count,ID3D11RenderTargetView* const* views,ID3D11DepthStencilView* dsv);

namespace p31bridge {
struct DrawScope {
    ID3D11DeviceContext* ctx;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> original;UINT slot=0;
    explicit DrawScope(ID3D11DeviceContext* c);
    ~DrawScope();
};
}
void STDMETHODCALLTYPE HookOMSetRenderTargets(ID3D11DeviceContext* self, UINT numViews,
                                               ID3D11RenderTargetView* const* rtvs,
                                               ID3D11DepthStencilView* dsv) {
    auto original = g_realOMSetRenderTargets;
    if (!original) return;
    original(self, numViews, rtvs, dsv);
    P11BRefreshCameraBindingForDepthPass(self, dsv);
    P37CameraTargetBound(self,numViews,rtvs,dsv);
    P13RepairContextHooks(self);
}

void STDMETHODCALLTYPE HookOMSetRenderTargetsUAV(ID3D11DeviceContext* self, UINT numRTVs,
                                                  ID3D11RenderTargetView* const* rtvs,
                                                  ID3D11DepthStencilView* dsv,
                                                  UINT uavStartSlot, UINT numUAVs,
                                                  ID3D11UnorderedAccessView* const* uavs,
                                                  const UINT* initialCounts) {
    auto original = g_realOMSetRenderTargetsUAV;
    if (!original) return;
    original(self, numRTVs, rtvs, dsv, uavStartSlot, numUAVs, uavs, initialCounts);
    P11BRefreshCameraBindingForDepthPass(self, dsv);
    P37CameraTargetBound(self,numRTVs,rtvs,dsv);
}

void STDMETHODCALLTYPE HookVSSetConstantBuffers(ID3D11DeviceContext* self, UINT startSlot, UINT numBuffers, ID3D11Buffer* const* buffers) {
    auto original = g_realVSSetConstantBuffers;
    if (!original) return;

    if (self == g_probeGameContext && buffers) {
        {
            std::lock_guard<std::mutex> lock(g_cbProbeMutex);
            for (UINT i = 0; i < numBuffers; ++i) {
                ID3D11Buffer* b = buffers[i];
                if (!b) continue;
                D3D11_BUFFER_DESC bd{};
                b->GetDesc(&bd);
                if ((bd.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0 || bd.ByteWidth < 64 || bd.ByteWidth > 4096) continue;
                auto& st = g_cbProbeStates[b];
                st.byteWidth = bd.ByteWidth;
                st.vsSlot = static_cast<int>(startSlot + i);
                st.lastBindFrame = g_probeFrameCounter.load(std::memory_order_relaxed);
                if (!st.retained) { b->AddRef(); st.retained = b; }
                if (!st.sizeLogged && g_probeFrameCounter.load(std::memory_order_relaxed) >= 2) {
                    st.sizeLogged = true;
                    Log("P10 VS CONSTANT BUFFER observed: cb=%p slot=%u bytes=%u usage=%u cpuAccess=0x%X",
                        b, startSlot + i, bd.ByteWidth, static_cast<unsigned>(bd.Usage), bd.CPUAccessFlags);
                }
                AnalyzeCBProbeStateLocked(b, st);
            }
        }

        if(startSlot<=1&&1<startSlot+numBuffers&&g_p10ViewProjectionValidated.load()&&
           buffers[1-startSlot]==g_p10ViewProjectionBuffer.load()&&P11AClassifyDepthPass(self)==P11ADepthPassKind::MainScene)
            g_p46SceneFrame.store(g_probeFrameCounter.load(),std::memory_order_release);

        // P11C: graphics-option changes can recreate the renderer camera CB
        // without restarting the process. If the validated source disappears
        // and another 160-byte slot-1 buffer is bound for several frames,
        // invalidate the cache and let the proven one-time readback validator
        // discover the replacement automatically.
        if (startSlot <= 1 && 1 < startSlot + numBuffers) {
            const UINT index = 1 - startSlot;
            ID3D11Buffer* b = buffers[index];
            if (b) {
                D3D11_BUFFER_DESC bd{};
                b->GetDesc(&bd);
                if (bd.ByteWidth == 160 && (bd.BindFlags & D3D11_BIND_CONSTANT_BUFFER) &&
                    P11AClassifyDepthPass(self) == P11ADepthPassKind::MainScene) {
                    const uint64_t frame = g_probeFrameCounter.load(std::memory_order_relaxed);
                    ID3D11Buffer* validated = g_p10ViewProjectionBuffer.load(std::memory_order_acquire);
                    if (g_p10ViewProjectionValidated.load(std::memory_order_acquire) && b == validated) {
                        g_p11cValidatedSourceLastBindFrame.store(frame, std::memory_order_relaxed);
                        g_p11cReplacementCandidate.store(nullptr, std::memory_order_relaxed);
                        g_p11cReplacementCandidateFrames.store(0, std::memory_order_relaxed);
                    } else if (g_p10ViewProjectionValidated.load(std::memory_order_acquire) && validated && b != validated) {
                        ID3D11Buffer* prev = g_p11cReplacementCandidate.load(std::memory_order_relaxed);
                        uint64_t lastFrame = g_p11cReplacementCandidateLastFrame.load(std::memory_order_relaxed);
                        if (prev != b) {
                            g_p11cReplacementCandidate.store(b, std::memory_order_relaxed);
                            g_p11cReplacementCandidateFrames.store(1, std::memory_order_relaxed);
                            g_p11cReplacementCandidateLastFrame.store(frame, std::memory_order_relaxed);
                        } else if (lastFrame != frame) {
                            g_p11cReplacementCandidateLastFrame.store(frame, std::memory_order_relaxed);
                            const uint32_t seen = g_p11cReplacementCandidateFrames.fetch_add(1, std::memory_order_relaxed) + 1;
                            const uint64_t oldLast = g_p11cValidatedSourceLastBindFrame.load(std::memory_order_relaxed);
                            if (seen >= 6 && frame > oldLast + 5) {
                                g_p11cPipelineRebuildRequested.store(true, std::memory_order_release);
                                if (!g_p11cDynamicRebindLogged.exchange(true, std::memory_order_acq_rel))
                                    Log("P11E CAMERA BUFFER REPLACEMENT DETECTED: old=%p new=%p; automatic VP rediscovery requested.", validated, b);
                            }
                        }
                    }
                }
            }
        }

        // If UE3 updates this DEFAULT buffer through a path that bypasses our
        // CPU UpdateSubresource observation, substitute one patched shadow copy
        // at bind time. This is intentionally a fallback only; once the direct
        // update patch fires we stop doing the synchronous readback entirely.
        if (startSlot <= 1 && 1 < startSlot + numBuffers) {
            const UINT index = 1 - startSlot;
            ID3D11Buffer* source = buffers[index];
            if (source && source == g_p10ViewProjectionBuffer.load(std::memory_order_acquire)) {
                const P11ADepthPassKind pass = P11AClassifyDepthPass(self);
                if (pass == P11ADepthPassKind::OtherDepth) {
                    if (!g_p11aShadowPassSkipLogged.exchange(true, std::memory_order_acq_rel)) {
                        Log("P11E SHADOW/DEPTH PASS ISOLATED: validated camera VP buffer rebound while a non-main DSV is active; leaving that pass unmodified.");
                    }
                } else if (pass == P11ADepthPassKind::MainScene) {
                    if (pass == P11ADepthPassKind::MainScene &&
                        !g_p11aMainSceneGateLogged.exchange(true, std::memory_order_acq_rel)) {
                        Log("P11E MAIN-SCENE VP GATE ACTIVE: HMD matrix substitution restricted to the captured full-resolution scene depth pass.");
                    }
                    if (ID3D11Buffer* shadow = P10BuildGpuShadowIfNeeded(self, source)) {
                        std::vector<ID3D11Buffer*> patchedBindings(buffers, buffers + numBuffers);
                        patchedBindings[index] = shadow;
                        original(self, startSlot, numBuffers, patchedBindings.data());
                        g_p12BoundFrame.store(g_probeFrameCounter.load(), std::memory_order_release);
                        return;
                    }
                }
            }
        }
    }
    original(self, startSlot, numBuffers, buffers);
}

HRESULT STDMETHODCALLTYPE HookContextMap(ID3D11DeviceContext* self, ID3D11Resource* resource, UINT subresource,
                                         D3D11_MAP mapType, UINT mapFlags, D3D11_MAPPED_SUBRESOURCE* mapped) {
    auto original = g_realContextMap;
    if (!original) return E_FAIL;
    HRESULT hr = original(self, resource, subresource, mapType, mapFlags, mapped);
    P13RepairContextHooks(self);
    if (self != g_probeGameContext) return hr;
    if (SUCCEEDED(hr) && resource && mapped && mapped->pData && subresource == 0) {
        ID3D11Buffer* buffer = nullptr;
        if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&buffer))) && buffer) {
            D3D11_BUFFER_DESC bd{};
            buffer->GetDesc(&bd);
            buffer->Release();
            if ((bd.BindFlags & D3D11_BIND_CONSTANT_BUFFER) && bd.ByteWidth >= 64 && bd.ByteWidth <= 4096) {
                std::lock_guard<std::mutex> lock(g_cbProbeMutex);
                g_activeCBMaps[resource] = {mapped->pData, bd.ByteWidth};
            }
        }
    }
    return hr;
}

void STDMETHODCALLTYPE HookContextUnmap(ID3D11DeviceContext* self, ID3D11Resource* resource, UINT subresource) {
    if (self != g_probeGameContext) { if (g_realContextUnmap) g_realContextUnmap(self, resource, subresource); return; }
    if (resource && subresource == 0) {
        ActiveMapProbe mp{};
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(g_cbProbeMutex);
            auto it = g_activeCBMaps.find(resource);
            if (it != g_activeCBMaps.end()) {
                mp = it->second;
                g_activeCBMaps.erase(it);
                found = true;
            }
        }
        if (found && mp.data && mp.bytes) CaptureConstantBufferBytes(resource, mp.data, mp.bytes);
    }
    if (g_realContextUnmap) g_realContextUnmap(self, resource, subresource);
    if(resource==g_p10ViewProjectionBuffer.load())g_p13SourceRevision.fetch_add(1);
}

static void P15InvalidateGpuCopy(ID3D11Resource* resource);
void STDMETHODCALLTYPE HookContextUpdateSubresource(ID3D11DeviceContext* self, ID3D11Resource* dst, UINT dstSubresource,
                                                    const D3D11_BOX* dstBox, const void* srcData, UINT srcRowPitch, UINT srcDepthPitch) {
    auto original = g_realUpdateSubresource;
    if (!original) return;
    if(dstBox||dstSubresource!=0)P15InvalidateGpuCopy(dst);

    if (dst && dstSubresource == 0 && !dstBox && srcData) {
        ID3D11Buffer* buffer = nullptr;
        if (SUCCEEDED(dst->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&buffer))) && buffer) {
            D3D11_BUFFER_DESC bd{};
            buffer->GetDesc(&bd);

            if ((bd.BindFlags & D3D11_BIND_CONSTANT_BUFFER) && bd.ByteWidth >= 64 && bd.ByteWidth <= 4096) {
                // Unlike P9B, observe updates from any context that shares this
                // patched D3D11 vtable. UE3 can populate render constants away
                // from the immediate context and later bind them there.
                CaptureConstantBufferBytes(dst, srcData, bd.ByteWidth);

                // P11E deliberately never patches the original shared UE3 VP buffer in-place.
                // Shadow/depth passes may reuse it. The renderer-HMD transform lives only in our
                // GPU shadow CB and is switched onto VS slot 1 for the main-scene DSV.
            }
            buffer->Release();
        }
    }
    original(self, dst, dstSubresource, dstBox, srcData, srcRowPitch, srcDepthPitch);
    if(dst==g_p10ViewProjectionBuffer.load())g_p13SourceRevision.fetch_add(1);
    P13RepairContextHooks(self);
}

static bool PatchContextSlot(void** vtable, size_t index, void* hook, void** originalOut) {
    if (!vtable || !hook || !originalOut || !vtable[index]) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    *originalOut = vtable[index];
    vtable[index] = hook;
    FlushInstructionCache(GetCurrentProcess(), &vtable[index], sizeof(void*));
    DWORD ignored = 0;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &ignored);
    return true;
}

static void P15InvalidateGpuCopy(ID3D11Resource* resource){
    std::lock_guard<std::mutex> lock(g_cbProbeMutex);auto it=g_cbProbeStates.find(resource);
    if(it!=g_cbProbeStates.end())it->second.bytes.clear();
}
#include "p17_shader_camera.inl"
#include "p15_pixel_scope.inl"
#include "p16_capture.inl"
void P29ObserveCurrentUiTarget(ID3D11DeviceContext* ctx,p13::UiShader ui);
#include "p13_ui_capture.inl"
#include "p37_camera_capture.inl"

bool InstallViewProjectionProbeHooks(ID3D11DeviceContext* context) {
    if (!context) return false;
    if (g_matrixProbeHooksInstalled.load(std::memory_order_acquire)) return true;
    void*** object = reinterpret_cast<void***>(context);
    if (!object || !*object) return false;
    void** vt = *object;
    g_probeGameContext = context;

    // ID3D11DeviceContext inherits ID3D11DeviceChild before its own methods.
    // ABI indices (IUnknown 0-2, ID3D11DeviceChild 3-6, then context methods):
    // VSSetConstantBuffers=7, Map=14, Unmap=15, GetData=29, SetPredication=30, OMSetRenderTargets=33,
    // OMSetRenderTargetsAndUnorderedAccessViews=34, UpdateSubresource=48.
    // P9 accidentally used 3/10/11/44, which overwrote GetDevice, PSSetSamplers,
    // VSSetShader and RSSetViewports with incompatible hook signatures and crashed.
    constexpr size_t kVSSetConstantBuffers = 7;
    constexpr size_t kMap = 14;
    constexpr size_t kUnmap = 15;
    constexpr size_t kGetData = 29;
    constexpr size_t kSetPredication = 30;
    constexpr size_t kOMSetRenderTargets = 33;
    constexpr size_t kOMSetRenderTargetsUAV = 34;
    constexpr size_t kUpdateSubresource = 48;

    void* origVS = nullptr;
    void* origMap = nullptr;
    void* origUnmap = nullptr;
    void* origGetData = nullptr;
    void* origSetPredication = nullptr;
    void* origOM = nullptr;
    void* origOMUAV = nullptr;
    void* origUpdate = nullptr;

    auto restoreSlot = [&](size_t index, void* original) {
        if (!original) return;
        DWORD oldProtect = 0;
        if (VirtualProtect(&vt[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            vt[index] = original;
            FlushInstructionCache(GetCurrentProcess(), &vt[index], sizeof(void*));
            DWORD ignored = 0;
            VirtualProtect(&vt[index], sizeof(void*), oldProtect, &ignored);
        }
    };

    if (!PatchContextSlot(vt, kVSSetConstantBuffers, reinterpret_cast<void*>(&HookVSSetConstantBuffers), &origVS)) return false;
    if (!PatchContextSlot(vt, kMap, reinterpret_cast<void*>(&HookContextMap), &origMap)) {
        restoreSlot(kVSSetConstantBuffers, origVS); return false;
    }
    if (!PatchContextSlot(vt, kUnmap, reinterpret_cast<void*>(&HookContextUnmap), &origUnmap)) {
        restoreSlot(kMap, origMap); restoreSlot(kVSSetConstantBuffers, origVS); return false;
    }
    if (!PatchContextSlot(vt, kGetData, reinterpret_cast<void*>(&HookContextGetData), &origGetData)) {
        restoreSlot(kUnmap, origUnmap); restoreSlot(kMap, origMap); restoreSlot(kVSSetConstantBuffers, origVS); return false;
    }
    if (!PatchContextSlot(vt, kSetPredication, reinterpret_cast<void*>(&HookContextSetPredication), &origSetPredication)) {
        restoreSlot(kGetData, origGetData); restoreSlot(kUnmap, origUnmap); restoreSlot(kMap, origMap); restoreSlot(kVSSetConstantBuffers, origVS); return false;
    }
    if (!PatchContextSlot(vt, kOMSetRenderTargets, reinterpret_cast<void*>(&HookOMSetRenderTargets), &origOM)) {
        restoreSlot(kSetPredication, origSetPredication); restoreSlot(kGetData, origGetData); restoreSlot(kUnmap, origUnmap); restoreSlot(kMap, origMap); restoreSlot(kVSSetConstantBuffers, origVS); return false;
    }
    if (!PatchContextSlot(vt, kOMSetRenderTargetsUAV, reinterpret_cast<void*>(&HookOMSetRenderTargetsUAV), &origOMUAV)) {
        restoreSlot(kOMSetRenderTargets, origOM); restoreSlot(kSetPredication, origSetPredication); restoreSlot(kGetData, origGetData); restoreSlot(kUnmap, origUnmap); restoreSlot(kMap, origMap); restoreSlot(kVSSetConstantBuffers, origVS); return false;
    }
    if (!PatchContextSlot(vt, kUpdateSubresource, reinterpret_cast<void*>(&HookContextUpdateSubresource), &origUpdate)) {
        restoreSlot(kOMSetRenderTargetsUAV, origOMUAV); restoreSlot(kOMSetRenderTargets, origOM);
        restoreSlot(kSetPredication, origSetPredication); restoreSlot(kGetData, origGetData); restoreSlot(kUnmap, origUnmap); restoreSlot(kMap, origMap); restoreSlot(kVSSetConstantBuffers, origVS); return false;
    }

    g_realVSSetConstantBuffers = reinterpret_cast<VSSetConstantBuffersFn>(origVS);
    g_realContextMap = reinterpret_cast<MapFn>(origMap);
    g_realContextUnmap = reinterpret_cast<UnmapFn>(origUnmap);
    g_realContextGetData = reinterpret_cast<GetDataFn>(origGetData);
    g_realContextSetPredication = reinterpret_cast<SetPredicationFn>(origSetPredication);
    g_realOMSetRenderTargets = reinterpret_cast<OMSetRenderTargetsFn>(origOM);
    g_realOMSetRenderTargetsUAV = reinterpret_cast<OMSetRenderTargetsUAVFn>(origOMUAV);
    g_realUpdateSubresource = reinterpret_cast<UpdateSubresourceFn>(origUpdate);

    P13InstallUiHooks(context);
    g_matrixProbeHooksInstalled.store(true, std::memory_order_release);
    Log("PF13 D3D11 RENDERER READY: CPU occlusion predicates covered; non-visibility GPU predicates preserved; main-scene pixel camera correction active.");
    return true;
}

// P6 native camera experiment. Outlast 2's shipped UE3 binary contains a native
// registration entry for AOLHeroexecGetViewRotation. We patch ONLY that generated
// name/function pair before the executable's normal initialization runs. If the
// exact shipping-build validation fails, nothing is patched and P6's SendInput
// camera bridge remains the automatic fallback.
struct UE3Rotator {
    int32_t Pitch;
    int32_t Yaw;
    int32_t Roll;
};
using HeroGetViewRotationExecFn = void (__fastcall*)(void*, void*, void*);
HeroGetViewRotationExecFn g_originalHeroGetViewRotation = nullptr;
std::atomic<bool> g_nativeTablePatched{false};
std::atomic<uint64_t> g_nativeHookCalls{0};
std::atomic<uint64_t> g_p46NativeViewFrame{~0ull};
std::atomic<bool> g_nativeGameplayActive{false};
std::atomic<void*> g_nativeHeroSelf{nullptr};
// P45 camera handoff: the validated native movement hook receives the real
// AOLPlayerController even when the generated Hero view hook is called with a
// null/self-less object. Prefer this owner for pawn state and camera latches.
std::atomic<void*> g_nativeControllerSelf{nullptr};
std::atomic<bool> g_nativeHeadLock{false};
std::atomic<float> g_nativeHeadYawRadians{0.0f};
std::atomic<float> g_nativeHeadPitchRadians{0.0f};
// PF12 temporarily mirrors HMD pitch into the native controller only while
// NativePlayerMove evaluates gameplay interactions. During that narrow scope
// GetViewRotation must not add the same pitch a second time.
thread_local bool g_nativeControllerPitchSynchronized=false;

// PF17 installs the HMD correction on AOLHero's actual GetViewRotation
// virtual (vtable slot 0x818). UE3's renderer, visibility code and native
// interaction scan call this virtual directly; patching only the generated
// UnrealScript exec wrapper left those systems on the flat/gamepad camera.
bool P47IsNativeViewVirtualPatched(void* self);
void P47EnsureNativeViewVirtual(void* self);

void __fastcall HookHeroGetViewRotation(void* self, void* stack, void* result) {
    auto original = g_originalHeroGetViewRotation;
    if (!original) return;
    if (self) {
        g_nativeHeroSelf.store(self, std::memory_order_relaxed);
        // This wrapper proves that self is an AOLHero before we inspect or
        // patch its class vtable. The original call below then uses the native
        // virtual immediately, including on this first observed frame.
        P47EnsureNativeViewVirtual(self);
    }
    original(self, stack, result);
    g_nativeHookCalls.fetch_add(1, std::memory_order_relaxed);
    if (!result || !g_nativeGameplayActive.load(std::memory_order_relaxed)) return;
    // The virtual hook already corrected the returned rotator. Adding the
    // headset angles again here would double the camera rotation.
    if (P47IsNativeViewVirtualPatched(self)) return;
    g_p46NativeViewFrame.store(g_probeFrameCounter.load(),std::memory_order_release);

    // UE3 FRotator uses 65536 integer units for one revolution. The original
    // function supplies the game's controller/body rotation; we add only the
    // HMD's rotation relative to the gameplay recenter pose. This is deliberately
    // non-destructive: it does not write the controller rotation or player body.
    constexpr float kRotatorUnitsPerRadian = 10430.3783504704527f; // 65536 / (2*pi)
    auto* rot = reinterpret_cast<UE3Rotator*>(result);
    const float yaw = g_nativeControllerPitchSynchronized?0.0f:g_nativeHeadYawRadians.load(std::memory_order_relaxed);
    const float pitch = g_nativeControllerPitchSynchronized?0.0f:
        g_nativeHeadPitchRadians.load(std::memory_order_relaxed);
    rot->Yaw += static_cast<int32_t>(std::lround(yaw * kRotatorUnitsPerRadian));
    rot->Pitch += static_cast<int32_t>(std::lround(pitch * kRotatorUnitsPerRadian));
}

bool InstallHeroViewRotationTableHookEarly() {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!base) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    // Validated against the user's exact 2018 Outlast2.exe. We still verify the
    // name pointer, string and original function pointer before touching memory.
    constexpr uintptr_t kRegistrationPairRva = 0x01FCB430;
    constexpr uintptr_t kNameRva = 0x019CE1F0;
    constexpr uintptr_t kOriginalExecRva = 0x004C4990;
    constexpr const char* kExpectedName = "AOLHeroexecGetViewRotation";
    const size_t imageSize = nt->OptionalHeader.SizeOfImage;
    if (kRegistrationPairRva + 16 > imageSize || kNameRva + 32 > imageSize || kOriginalExecRva >= imageSize) return false;

    auto** pair = reinterpret_cast<void**>(base + kRegistrationPairRva);
    const char* expectedNamePtr = reinterpret_cast<const char*>(base + kNameRva);
    if (pair[0] != expectedNamePtr) return false;
    if (std::strcmp(expectedNamePtr, kExpectedName) != 0) return false;
    void* expectedOriginal = base + kOriginalExecRva;
    if (pair[1] != expectedOriginal) return false;

    g_originalHeroGetViewRotation = reinterpret_cast<HeroGetViewRotationExecFn>(pair[1]);
    DWORD oldProtect = 0;
    if (!VirtualProtect(&pair[1], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        g_originalHeroGetViewRotation = nullptr;
        return false;
    }
    pair[1] = reinterpret_cast<void*>(&HookHeroGetViewRotation);
    DWORD ignored = 0;
    VirtualProtect(&pair[1], sizeof(void*), oldProtect, &ignored);
    g_nativeTablePatched.store(true, std::memory_order_release);
    return true;
}

std::wstring ModuleDir() {
    wchar_t path[MAX_PATH]{};
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ModuleDir), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring s(path);
    auto p = s.find_last_of(L"\\/");
    return p == std::wstring::npos ? L"." : s.substr(0, p);
}

std::string WideToUtf8(const wchar_t* w) {
    if (!w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

void Log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    const std::wstring path = ModuleDir() + L"\\outlast2_vr_p34.log";
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fwprintf(f, L"[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    char buf[4096]{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    wchar_t wbuf[4096]{};
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, 4096);
    fwprintf(f, L"%ls\n", wbuf);
    fclose(f);
}

void P19InspectPawn(void* self);
void P20Observe(void* controller);
void P22NativeRoomMove(void* controller,float dt);
namespace p31native {void Tick(void* controller);}
namespace p35runtime {void Tick(void* controller);}
#include "p15_native.inl"
#include "p19_rig_capture.inl"
#include "p22_roomscale.inl"
#include "p20_native_ik.inl"
#include "p27_native_hud.inl"
#include "p29_camcorder_probe.inl"
#include "p31_native_capture.inl"
#include "p31_lcd_bridge.inl"
#include "p46_native_ui.inl"
#include "p35_runtime.inl"
#include "p18_xinput.inl"

using P47NativeGetViewRotationFn=UE3Rotator* (__fastcall*)(void*,UE3Rotator*);
struct P47NativeViewSlot {
    std::atomic<void*> table{nullptr};
    std::atomic<void*> original{nullptr};
};
std::array<P47NativeViewSlot,8> g_p47NativeViewSlots{};
std::mutex g_p47NativeViewInstallMutex;
std::atomic<uint64_t> g_p47NativeViewCalls{0};

static P47NativeGetViewRotationFn P47OriginalNativeView(void* self){
    if(!self||!P15Readable(self,sizeof(void*)))return nullptr;
    void* table=nullptr;std::memcpy(&table,self,sizeof(table));
    for(auto& entry:g_p47NativeViewSlots){
        if(entry.table.load(std::memory_order_acquire)==table)
            return reinterpret_cast<P47NativeGetViewRotationFn>(
                entry.original.load(std::memory_order_acquire));
    }
    return nullptr;
}

bool P47IsNativeViewVirtualPatched(void* self){
    if(!self||!P15Readable(self,sizeof(void*)))return false;
    void* table=nullptr;std::memcpy(&table,self,sizeof(table));
    if(!table)return false;
    for(auto& entry:g_p47NativeViewSlots)
        if(entry.table.load(std::memory_order_acquire)==table)return true;
    return false;
}

static UE3Rotator* __fastcall P47NativeGetViewRotation(void* self,UE3Rotator* out){
    const auto original=P47OriginalNativeView(self);
    if(!original)return out;
    UE3Rotator* result=original(self,out);
    if(!result||!P15Readable(result,sizeof(UE3Rotator))||
       !g_nativeGameplayActive.load(std::memory_order_relaxed))return result;

    g_p46NativeViewFrame.store(g_probeFrameCounter.load(),std::memory_order_release);
    constexpr float kRotatorUnitsPerRadian=10430.3783504704527f;
    const float yaw=g_nativeControllerPitchSynchronized?0.0f:
        g_nativeHeadYawRadians.load(std::memory_order_relaxed);
    const float pitch=g_nativeControllerPitchSynchronized?0.0f:
        g_nativeHeadPitchRadians.load(std::memory_order_relaxed);
    result->Yaw+=static_cast<int32_t>(std::lround(yaw*kRotatorUnitsPerRadian));
    result->Pitch+=static_cast<int32_t>(std::lround(pitch*kRotatorUnitsPerRadian));
    if(g_p47NativeViewCalls.fetch_add(1,std::memory_order_relaxed)==0)
        Log("PF17 NATIVE VIEW ACTIVE: renderer, culling, lights/reflections, particles and native interaction aim now share the headset yaw/pitch; controller rotation and movement input are untouched.");
    return result;
}

void P47EnsureNativeViewVirtual(void* self){
    if(!self||P47IsNativeViewVirtualPatched(self)||!P15Readable(self,sizeof(void*)))return;
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if(!base)return;
    // Exact retail exec-wrapper bytes prove that GetViewRotation is virtual
    // slot 0x818 with the FRotator*(self, out) ABI used below.
    const unsigned char wrapperSig[]={
        0x48,0x8b,0x07,0x48,0x8d,0x54,0x24,0x20,0x48,0x8b,0xcf,
        0xff,0x90,0x18,0x08,0x00,0x00};
    if(std::memcmp(base+0x4c49c1,wrapperSig,sizeof(wrapperSig)))return;

    void* table=nullptr;std::memcpy(&table,self,sizeof(table));
    if(!table||!P15Readable(table,0x820))return;
    auto** slot=reinterpret_cast<void**>(static_cast<unsigned char*>(table)+0x818);
    std::lock_guard<std::mutex> lock(g_p47NativeViewInstallMutex);
    if(P47IsNativeViewVirtualPatched(self))return;
    void* target=*slot;
    if(!target||target==reinterpret_cast<void*>(&P47NativeGetViewRotation))return;

    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE)return;
    auto* targetBytes=static_cast<unsigned char*>(target);
    if(targetBytes<base+0x1000||targetBytes>=base+nt->OptionalHeader.SizeOfImage)return;
    MEMORY_BASIC_INFORMATION targetRegion{};
    if(!VirtualQuery(target,&targetRegion,sizeof(targetRegion))||
       !(targetRegion.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return;

    P47NativeViewSlot* freeEntry=nullptr;
    for(auto& entry:g_p47NativeViewSlots){
        if(!entry.table.load(std::memory_order_acquire)){freeEntry=&entry;break;}
    }
    if(!freeEntry){Log("PF17 NATIVE VIEW rejected: supported hero-vtable capacity exhausted.");return;}
    freeEntry->original.store(target,std::memory_order_release);
    freeEntry->table.store(table,std::memory_order_release);
    DWORD old=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)){
        freeEntry->table.store(nullptr,std::memory_order_release);
        freeEntry->original.store(nullptr,std::memory_order_release);return;
    }
    *slot=reinterpret_cast<void*>(&P47NativeGetViewRotation);
    DWORD ignored=0;VirtualProtect(slot,sizeof(void*),old,&ignored);
    FlushInstructionCache(GetCurrentProcess(),slot,sizeof(void*));
    Log("PF17 NATIVE VIEW HOOK installed: AOLHero vtable=%p original=%p slot=0x818; no input-axis or pawn-rotation writes.",table,target);
}
HMODULE LoadRealDInput() {
    std::call_once(g_realOnce, [] {
        wchar_t sys[MAX_PATH]{};
        GetSystemDirectoryW(sys, MAX_PATH);
        std::wstring p = sys;
        p += L"\\dinput8.dll";
        g_realDInput = LoadLibraryW(p.c_str());
        Log("P13 BUILD_ID=%s", kBuildId);
        // Keep the existing P44 gameplay projection and interaction ownership.
        // The validated native Hero view path is now attempted so engine-side
        // camera consumers receive the same HMD yaw/pitch as the renderer.
        // If the executable is not the validated retail build, the hook is
        // rejected and the renderer-only fallback remains unchanged.
        Log("MC6F BODY-ANCHORED TRACKED ARMS: Fix 6 visuals and MC6E hand-trace correction retained; headset, hands and native body now share one bounded tracking frame.");
        P18InstallXInput();
        const auto mc2Config=ModuleDir()+L"\\outlast2_vr_p32.ini";
        const bool mc2IkRequested=GetPrivateProfileIntW(L"VR",L"BodyIK",0,mc2Config.c_str())!=0;
        g_p20Enabled.store(mc2IkRequested);
        // MC6F deliberately removes the tracked-right-arm camcorder path. Right
        // grip is only a short native gamepad button pulse; the game owns every
        // body, hand and camcorder-model transform.
        g_p20PhysicalCamcorder.store(false);
        if(mc2IkRequested&&!P20Install()){
            g_p20Enabled.store(false);g_p20PhysicalCamcorder.store(false);
            Log("MC6F TRACKED-ARM HOOK REJECTED: executable/rig signatures differ; all native animation retained.");
        }else Log("MC6F TRACKED-ARM CONFIG: trackedArms=%d physicalCamcorder=0 handWallTrace=0 bodyAnchor=10cm; native-animation handoff enabled.",mc2IkRequested?1:0);
        p46ui::Install();
        P11EInstallFrustumCullBypass();
        if(!P15InstallMovement())Log("P15 NATIVE MOVEMENT HOOK rejected: executable signature mismatch; original movement retained.");
        const bool nativeCameraHook=InstallHeroViewRotationTableHookEarly();
        Log("MC6F CINEMATIC CAMERA: native AOLHeroexecGetViewRotation hook=%s; renderer fallback=%s; body-relative bounded 6DoF stays active while torso/head/camera and native camcorder animation remain game-owned.",
            nativeCameraHook?"installed":"rejected by retail signature validation",nativeCameraHook?"disabled":"active");
        Log("Real system DirectInput8=%s", g_realDInput ? "OK" : "FAILED");
        Log("GetProcAddress interception installed=%s", g_getProcHookInstalled ? "yes" : "no");
    });
    return g_realDInput;
}

template <class T>
T RealDInputProc(const char* name) {
    HMODULE m = LoadRealDInput();
    return m ? reinterpret_cast<T>(::GetProcAddress(m, name)) : nullptr;
}

const char* XrResultName(XrResult r) {
    switch (r) {
        case XR_SUCCESS: return "XR_SUCCESS";
        case XR_TIMEOUT_EXPIRED: return "XR_TIMEOUT_EXPIRED";
        case XR_SESSION_LOSS_PENDING: return "XR_SESSION_LOSS_PENDING";
        case XR_EVENT_UNAVAILABLE: return "XR_EVENT_UNAVAILABLE";
        case XR_SESSION_NOT_FOCUSED: return "XR_SESSION_NOT_FOCUSED";
        case XR_FRAME_DISCARDED: return "XR_FRAME_DISCARDED";
        case XR_ERROR_VALIDATION_FAILURE: return "XR_ERROR_VALIDATION_FAILURE";
        case XR_ERROR_RUNTIME_FAILURE: return "XR_ERROR_RUNTIME_FAILURE";
        case XR_ERROR_OUT_OF_MEMORY: return "XR_ERROR_OUT_OF_MEMORY";
        case XR_ERROR_INITIALIZATION_FAILED: return "XR_ERROR_INITIALIZATION_FAILED";
        case XR_ERROR_FUNCTION_UNSUPPORTED: return "XR_ERROR_FUNCTION_UNSUPPORTED";
        case XR_ERROR_EXTENSION_NOT_PRESENT: return "XR_ERROR_EXTENSION_NOT_PRESENT";
        case XR_ERROR_GRAPHICS_DEVICE_INVALID: return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
        case XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING: return "XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING";
        case XR_ERROR_RUNTIME_UNAVAILABLE: return "XR_ERROR_RUNTIME_UNAVAILABLE";
        default: return "XR_RESULT_OTHER";
    }
}

struct OpenXRQuad {
    HMODULE loader = nullptr;
    PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr = nullptr;
    PFN_xrCreateInstance xrCreateInstance = nullptr;
    PFN_xrDestroyInstance xrDestroyInstance = nullptr;
    PFN_xrGetSystem xrGetSystem = nullptr;
    PFN_xrGetD3D11GraphicsRequirementsKHR xrGetD3D11GraphicsRequirementsKHR = nullptr;
    PFN_xrCreateSession xrCreateSession = nullptr;
    PFN_xrDestroySession xrDestroySession = nullptr;
    PFN_xrCreateReferenceSpace xrCreateReferenceSpace = nullptr;
    PFN_xrDestroySpace xrDestroySpace = nullptr;
    PFN_xrLocateSpace xrLocateSpace = nullptr;
    PFN_xrLocateViews xrLocateViews = nullptr;
    PFN_xrEnumerateViewConfigurationViews xrEnumerateViewConfigurationViews = nullptr;
    PFN_xrEnumerateSwapchainFormats xrEnumerateSwapchainFormats = nullptr;
    PFN_xrCreateSwapchain xrCreateSwapchain = nullptr;
    PFN_xrDestroySwapchain xrDestroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages xrEnumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage xrAcquireSwapchainImage = nullptr;
    PFN_xrWaitSwapchainImage xrWaitSwapchainImage = nullptr;
    PFN_xrReleaseSwapchainImage xrReleaseSwapchainImage = nullptr;
    PFN_xrPollEvent xrPollEvent = nullptr;
    PFN_xrBeginSession xrBeginSession = nullptr;
    PFN_xrEndSession xrEndSession = nullptr;
    PFN_xrWaitFrame xrWaitFrame = nullptr;
    PFN_xrBeginFrame xrBeginFrame = nullptr;
    PFN_xrEndFrame xrEndFrame = nullptr;

    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace viewSpace = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE;

    // P4 keeps the proven flat menu compositor and automatically enters
    // projection mode when Outlast hides its cursor for gameplay. No F6 gate.
    XrSwapchain flatSwapchain = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> flatImages;
    XrSwapchain stereoSwapchain = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> stereoImages;

    bool initialized = false;
    bool failed = false;
    bool sessionRunning = false;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::vector<int64_t> p46RuntimeFormats;
    bool p46GameplaySrgb = true;
    DXGI_FORMAT P46ColorFormat(DXGI_FORMAT source){
        const DXGI_FORMAT desired=p46::Srgb(source);
        if(std::find(p46RuntimeFormats.begin(),p46RuntimeFormats.end(),static_cast<int64_t>(desired))!=p46RuntimeFormats.end())return desired;
        return source;
    }
    DXGI_FORMAT P46ProjectionFormat(DXGI_FORMAT source,uint32_t arraySize){
        // The array-2 swapchain keeps the native gameplay projection and raw
        // bytes. CP1 changes only their OpenXR transfer-function declaration.
        // Array-1 is the frontend capture; the separate world-screen renderer
        // also continues to call P46ColorFormat directly.
        return arraySize==2?p46::Gameplay(source,p46GameplaySrgb):P46ColorFormat(source);
    }
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    bool gameplayVr = false;
    bool haveLastHeadAngles = false;
    uint32_t hiddenCursorFrames = 0;
    uint32_t visibleCursorFrames = 0;
    float lastYaw = 0.0f;
    float lastPitch = 0.0f;
    bool haveNativeCenter = false;
    bool nativeModeLogged = false;
    float nativeCenterYaw = 0.0f;
    float nativeCenterPitch = 0.0f;

    bool p11HaveHeadCenter = false;
    bool p11HaveLastRawHead = false;
    bool p11RendererTrackingLogged = false;
    XrQuaternionf p11HeadCenter{0.0f, 0.0f, 0.0f, 1.0f};
    XrQuaternionf p11LastRawHead{0.0f, 0.0f, 0.0f, 1.0f};

    // P7A stops using a constant fake stereo shift. It samples Outlast 2's
    // real scene depth buffer and generates eye-dependent parallax from depth.
    // This is a depth-backed stereoscopic pass, not yet a second UE3 geometry
    // render. It gives us a real 3D depth signal while we locate the engine's
    // dual-render boundary for the next stage.
    static constexpr float kEyeHalfSeparationMeters = 0.032f;
    // Conservative first-test separation.  The earlier dormant P7 pass used
    // 0.020, which is too aggressive for a public test and exaggerates
    // disocclusion around hands and nearby geometry.
    static constexpr float kDepthStereoStrength = 0.008f;

    ID3D11Texture2D* colorCopy = nullptr;
    ID3D11ShaderResourceView* colorSrv = nullptr;
    ID3D11Texture2D* depthCopy = nullptr;
    ID3D11ShaderResourceView* depthSrv = nullptr;
    DXGI_FORMAT capturedDepthFormat = DXGI_FORMAT_UNKNOWN;
    UINT capturedDepthWidth = 0;
    UINT capturedDepthHeight = 0;
    ID3D11VertexShader* stereoVS = nullptr;
    ID3D11PixelShader* stereoPS = nullptr;
    ID3D11SamplerState* stereoSampler = nullptr;
    ID3D11SamplerState* stereoDepthSampler = nullptr;
    ID3D11RasterizerState* stereoRaster = nullptr;
    ID3D11DepthStencilState* stereoDepthState = nullptr;
    ID3D11DeviceContext* stereoDeferred = nullptr;
    ID3D11Buffer* stereoCB[2]{};
    bool stereoPipelineReady = false;
    bool depthCaptureLogged = false;
    bool depthUnavailableLogged = false;
    bool lastFrameDepthStereo = false;

    struct StereoConstants {
        float sourceAspect;
        float targetAspect;
        float eyeSign;
        float strength;
        float depthScale;
        float pad0;
        float pad1;
        float pad2;
    };

    static float WrapPi(float v) {
        constexpr float pi = 3.14159265358979323846f;
        constexpr float twoPi = 6.28318530717958647692f;
        while (v > pi) v -= twoPi;
        while (v < -pi) v += twoPi;
        return v;
    }

    static bool OutlastHasFocus() {
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        return pid == GetCurrentProcessId();
    }

    static void PoseYawPitch(const XrQuaternionf& q, float& yaw, float& pitch) {
        const float fx = -2.0f * (q.x * q.z + q.w * q.y);
        const float fy = -2.0f * (q.y * q.z - q.w * q.x);
        const float fz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        yaw = std::atan2(fx, -fz);
        pitch = std::asin(std::clamp(fy, -1.0f, 1.0f));
    }

    static bool SystemCursorVisible() {
        CURSORINFO ci{};
        ci.cbSize = sizeof(ci);
        if (!GetCursorInfo(&ci)) return false;
        return (ci.flags & CURSOR_SHOWING) != 0;
    }

    static XrQuaternionf P11QuatConjugate(const XrQuaternionf& q) {
        return {-q.x, -q.y, -q.z, q.w};
    }

    static XrQuaternionf P11QuatMul(const XrQuaternionf& a, const XrQuaternionf& b) {
        XrQuaternionf q{};
        q.w = a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z;
        q.x = a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y;
        q.y = a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x;
        q.z = a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w;
        const float n = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
        if (n > 1e-6f) { q.x/=n; q.y/=n; q.z/=n; q.w/=n; }
        else { q = {0.0f,0.0f,0.0f,1.0f}; }
        return q;
    }

    static void P11RotateVector(const XrQuaternionf& q, float x, float y, float z,
                                float& ox, float& oy, float& oz) {
        // Efficient q*v*q^-1 for a unit quaternion.
        const float tx = 2.0f * (q.y*z - q.z*y);
        const float ty = 2.0f * (q.z*x - q.x*z);
        const float tz = 2.0f * (q.x*y - q.y*x);
        ox = x + q.w*tx + (q.y*tz - q.z*ty);
        oy = y + q.w*ty + (q.z*tx - q.x*tz);
        oz = z + q.w*tz + (q.x*ty - q.y*tx);
    }

    void P11StoreRelativeHeadBasis(const XrQuaternionf& rel) {
        float rx,ry,rz, ux,uy,uz, fx,fy,fz;
        P11RotateVector(rel, 1.0f,0.0f,0.0f, rx,ry,rz);
        P11RotateVector(rel, 0.0f,1.0f,0.0f, ux,uy,uz);
        P11RotateVector(rel, 0.0f,0.0f,-1.0f, fx,fy,fz);
        // XR uses -Z as forward.  Outlast's VP basis is represented as
        // Right/Up/Forward, so negate the XR Z coefficient when expressing an
        // axis in that basis. Identity therefore maps to identity exactly.
        g_p11RightX.store(rx, std::memory_order_relaxed);
        g_p11RightY.store(ry, std::memory_order_relaxed);
        g_p11RightZ.store(-rz, std::memory_order_relaxed);
        g_p11UpX.store(ux, std::memory_order_relaxed);
        g_p11UpY.store(uy, std::memory_order_relaxed);
        g_p11UpZ.store(-uz, std::memory_order_relaxed);
        g_p11ForwardX.store(fx, std::memory_order_relaxed);
        g_p11ForwardY.store(fy, std::memory_order_relaxed);
        g_p11ForwardZ.store(-fz, std::memory_order_relaxed);
        g_p11HeadPoseValid.store(true, std::memory_order_release);
        g_p11HeadPoseSamples.fetch_add(1, std::memory_order_relaxed);
    }

    void UpdateAutomaticPresentationMode() {
        // Outlast 2 shows the OS cursor in its front-end menus and hides it
        // while the mouse is captured for gameplay. Debounce transitions so
        // loading/menu animation frames cannot flicker between layer types.
        // Once the validated game state is available it is authoritative. The
        // front-end can leave the Windows cursor visible, and renderer bindings
        // are only discovered after gameplay starts; requiring either here made
        // a circular gate that could leave an entire level on the menu screen.
        // Cursor/projection heuristics remain only as a startup fallback while
        // the native state is still unknown.
        const bool nativeStateKnown = p46ui::StateKnown();
        const bool cursorVisible = p46ui::Screen() ||
            (!nativeStateKnown && (SystemCursorVisible() || !g_p10ViewProjectionValidated.load()));
        if (cursorVisible) {
            ++visibleCursorFrames;
            hiddenCursorFrames = 0;
            if (gameplayVr && visibleCursorFrames >= (p46ui::Screen()?1u:3u)) {
                gameplayVr = false;
                haveLastHeadAngles = false;
                haveNativeCenter = false;
                g_nativeGameplayActive.store(false, std::memory_order_relaxed);
                g_nativeHeadLock.store(false, std::memory_order_relaxed);
                g_nativeHeadYawRadians.store(0.0f, std::memory_order_relaxed);
                g_nativeHeadPitchRadians.store(0.0f, std::memory_order_relaxed);
                p11HaveHeadCenter = false;
                p11HaveLastRawHead = false;
                P11ResetRendererHeadBasis();
                Log("P46 AUTO -> WORLD SCREEN (%s)",p46ui::Reason());
            }
        } else {
            ++hiddenCursorFrames;
            visibleCursorFrames = 0;
            if (!gameplayVr && hiddenCursorFrames >= 3) {
                gameplayVr = true;
                haveLastHeadAngles = false;
                haveNativeCenter = false;
                g_nativeGameplayActive.store(true, std::memory_order_relaxed);
                p11HaveHeadCenter = false;
                p11HaveLastRawHead = false;
                P11ResetRendererHeadBasis();
                Log("P11E AUTO -> GAMEPLAY VR (%s); renderer HMD center will lock on first valid pose.",
                    nativeStateKnown?"validated native game state":"cursor hidden fallback");
            }
        }
        g_nativeGameplayActive.store(gameplayVr, std::memory_order_relaxed);
        g_p10GameplayActive.store(gameplayVr, std::memory_order_relaxed);
    }

    void UpdateRendererHeadPoseFromHmd(XrTime predictedDisplayTime) {
        // P37: once the exact native AOLHero::GetViewRotation hook is live, the
        // engine camera (and its Wwise listener/culling consumers) owns HMD yaw/pitch.
        // Do not apply the same yaw/pitch again in the renderer matrix.
        if(g_nativeTablePatched.load(std::memory_order_relaxed)&&g_nativeHookCalls.load(std::memory_order_relaxed)>0){
            p11HaveHeadCenter=false;p11HaveLastRawHead=false;P11ResetRendererHeadBasis();return;
        }
        if (!gameplayVr || !xrLocateSpace || !localSpace || !viewSpace || !OutlastHasFocus()) {
            p11HaveHeadCenter = false;
            p11HaveLastRawHead = false;
            P11ResetRendererHeadBasis();
            return;
        }
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        const XrResult r = xrLocateSpace(viewSpace, localSpace, predictedDisplayTime, &loc);
        if (XR_FAILED(r) || (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0) return;

        XrQuaternionf current = loc.pose.orientation;
        const float qn = std::sqrt(current.x*current.x + current.y*current.y + current.z*current.z + current.w*current.w);
        if (qn <= 1e-6f) return;
        current.x/=qn; current.y/=qn; current.z/=qn; current.w/=qn;

        // Treat a huge one-frame orientation discontinuity as an OpenXR
        // recenter/runtime jump rather than whipping the renderer matrix.
        if (p11HaveLastRawHead) {
            float dot = std::fabs(current.x*p11LastRawHead.x + current.y*p11LastRawHead.y +
                                  current.z*p11LastRawHead.z + current.w*p11LastRawHead.w);
            dot = std::clamp(dot, 0.0f, 1.0f);
            const float deltaAngle = 2.0f * std::acos(dot);
            if (deltaAngle > 0.65f) {
                p11HaveHeadCenter = false;
                P11ResetRendererHeadBasis();
                Log("P11E OpenXR orientation jump %.3f rad -> renderer HMD center reset.", deltaAngle);
            }
        }
        p11LastRawHead = current;
        p11HaveLastRawHead = true;

        if (!p11HaveHeadCenter) {
            p11HeadCenter = current;
            p11HaveHeadCenter = true;
            P11ResetRendererHeadBasis();
            return;
        }

        const XrQuaternionf rel = P11QuatMul(P11QuatConjugate(p11HeadCenter), current);
        P11StoreRelativeHeadBasis(rel);
        if (!p11RendererTrackingLogged) {
            p11RendererTrackingLogged = true;
            Log("P11E RENDERER-NATIVE HMD ROTATION ACTIVE: OpenXR orientation now modifies the validated world VP matrix about Blake eye pivot with next-frame prediction; SendInput path disabled.");
        }
    }

    template <typename T>
    bool LoadInstanceProc(const char* name, T& out) {
        PFN_xrVoidFunction fn = nullptr;
        XrResult r = xrGetInstanceProcAddr(instance, name, &fn);
        if (XR_FAILED(r) || !fn) {
            Log("OpenXR missing %s: %s (%d)", name, XrResultName(r), static_cast<int>(r));
            return false;
        }
        out = reinterpret_cast<T>(fn);
        return true;
    }

    bool LoadLoader() {
        if (loader) return true;
        const std::wstring local = ModuleDir() + L"\\openxr_loader.dll";
        loader = LoadLibraryW(local.c_str());
        if (!loader) {
            Log("OpenXR loader failed to load from %s (err=%lu)", WideToUtf8(local.c_str()).c_str(), GetLastError());
            return false;
        }
        xrGetInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(::GetProcAddress(loader, "xrGetInstanceProcAddr"));
        xrCreateInstance = reinterpret_cast<PFN_xrCreateInstance>(::GetProcAddress(loader, "xrCreateInstance"));
        if (!xrGetInstanceProcAddr || !xrCreateInstance) {
            Log("OpenXR loader exports missing xrGetInstanceProcAddr/xrCreateInstance");
            return false;
        }
        Log("OpenXR loader loaded from game directory.");
        return true;
    }

    bool CreateColorSwapchain(uint32_t arraySize, XrSwapchain& outSwapchain,
                              std::vector<XrSwapchainImageD3D11KHR>& outImages,
                              const char* label) {
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        ci.format = static_cast<int64_t>(P46ProjectionFormat(format,arraySize));
        ci.sampleCount = 1;
        ci.width = width;
        ci.height = height;
        ci.faceCount = 1;
        ci.arraySize = arraySize;
        ci.mipCount = 1;
        XrResult xr = xrCreateSwapchain(session, &ci, &outSwapchain);
        if (XR_FAILED(xr)) {
            Log("xrCreateSwapchain(%s,array=%u) failed: %s (%d)", label, arraySize, XrResultName(xr), static_cast<int>(xr));
            return false;
        }
        uint32_t imageCount = 0;
        xr = xrEnumerateSwapchainImages(outSwapchain, 0, &imageCount, nullptr);
        if (XR_FAILED(xr) || !imageCount) {
            Log("xrEnumerateSwapchainImages(%s,count) failed: %s (%d)", label, XrResultName(xr), static_cast<int>(xr));
            return false;
        }
        outImages.assign(imageCount, XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        xr = xrEnumerateSwapchainImages(outSwapchain, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(outImages.data()));
        if (XR_FAILED(xr)) {
            Log("xrEnumerateSwapchainImages(%s,list) failed: %s (%d)", label, XrResultName(xr), static_cast<int>(xr));
            return false;
        }
        Log("P4 %s swapchain ready: %ux%u array=%u images=%u", label, width, height, arraySize, imageCount);
        return true;
    }

    bool Init(IDXGISwapChain* realSwapChain) {
        if (initialized) return true;
        if (failed || !realSwapChain) return false;

        ID3D11Device* dev = nullptr;
        HRESULT hr = realSwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev));
        if (FAILED(hr) || !dev) {
            Log("P4 GetDevice(D3D11) failed: 0x%08X", static_cast<unsigned>(hr));
            failed = true;
            return false;
        }
        device = dev;
        device->GetImmediateContext(&context);

        DXGI_SWAP_CHAIN_DESC sd{};
        realSwapChain->GetDesc(&sd);
        width = sd.BufferDesc.Width;
        height = sd.BufferDesc.Height;
        format = sd.BufferDesc.Format;
        Log("Captured Outlast2 swapchain for P4: %ux%u fmt=%u windowed=%s", width, height,
            static_cast<unsigned>(format), sd.Windowed ? "true" : "false");

        if (!LoadLoader()) { failed = true; return false; }

        const char* exts[] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME };
        XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
        std::snprintf(ici.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Outlast2VR");
        ici.applicationInfo.applicationVersion = 3;
        std::snprintf(ici.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "UE3-Outlast2-VR-Bridge");
        ici.applicationInfo.engineVersion = 3;
        ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
        ici.enabledExtensionCount = 1;
        ici.enabledExtensionNames = exts;

        XrResult xr = xrCreateInstance(&ici, &instance);
        if (XR_FAILED(xr)) {
            Log("xrCreateInstance failed: %s (%d)", XrResultName(xr), static_cast<int>(xr));
            failed = true;
            return false;
        }

        bool ok = true;
        ok &= LoadInstanceProc("xrDestroyInstance", xrDestroyInstance);
        ok &= LoadInstanceProc("xrGetSystem", xrGetSystem);
        ok &= LoadInstanceProc("xrGetD3D11GraphicsRequirementsKHR", xrGetD3D11GraphicsRequirementsKHR);
        ok &= LoadInstanceProc("xrCreateSession", xrCreateSession);
        ok &= LoadInstanceProc("xrDestroySession", xrDestroySession);
        ok &= LoadInstanceProc("xrCreateReferenceSpace", xrCreateReferenceSpace);
        ok &= LoadInstanceProc("xrDestroySpace", xrDestroySpace);
        ok &= LoadInstanceProc("xrLocateSpace", xrLocateSpace);
        ok &= LoadInstanceProc("xrLocateViews", xrLocateViews);
        ok &= LoadInstanceProc("xrEnumerateViewConfigurationViews", xrEnumerateViewConfigurationViews);
        ok &= LoadInstanceProc("xrEnumerateSwapchainFormats", xrEnumerateSwapchainFormats);
        ok &= LoadInstanceProc("xrCreateSwapchain", xrCreateSwapchain);
        ok &= LoadInstanceProc("xrDestroySwapchain", xrDestroySwapchain);
        ok &= LoadInstanceProc("xrEnumerateSwapchainImages", xrEnumerateSwapchainImages);
        ok &= LoadInstanceProc("xrAcquireSwapchainImage", xrAcquireSwapchainImage);
        ok &= LoadInstanceProc("xrWaitSwapchainImage", xrWaitSwapchainImage);
        ok &= LoadInstanceProc("xrReleaseSwapchainImage", xrReleaseSwapchainImage);
        ok &= LoadInstanceProc("xrPollEvent", xrPollEvent);
        ok &= LoadInstanceProc("xrBeginSession", xrBeginSession);
        ok &= LoadInstanceProc("xrEndSession", xrEndSession);
        ok &= LoadInstanceProc("xrWaitFrame", xrWaitFrame);
        ok &= LoadInstanceProc("xrBeginFrame", xrBeginFrame);
        ok &= LoadInstanceProc("xrEndFrame", xrEndFrame);
        if (!ok) { failed = true; return false; }

        XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
        sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        xr = xrGetSystem(instance, &sgi, &systemId);
        if (XR_FAILED(xr)) {
            Log("xrGetSystem(HMD) failed: %s (%d)", XrResultName(xr), static_cast<int>(xr));
            failed = true;
            return false;
        }

        XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        xr = xrGetD3D11GraphicsRequirementsKHR(instance, systemId, &req);
        if (XR_FAILED(xr)) {
            Log("xrGetD3D11GraphicsRequirementsKHR failed: %s (%d)", XrResultName(xr), static_cast<int>(xr));
            failed = true;
            return false;
        }

        IDXGIDevice* xdev = nullptr;
        IDXGIAdapter* adapter = nullptr;
        LUID gameLuid{};
        if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&xdev))) && xdev) {
            if (SUCCEEDED(xdev->GetAdapter(&adapter)) && adapter) {
                DXGI_ADAPTER_DESC ad{};
                if (SUCCEEDED(adapter->GetDesc(&ad))) gameLuid = ad.AdapterLuid;
            }
        }
        Log("Runtime D3D11 LUID=%08X:%08X minFeature=0x%04X; game LUID=%08X:%08X feature=0x%04X",
            static_cast<unsigned>(req.adapterLuid.HighPart), static_cast<unsigned>(req.adapterLuid.LowPart),
            static_cast<unsigned>(req.minFeatureLevel), static_cast<unsigned>(gameLuid.HighPart),
            static_cast<unsigned>(gameLuid.LowPart), static_cast<unsigned>(device->GetFeatureLevel()));
        if (adapter) adapter->Release();
        if (xdev) xdev->Release();
        if (req.adapterLuid.HighPart != gameLuid.HighPart || req.adapterLuid.LowPart != gameLuid.LowPart) {
            Log("P4 cannot share Outlast2 textures with OpenXR: runtime requires a different GPU adapter.");
            failed = true;
            return false;
        }
        if (device->GetFeatureLevel() < req.minFeatureLevel) {
            Log("P4 cannot create session: game D3D feature level is below runtime minimum.");
            failed = true;
            return false;
        }

        XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        binding.device = device;
        XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
        sci.next = &binding;
        sci.systemId = systemId;
        xr = xrCreateSession(instance, &sci, &session);
        if (XR_FAILED(xr)) {
            Log("xrCreateSession failed: %s (%d)", XrResultName(xr), static_cast<int>(xr));
            failed = true;
            return false;
        }

        XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        rs.poseInReferenceSpace.orientation.w = 1.0f;
        xr = xrCreateReferenceSpace(session, &rs, &viewSpace);
        if (XR_FAILED(xr)) { Log("xrCreateReferenceSpace(VIEW) failed: %d", static_cast<int>(xr)); failed = true; return false; }

        XrReferenceSpaceCreateInfo localRs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        localRs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        localRs.poseInReferenceSpace.orientation.w = 1.0f;
        xr = xrCreateReferenceSpace(session, &localRs, &localSpace);
        if (XR_FAILED(xr)) { Log("xrCreateReferenceSpace(LOCAL) failed: %d", static_cast<int>(xr)); failed = true; return false; }

        uint32_t formatCount = 0;
        xr = xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr);
        if (XR_FAILED(xr) || !formatCount) { Log("xrEnumerateSwapchainFormats(count) failed: %d", static_cast<int>(xr)); failed = true; return false; }
        std::vector<int64_t> formats(formatCount);
        xr = xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data());
        if (XR_FAILED(xr)) { Log("xrEnumerateSwapchainFormats(list) failed: %d", static_cast<int>(xr)); failed = true; return false; }
        p46RuntimeFormats=formats;
        p46GameplaySrgb=GetPrivateProfileIntW(
            L"VR",L"GameplaySrgb",1,
            (ModuleDir()+L"\\outlast2_vr_p35.ini").c_str())!=0;
        const int64_t frontendDesired = static_cast<int64_t>(P46ColorFormat(format));
        int64_t gameplayDesired = static_cast<int64_t>(p46::Gameplay(format,p46GameplaySrgb));
        if(p46GameplaySrgb&&std::find(formats.begin(),formats.end(),gameplayDesired)==formats.end()){
            p46GameplaySrgb=false;
            gameplayDesired=static_cast<int64_t>(p46::Gameplay(format,false));
            Log("CP1 COLOR FALLBACK: runtime lacks requested sRGB gameplay format; using legacy UNORM transfer.");
        }
        Log("CP1 COLOR: frontend source DXGI=%u -> XR DXGI=%u; gameplay XR DXGI=%u; GameplaySrgb=%d; raw pixel copy unchanged.",
            unsigned(format),unsigned(frontendDesired),unsigned(gameplayDesired),p46GameplaySrgb?1:0);
        if (std::find(formats.begin(), formats.end(), frontendDesired) == formats.end() ||
            std::find(formats.begin(), formats.end(), gameplayDesired) == formats.end()) {
            Log("Runtime lacks required frontend/gameplay formats %u/%u.",
                static_cast<unsigned>(frontendDesired),static_cast<unsigned>(gameplayDesired));
            failed = true;
            return false;
        }

        if (!CreateColorSwapchain(1, flatSwapchain, flatImages, "flat-menu")) { failed = true; return false; }
        if (!CreateColorSwapchain(2, stereoSwapchain, stereoImages, "stereo-gameplay")) { failed = true; return false; }

        if(!P18Init())Log("P18 VR action controls disabled or unavailable; physical keyboard/gamepad input retained.");
        initialized = true;
        Log("CP1 OpenXR initialized: PF10 projection/world-screen paths unchanged; gameplay color transfer=%s; no gamma shader or pixel filter.",
            p46GameplaySrgb?"sRGB":"legacy UNORM");
        return true;
    }

    void PollEvents() {
        if (!initialized || !xrPollEvent) return;
        for (;;) {
            XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
            XrResult r = xrPollEvent(instance, &ev);
            if (r == XR_EVENT_UNAVAILABLE) break;
            if (XR_FAILED(r)) { Log("xrPollEvent failed: %s (%d)", XrResultName(r), static_cast<int>(r)); break; }
            if (ev.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                const auto* change=reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&ev);
                if (change->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL) {
                    p12ReferenceChangeTime=change->changeTime;
                    p46LastGameplayValid=false;
                    P12Reset(false);
                    Log("P12 LOCAL reference-space change scheduled; discarded eye history.");
                }
            }
            if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(&ev);
                state = changed->state;
                p46LastGameplayValid=false;p46Anchored=false;
                if(state!=XR_SESSION_STATE_FOCUSED)P18Clear();
                P12Reset(true);
                Log("OpenXR session state -> %d", static_cast<int>(state));
                if (state == XR_SESSION_STATE_READY && !sessionRunning) {
                    XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XrResult br = xrBeginSession(session, &bi);
                    if (XR_SUCCEEDED(br)) {
                        sessionRunning = true;
                        Log("xrBeginSession SUCCESS. P4 starts flat and automatically enters VR when gameplay captures/hides the cursor.");
                    } else Log("xrBeginSession failed: %s (%d)", XrResultName(br), static_cast<int>(br));
                } else if (state == XR_SESSION_STATE_STOPPING && sessionRunning) {
                    P13DiscardOpenFrame();
                    XrResult er = xrEndSession(session);
                    Log("xrEndSession on STOPPING: %s (%d)", XrResultName(er), static_cast<int>(er));
                    sessionRunning = false;
                } else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING) {
                    p13FrameBegun=false;
                    sessionRunning = false;
                }
            }
        }
    }

    bool AcquireAndCopyFlat(ID3D11Texture2D* backbuffer) {
        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrResult xr = xrAcquireSwapchainImage(flatSwapchain, &ai, &idx);
        if (XR_FAILED(xr)) return false;
        XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wi.timeout = XR_INFINITE_DURATION;
        xr = xrWaitSwapchainImage(flatSwapchain, &wi);
        if (XR_SUCCEEDED(xr) && idx < flatImages.size() && flatImages[idx].texture && backbuffer) {
            context->CopyResource(flatImages[idx].texture, backbuffer);
        }
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(flatSwapchain, &ri);
        return XR_SUCCEEDED(xr);
    }

    static bool DepthFormats(DXGI_FORMAT srcFormat, DXGI_FORMAT dsvFormat,
                             DXGI_FORMAT& textureFormat, DXGI_FORMAT& srvFormat) {
        DXGI_FORMAT f = dsvFormat != DXGI_FORMAT_UNKNOWN ? dsvFormat : srcFormat;
        switch (f) {
            case DXGI_FORMAT_D24_UNORM_S8_UINT:
            case DXGI_FORMAT_R24G8_TYPELESS:
            case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
                textureFormat = DXGI_FORMAT_R24G8_TYPELESS;
                srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                return true;
            case DXGI_FORMAT_D32_FLOAT:
            case DXGI_FORMAT_R32_TYPELESS:
            case DXGI_FORMAT_R32_FLOAT:
                textureFormat = DXGI_FORMAT_R32_TYPELESS;
                srvFormat = DXGI_FORMAT_R32_FLOAT;
                return true;
            case DXGI_FORMAT_D16_UNORM:
            case DXGI_FORMAT_R16_TYPELESS:
            case DXGI_FORMAT_R16_UNORM:
                textureFormat = DXGI_FORMAT_R16_TYPELESS;
                srvFormat = DXGI_FORMAT_R16_UNORM;
                return true;
            case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            case DXGI_FORMAT_R32G8X24_TYPELESS:
            case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
                textureFormat = DXGI_FORMAT_R32G8X24_TYPELESS;
                srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
                return true;
            default:
                return false;
        }
    }

    void ReleaseStereoResources() {
        for (auto*& cb : stereoCB) { if (cb) cb->Release(); cb = nullptr; }
        if (stereoDeferred) { stereoDeferred->Release(); stereoDeferred = nullptr; }
        if (stereoDepthState) { stereoDepthState->Release(); stereoDepthState = nullptr; }
        if (stereoRaster) { stereoRaster->Release(); stereoRaster = nullptr; }
        if (stereoDepthSampler) { stereoDepthSampler->Release(); stereoDepthSampler = nullptr; }
        if (stereoSampler) { stereoSampler->Release(); stereoSampler = nullptr; }
        if (stereoPS) { stereoPS->Release(); stereoPS = nullptr; }
        if (stereoVS) { stereoVS->Release(); stereoVS = nullptr; }
        if (depthSrv) { depthSrv->Release(); depthSrv = nullptr; }
        if (depthCopy) { depthCopy->Release(); depthCopy = nullptr; }
        if (colorSrv) { colorSrv->Release(); colorSrv = nullptr; }
        if (colorCopy) { colorCopy->Release(); colorCopy = nullptr; }
        stereoPipelineReady = false;
    }

    bool EnsureStereoPipeline() {
        if (stereoPipelineReady) return true;
        if (!device) return false;

        using D3DCompileFn = HRESULT (WINAPI*)(LPCVOID, SIZE_T, LPCSTR,
            const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
            UINT, UINT, ID3DBlob**, ID3DBlob**);
        HMODULE compiler = LoadLibraryW((ModuleDir() + L"\\d3dcompiler_46.dll").c_str());
        if (!compiler) compiler = LoadLibraryW(L"d3dcompiler_47.dll");
        auto compile = compiler ? reinterpret_cast<D3DCompileFn>(GetProcAddress(compiler, "D3DCompile")) : nullptr;
        if (!compile) {
            Log("P7A D3DCompile unavailable; depth stereo shader cannot be created.");
            if (compiler) FreeLibrary(compiler);
            return false;
        }

        static const char* shader = R"HLSL(
Texture2D ColorTex : register(t0);
Texture2D DepthTex : register(t1);
SamplerState LinearClamp : register(s0);
SamplerState PointClamp : register(s1);
cbuffer StereoCB : register(b0) {
    float sourceAspect;
    float targetAspect;
    float eyeSign;
    float strength;
    float depthScale;
    float pad0;
    float pad1;
    float pad2;
};
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 p = id == 0 ? float2(-1.0,-1.0) : (id == 1 ? float2(-1.0,3.0) : float2(3.0,-1.0));
    o.pos = float4(p,0.0,1.0);
    o.uv = float2((p.x + 1.0) * 0.5, 1.0 - (p.y + 1.0) * 0.5);
    return o;
}
float2 WithoutCenterReticle(float2 uv) {
    // The native interaction dot is baked into the gameplay color buffer in
    // some levels, so clearing the separately extracted HUD cannot remove it.
    // Radially sample the immediately surrounding scene over only the tiny
    // fixed center mark. Menus use a different presentation path.
    float2 relative=uv-float2(0.5,0.5);
    float2 aspectRelative=float2(relative.x*sourceAspect,relative.y);
    const float radius=0.010;
    float distanceFromCenter=length(aspectRelative);
    if(distanceFromCenter<radius){
        float2 direction=distanceFromCenter>0.00001?aspectRelative/distanceFromCenter:float2(0,-1);
        aspectRelative=direction*radius;
        return float2(0.5+aspectRelative.x/max(sourceAspect,0.001),0.5+aspectRelative.y);
    }
    return uv;
}
float4 PSMain(VSOut i) : SV_Target {
    float2 uv = i.uv;
    float crop = saturate(targetAspect / max(sourceAspect, 0.001));
    uv.x = (uv.x - 0.5) * crop + 0.5;
    // Point sampling prevents depth values from bleeding across object edges.
    float d = DepthTex.SampleLevel(PointClamp, uv, 0).r;
    // UE3/D3D11 uses the usual near=0, far=1 depth convention here. For a
    // perspective buffer, (1-depth) is approximately proportional to 1/z for
    // most of the range, which gives stable binocular disparity without knowing
    // the title's exact near/far clip planes.
    float nearWeight = saturate((1.0 - d) * depthScale);
    float shift = eyeSign * strength * nearWeight * crop;
    float2 suv = float2(saturate(uv.x + shift), saturate(uv.y));
    return ColorTex.SampleLevel(LinearClamp, WithoutCenterReticle(suv), 0);
}
)HLSL";

        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psBlob = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT hr = compile(shader, std::strlen(shader), "Outlast2VR_P7", nullptr, nullptr,
                             "VSMain", "vs_5_0", 0, 0, &vsBlob, &errors);
        if (FAILED(hr) || !vsBlob) {
            Log("P7A VS compile failed: 0x%08X %s", static_cast<unsigned>(hr),
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
            if (errors) errors->Release();
            if (compiler) FreeLibrary(compiler);
            return false;
        }
        if (errors) { errors->Release(); errors = nullptr; }
        hr = compile(shader, std::strlen(shader), "Outlast2VR_P7", nullptr, nullptr,
                     "PSMain", "ps_5_0", 0, 0, &psBlob, &errors);
        if (FAILED(hr) || !psBlob) {
            Log("P7A PS compile failed: 0x%08X %s", static_cast<unsigned>(hr),
                errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
            if (errors) errors->Release();
            vsBlob->Release();
            if (compiler) FreeLibrary(compiler);
            return false;
        }
        if (errors) errors->Release();

        hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &stereoVS);
        if (SUCCEEDED(hr)) hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &stereoPS);
        vsBlob->Release();
        psBlob->Release();
        if (compiler) FreeLibrary(compiler);
        if (FAILED(hr) || !stereoVS || !stereoPS) {
            Log("P7A shader creation failed: 0x%08X", static_cast<unsigned>(hr));
            return false;
        }

        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr = device->CreateSamplerState(&sd, &stereoSampler);
        if (FAILED(hr)) return false;

        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        hr = device->CreateSamplerState(&sd, &stereoDepthSampler);
        if (FAILED(hr)) return false;

        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        hr = device->CreateRasterizerState(&rd, &stereoRaster);
        if (FAILED(hr)) return false;

        D3D11_DEPTH_STENCIL_DESC dsd{};
        dsd.DepthEnable = FALSE;
        dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
        hr = device->CreateDepthStencilState(&dsd, &stereoDepthState);
        if (FAILED(hr)) return false;

        hr = device->CreateDeferredContext(0, &stereoDeferred);
        if (FAILED(hr) || !stereoDeferred) return false;

        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(StereoConstants);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        for (auto*& cb : stereoCB) {
            hr = device->CreateBuffer(&bd, nullptr, &cb);
            if (FAILED(hr)) return false;
        }

        stereoPipelineReady = true;
        Log("P7A depth-stereo shader pipeline ready.");
        return true;
    }

    bool EnsureColorCopy(ID3D11Texture2D* backbuffer) {
        if (colorCopy && colorSrv) return true;
        D3D11_TEXTURE2D_DESC src{};
        backbuffer->GetDesc(&src);
        if (src.SampleDesc.Count != 1) return false;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = src.Width;
        td.Height = src.Height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = src.Format;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device->CreateTexture2D(&td, nullptr, &colorCopy);
        if (FAILED(hr)) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = src.Format;
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sv.Texture2D.MostDetailedMip = 0;
        sv.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(colorCopy, &sv, &colorSrv);
        return SUCCEEDED(hr) && colorSrv;
    }

    bool CaptureSceneDepth() {
        ID3D11Texture2D* srcDepth = nullptr;
        DXGI_FORMAT depthViewFormat = DXGI_FORMAT_UNKNOWN;
        const char* depthSource = "none";

        // First choice: the depth stencil still bound to the output-merger.
        ID3D11RenderTargetView* boundRtv = nullptr;
        ID3D11DepthStencilView* dsv = nullptr;
        context->OMGetRenderTargets(1, &boundRtv, &dsv);
        if (boundRtv) boundRtv->Release();
        if (dsv) {
            D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsv->GetDesc(&dsvDesc);
            depthViewFormat = dsvDesc.Format;
            ID3D11Resource* resource = nullptr;
            dsv->GetResource(&resource);
            dsv->Release();
            if (resource) {
                resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&srcDepth));
                resource->Release();
                if (srcDepth) depthSource = "OM depth-stencil";
            }
        }

        // UE3 often unbinds the DSV for its final post-process pass. In that
        // case the scene depth commonly remains bound as a pixel-shader SRV.
        // Scan the currently bound SRVs for a full-resolution depth-family texture.
        if (!srcDepth) {
            constexpr UINT kSrvSlots = 32;
            ID3D11ShaderResourceView* srvs[kSrvSlots]{};
            context->PSGetShaderResources(0, kSrvSlots, srvs);
            for (UINT i = 0; i < kSrvSlots; ++i) {
                if (!srvs[i]) continue;
                D3D11_SHADER_RESOURCE_VIEW_DESC svDesc{};
                srvs[i]->GetDesc(&svDesc);
                ID3D11Resource* resource = nullptr;
                srvs[i]->GetResource(&resource);
                ID3D11Texture2D* candidate = nullptr;
                if (resource) {
                    resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&candidate));
                    resource->Release();
                }
                if (candidate) {
                    D3D11_TEXTURE2D_DESC cd{};
                    candidate->GetDesc(&cd);
                    DXGI_FORMAT tf = DXGI_FORMAT_UNKNOWN, sf = DXGI_FORMAT_UNKNOWN;
                    const bool depthFamily = DepthFormats(cd.Format, svDesc.Format, tf, sf);
                    const bool sceneSized = cd.Width >= width / 2u && cd.Height >= height / 2u;
                    if (depthFamily && sceneSized && cd.SampleDesc.Count == 1) {
                        srcDepth = candidate;
                        candidate = nullptr;
                        depthViewFormat = svDesc.Format;
                        depthSource = "PS depth SRV";
                    }
                    if (candidate) candidate->Release();
                }
                if (srcDepth) {
                    for (UINT j = i; j < kSrvSlots; ++j) if (srvs[j]) { srvs[j]->Release(); srvs[j] = nullptr; }
                    for (UINT j = 0; j < i; ++j) if (srvs[j]) { srvs[j]->Release(); srvs[j] = nullptr; }
                    break;
                }
            }
            if (!srcDepth) for (auto*& v : srvs) if (v) { v->Release(); v = nullptr; }
        }

        // P7A third choice: the full-resolution DSV captured when UE3 created it.
        // UE3 unbinds scene depth during post-processing, but the resource itself
        // still contains the completed frame's depth data at Present.
        if (!srcDepth) {
            ID3D11DepthStencilView* earlyDsv = nullptr;
            DXGI_FORMAT earlyViewFormat = DXGI_FORMAT_UNKNOWN;
            {
                std::lock_guard<std::mutex> lock(g_earlyDepthMutex);
                if (g_earlyFullResDsv) {
                    g_earlyFullResDsv->AddRef();
                    earlyDsv = g_earlyFullResDsv;
                    earlyViewFormat = g_earlyDepthViewFormat;
                }
            }
            if (earlyDsv) {
                ID3D11Resource* resource = nullptr;
                earlyDsv->GetResource(&resource);
                earlyDsv->Release();
                if (resource) {
                    resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&srcDepth));
                    resource->Release();
                }
                if (srcDepth) {
                    depthViewFormat = earlyViewFormat;
                    depthSource = "early CreateDepthStencilView capture";
                }
            }
        }

        if (!srcDepth) {
            if (!depthUnavailableLogged) {
                depthUnavailableLogged = true;
                Log("P7A scene depth unavailable: no Present DSV/SRV and no early full-res DSV capture. Known-good stereo fallback will be used.");
            }
            return false;
        }

        D3D11_TEXTURE2D_DESC desc{};
        srcDepth->GetDesc(&desc);
        DXGI_FORMAT texFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
        if (desc.SampleDesc.Count != 1 || !DepthFormats(desc.Format, depthViewFormat, texFormat, srvFormat)) {
            if (!depthUnavailableLogged) {
                depthUnavailableLogged = true;
                Log("P7A unsupported scene depth: source=%s %ux%u resourceFmt=%u viewFmt=%u samples=%u.",
                    depthSource, desc.Width, desc.Height, static_cast<unsigned>(desc.Format),
                    static_cast<unsigned>(depthViewFormat), desc.SampleDesc.Count);
            }
            srcDepth->Release();
            return false;
        }

        if (!depthCopy || capturedDepthWidth != desc.Width || capturedDepthHeight != desc.Height || capturedDepthFormat != srvFormat) {
            if (depthSrv) { depthSrv->Release(); depthSrv = nullptr; }
            if (depthCopy) { depthCopy->Release(); depthCopy = nullptr; }
            D3D11_TEXTURE2D_DESC copyDesc{};
            copyDesc.Width = desc.Width;
            copyDesc.Height = desc.Height;
            copyDesc.MipLevels = 1;
            copyDesc.ArraySize = 1;
            copyDesc.Format = texFormat;
            copyDesc.SampleDesc.Count = 1;
            copyDesc.Usage = D3D11_USAGE_DEFAULT;
            copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = device->CreateTexture2D(&copyDesc, nullptr, &depthCopy);
            if (FAILED(hr) || !depthCopy) { srcDepth->Release(); return false; }
            D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
            sv.Format = srvFormat;
            sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sv.Texture2D.MostDetailedMip = 0;
            sv.Texture2D.MipLevels = 1;
            hr = device->CreateShaderResourceView(depthCopy, &sv, &depthSrv);
            if (FAILED(hr) || !depthSrv) { srcDepth->Release(); return false; }
            capturedDepthWidth = desc.Width;
            capturedDepthHeight = desc.Height;
            capturedDepthFormat = srvFormat;
            depthCaptureLogged = false;
        }

        context->CopyResource(depthCopy, srcDepth);
        srcDepth->Release();
        if (!depthCaptureLogged) {
            depthCaptureLogged = true;
            depthUnavailableLogged = false;
            Log("P7A REAL SCENE DEPTH CAPTURED: source=%s %ux%u resourceFmt=%u viewFmt=%u srvFmt=%u. Depth-backed stereo ACTIVE.",
                depthSource, desc.Width, desc.Height, static_cast<unsigned>(desc.Format),
                static_cast<unsigned>(depthViewFormat), static_cast<unsigned>(srvFormat));
        }
        return true;
    }

    bool AcquireAndCopyStereoFallback(ID3D11Texture2D* backbuffer, uint32_t cropWidth) {
        lastFrameDepthStereo = false;
        constexpr uint32_t kFallbackShiftPixels = 14;
        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrResult xr = xrAcquireSwapchainImage(stereoSwapchain, &ai, &idx);
        if (XR_FAILED(xr)) { lastFrameDepthStereo = false; return false; }
        XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wi.timeout = XR_INFINITE_DURATION;
        xr = xrWaitSwapchainImage(stereoSwapchain, &wi);
        if (XR_SUCCEEDED(xr) && idx < stereoImages.size() && stereoImages[idx].texture && backbuffer) {
            ID3D11Texture2D* dst = stereoImages[idx].texture;
            const float black[4] = {0,0,0,1};
            cropWidth = std::clamp<uint32_t>(cropWidth, std::min<uint32_t>(width, 320u), width);
            const uint32_t shift = std::min<uint32_t>(kFallbackShiftPixels, cropWidth / 8u);
            const int64_t centerLeft = static_cast<int64_t>((width - cropWidth) / 2u);
            for (uint32_t eye = 0; eye < 2; ++eye) {
                D3D11_RENDER_TARGET_VIEW_DESC rvd{};
                rvd.Format = format;
                rvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                rvd.Texture2DArray.MipSlice = 0;
                rvd.Texture2DArray.FirstArraySlice = eye;
                rvd.Texture2DArray.ArraySize = 1;
                ID3D11RenderTargetView* rtv = nullptr;
                if (SUCCEEDED(device->CreateRenderTargetView(dst, &rvd, &rtv)) && rtv) {
                    context->ClearRenderTargetView(rtv, black);
                    rtv->Release();
                }
                int64_t srcLeft = centerLeft + (eye == 0 ? -static_cast<int64_t>(shift) : static_cast<int64_t>(shift));
                srcLeft = std::clamp<int64_t>(srcLeft, 0, static_cast<int64_t>(width - cropWidth));
                D3D11_BOX box{};
                box.left = static_cast<UINT>(srcLeft); box.right = static_cast<UINT>(srcLeft) + cropWidth;
                box.top = 0; box.bottom = height; box.front = 0; box.back = 1;
                const UINT dstSubresource = D3D11CalcSubresource(0, eye, 1);
                context->CopySubresourceRegion(dst, dstSubresource, 0, 0, 0, backbuffer, 0, &box);
                // The copy fallback has no pixel shader. Replace only the
                // source-space center dot with the adjacent scene strip.
                constexpr UINT kReticleHalf=10;
                const UINT centerX=width/2u,centerY=height/2u;
                if(centerX>=static_cast<UINT>(srcLeft)+kReticleHalf&&
                   centerX+kReticleHalf<static_cast<UINT>(srcLeft)+cropWidth&&
                   centerY>=kReticleHalf*3&&centerY+kReticleHalf<height){
                    D3D11_BOX patch{};
                    patch.left=centerX-kReticleHalf;patch.right=centerX+kReticleHalf;
                    patch.top=centerY-kReticleHalf*3;patch.bottom=centerY-kReticleHalf;
                    patch.front=0;patch.back=1;
                    context->CopySubresourceRegion(dst,dstSubresource,
                        centerX-kReticleHalf-static_cast<UINT>(srcLeft),centerY-kReticleHalf,0,
                        backbuffer,0,&patch);
                }
            }
        }
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(stereoSwapchain, &ri);
        return XR_SUCCEEDED(xr);
    }

    bool AcquireAndRenderDepthStereo(ID3D11Texture2D* backbuffer, float targetAspect, uint32_t cropWidth) {
        (void)cropWidth;
        if (!EnsureStereoPipeline() || !EnsureColorCopy(backbuffer) || !CaptureSceneDepth())
            return false;

        context->CopyResource(colorCopy, backbuffer);
        lastFrameDepthStereo = true;
        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrResult xr = xrAcquireSwapchainImage(stereoSwapchain, &ai, &idx);
        if (XR_FAILED(xr)) { lastFrameDepthStereo = false; return false; }
        XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wi.timeout = XR_INFINITE_DURATION;
        xr = xrWaitSwapchainImage(stereoSwapchain, &wi);
        if (XR_FAILED(xr) || idx >= stereoImages.size() || !stereoImages[idx].texture) {
            XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xrReleaseSwapchainImage(stereoSwapchain, &ri);
            lastFrameDepthStereo = false;
            return false;
        }

        // Record this pass on a deferred context, then execute it while asking
        // D3D11 to restore the game's immediate-context state.  This avoids the
        // state leaks that made earlier compositor experiments break gameplay.
        stereoDeferred->ClearState();
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0; vp.TopLeftY = 0;
        vp.Width = static_cast<float>(width); vp.Height = static_cast<float>(height);
        vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
        stereoDeferred->RSSetViewports(1, &vp);
        stereoDeferred->RSSetState(stereoRaster);
        stereoDeferred->IASetInputLayout(nullptr);
        stereoDeferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        stereoDeferred->VSSetShader(stereoVS, nullptr, 0);
        stereoDeferred->GSSetShader(nullptr, nullptr, 0);
        stereoDeferred->HSSetShader(nullptr, nullptr, 0);
        stereoDeferred->DSSetShader(nullptr, nullptr, 0);
        stereoDeferred->PSSetShader(stereoPS, nullptr, 0);
        const FLOAT blendFactor[4] = {0,0,0,0};
        stereoDeferred->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFFu);
        stereoDeferred->OMSetDepthStencilState(stereoDepthState, 0);
        ID3D11ShaderResourceView* srvs[2] = {colorSrv, depthSrv};
        stereoDeferred->PSSetShaderResources(0, 2, srvs);
        ID3D11SamplerState* samplers[2] = {stereoSampler, stereoDepthSampler};
        stereoDeferred->PSSetSamplers(0, 2, samplers);

        const float sourceAspect = height ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        ID3D11Texture2D* dst = stereoImages[idx].texture;
        bool bothEyesRendered=true;
        for (uint32_t eye = 0; eye < 2; ++eye) {
            D3D11_RENDER_TARGET_VIEW_DESC rvd{};
            rvd.Format = format;
            rvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rvd.Texture2DArray.MipSlice = 0;
            rvd.Texture2DArray.FirstArraySlice = eye;
            rvd.Texture2DArray.ArraySize = 1;
            ID3D11RenderTargetView* rtv = nullptr;
            const HRESULT rtvResult=device->CreateRenderTargetView(dst,&rvd,&rtv);
            if (FAILED(rtvResult) || !rtv) {
                Log("TS1 eye %u RTV creation failed: 0x%08X",eye,static_cast<unsigned>(rtvResult));
                bothEyesRendered=false;
                continue;
            }
            stereoDeferred->OMSetRenderTargets(1, &rtv, nullptr);

            StereoConstants constants{};
            constants.sourceAspect=sourceAspect;
            constants.targetAspect=targetAspect;
            constants.eyeSign=eye==0?-1.0f:1.0f;
            constants.strength=kDepthStereoStrength;
            constants.depthScale=12.0f;
            stereoDeferred->UpdateSubresource(stereoCB[eye],0,nullptr,&constants,0,0);
            stereoDeferred->PSSetConstantBuffers(0, 1, &stereoCB[eye]);
            stereoDeferred->Draw(3, 0);
            rtv->Release();
        }
        lastFrameDepthStereo=lastFrameDepthStereo&&bothEyesRendered;
        ID3D11ShaderResourceView* nullSrvs[2]{};
        stereoDeferred->PSSetShaderResources(0, 2, nullSrvs);

        ID3D11CommandList* commandList=nullptr;
        const HRESULT finish=stereoDeferred->FinishCommandList(FALSE,&commandList);
        if (SUCCEEDED(finish)&&commandList&&lastFrameDepthStereo)
            context->ExecuteCommandList(commandList,TRUE);
        else {
            Log("TS1 command-list completion failed: hr=0x%08X list=%d bothEyes=%d",
                static_cast<unsigned>(finish),commandList?1:0,bothEyesRendered?1:0);
            lastFrameDepthStereo=false;
        }
        if(commandList)commandList->Release();

        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        const XrResult release=xrReleaseSwapchainImage(stereoSwapchain, &ri);
        if(XR_FAILED(release)){
            failed=true;
            Log("TS1 depth-stereo swapchain release failed: %d",static_cast<int>(release));
            return false;
        }
        return lastFrameDepthStereo;
    }

    bool p36FlatEverReady=false;
    #include "p12_aer.inl"

    #include "p19_poses.inl"
    #include "p37_gui.inl"
    #include "p39_ui.inl"
    #include "p18_actions.inl"
#include "p13_frame.inl"

    void Submit(IDXGISwapChain* realSwapChain) { P13Submit(realSwapChain); }

    void P32DesktopMirror(IDXGISwapChain* realSwapChain) {
        static const bool enabled=GetPrivateProfileIntW(
            L"VR",L"DesktopSingleEye",1,(ModuleDir()+L"\\outlast2_vr_p32.ini").c_str())!=0;
        static bool configLogged=false;
        if(!configLogged){
            configLogged=true;
            Log("P45 DESKTOP MIRROR CONFIG: enabled=%d; mirror is copied after XR submission.",enabled?1:0);
        }
        if(!enabled||!gameplayVr||!context||!realSwapChain)return;
        const uint64_t frame=g_probeFrameCounter.load(std::memory_order_relaxed);
        const int eye=p32::SelectMirrorEye(p12EyeColor[1]&&p12Eyes[1].valid,p12Eyes[1].frame,
                                           p12EyeColor[0]&&p12Eyes[0].valid,p12Eyes[0].frame,frame);
        if(eye<0)return;
        ID3D11Texture2D* backbuffer=nullptr;
        if(FAILED(realSwapChain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&backbuffer)))||!backbuffer)return;
        D3D11_TEXTURE2D_DESC src{},dst{};p12EyeColor[eye]->GetDesc(&src);backbuffer->GetDesc(&dst);
        if(p32::SameMirrorSurface(src.Width,src.Height,unsigned(src.Format),src.ArraySize,src.MipLevels,src.SampleDesc.Count,
                                  dst.Width,dst.Height,unsigned(dst.Format),dst.ArraySize,dst.MipLevels,dst.SampleDesc.Count)){
            if(src.SampleDesc.Count==dst.SampleDesc.Count)context->CopyResource(backbuffer,p12EyeColor[eye]);
            else if(src.SampleDesc.Count>1&&dst.SampleDesc.Count==1)context->ResolveSubresource(backbuffer,0,p12EyeColor[eye],0,src.Format);
        }
        backbuffer->Release();
        static bool logged=false;if(!logged){logged=true;Log("P32 DESKTOP MIRROR: stable single-eye copy enabled; desktop no longer alternates stereo eye frames.");}
    }

};

OpenXRQuad g_xr;

class SwapChainProxy final : public IDXGISwapChain {
public:
    explicit SwapChainProxy(IDXGISwapChain* real) : real_(real) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) ||
            riid == __uuidof(IDXGIDeviceSubObject) || riid == __uuidof(IDXGISwapChain)) {
            *ppvObject = static_cast<IDXGISwapChain*>(this);
            AddRef();
            return S_OK;
        }
        return real_->QueryInterface(riid, ppvObject);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs_;
        if (!r) {
            real_->Release();
            delete this;
        }
        return r;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override { return real_->SetPrivateData(Name, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) override { return real_->SetPrivateDataInterface(Name, pUnknown); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override { return real_->GetPrivateData(Name, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override { return real_->GetParent(riid, ppParent); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppDevice) override { return real_->GetDevice(riid, ppDevice); }
    HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override {
        if (Flags & DXGI_PRESENT_TEST) return real_->Present(SyncInterval, Flags);
        g_probeFrameCounter.fetch_add(1, std::memory_order_relaxed);
        P11CProcessPipelineRebuildRequest();
        P9BReadBackBoundConstantBuffers();
        g_xr.Submit(real_);
        g_xr.P32DesktopMirror(real_);
        return real_->Present(SyncInterval, Flags);
    }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) override { return real_->GetBuffer(Buffer, riid, ppSurface); }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) override { return real_->SetFullscreenState(Fullscreen, pTarget); }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget) override { return real_->GetFullscreenState(pFullscreen, ppTarget); }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) override { return real_->GetDesc(pDesc); }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) override {
        p31bridge::Reset();
        g_p13Backbuffer.store(nullptr);
        g_p13ExtractUi.store(false);
        return real_->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) override { return real_->ResizeTarget(pNewTargetParameters); }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** ppOutput) override { return real_->GetContainingOutput(ppOutput); }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) override { return real_->GetFrameStatistics(pStats); }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* pLastPresentCount) override { return real_->GetLastPresentCount(pLastPresentCount); }

private:
    std::atomic<ULONG> refs_{1};
    IDXGISwapChain* real_ = nullptr; // ownership of caller's original reference transferred here
};

class FactoryProxy final : public IDXGIFactory1 {
public:
    explicit FactoryProxy(IUnknown* realUnknown) {
        realUnknown->QueryInterface(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&real0_));
        realUnknown->QueryInterface(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&real1_));
        realUnknown->Release();
    }

    bool Valid() const { return real0_ != nullptr; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIFactory)) {
            *ppvObject = static_cast<IDXGIFactory*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == __uuidof(IDXGIFactory1) && real1_) {
            *ppvObject = static_cast<IDXGIFactory1*>(this);
            AddRef();
            return S_OK;
        }
        return real0_->QueryInterface(riid, ppvObject);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs_;
        if (!r) {
            if (real1_) real1_->Release();
            if (real0_) real0_->Release();
            delete this;
        }
        return r;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override { return real0_->SetPrivateData(Name, DataSize, pData); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown) override { return real0_->SetPrivateDataInterface(Name, pUnknown); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override { return real0_->GetPrivateData(Name, pDataSize, pData); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override { return real0_->GetParent(riid, ppParent); }
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT Adapter, IDXGIAdapter** ppAdapter) override { return real0_->EnumAdapters(Adapter, ppAdapter); }
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND WindowHandle, UINT Flags) override { return real0_->MakeWindowAssociation(WindowHandle, Flags); }
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND* pWindowHandle) override { return real0_->GetWindowAssociation(pWindowHandle); }
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) override {
        if (!ppSwapChain) return E_POINTER;
        if (pDevice && pDesc) {
            ID3D11Device* d3dDevice = nullptr;
            if (SUCCEEDED(pDevice->QueryInterface(__uuidof(ID3D11Device), reinterpret_cast<void**>(&d3dDevice))) && d3dDevice) {
                InstallDepthStencilCreationHook(d3dDevice, pDesc->BufferDesc.Width, pDesc->BufferDesc.Height);
                ID3D11DeviceContext* immediate = nullptr;
                d3dDevice->GetImmediateContext(&immediate);
                if (immediate) {
                    InstallViewProjectionProbeHooks(immediate);
                    immediate->Release();
                }
                d3dDevice->Release();
            }
        }
        IDXGISwapChain* realSc = nullptr;
        HRESULT hr = real0_->CreateSwapChain(pDevice, pDesc, &realSc);
        if (FAILED(hr) || !realSc) return hr;
        auto* wrapped = new SwapChainProxy(realSc);
        *ppSwapChain = wrapped;
        Log("Wrapped Outlast2 IDXGISwapChain without modifying DXGI vtables. %ux%u fmt=%u",
            pDesc ? pDesc->BufferDesc.Width : 0, pDesc ? pDesc->BufferDesc.Height : 0,
            pDesc ? static_cast<unsigned>(pDesc->BufferDesc.Format) : 0);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter** ppAdapter) override { return real0_->CreateSoftwareAdapter(Module, ppAdapter); }
    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT Adapter, IDXGIAdapter1** ppAdapter) override {
        return real1_ ? real1_->EnumAdapters1(Adapter, ppAdapter) : DXGI_ERROR_UNSUPPORTED;
    }
    BOOL STDMETHODCALLTYPE IsCurrent() override { return real1_ ? real1_->IsCurrent() : TRUE; }

private:
    std::atomic<ULONG> refs_{1};
    IDXGIFactory* real0_ = nullptr;
    IDXGIFactory1* real1_ = nullptr;
};

HRESULT WINAPI HookCreateDXGIFactoryCommon(CreateDXGIFactoryFn realFn, REFIID riid, void** ppFactory, const char* name) {
    if (!realFn) return E_FAIL;
    IUnknown* raw = nullptr;
    HRESULT hr = realFn(riid, reinterpret_cast<void**>(&raw));
    if (FAILED(hr) || !raw || !ppFactory) return hr;

    auto* proxy = new FactoryProxy(raw); // consumes raw reference
    if (!proxy->Valid()) {
        proxy->Release();
        return E_NOINTERFACE;
    }
    HRESULT qhr = proxy->QueryInterface(riid, ppFactory);
    proxy->Release(); // QueryInterface owns caller's reference now
    if (SUCCEEDED(qhr)) {
        if (!g_dxgiInterceptSeen.exchange(true)) Log("Intercepted %s and returned a COM wrapper factory.", name);
        return S_OK;
    }
    return qhr;
}

HRESULT WINAPI HookCreateDXGIFactory(REFIID riid, void** ppFactory) {
    return HookCreateDXGIFactoryCommon(g_realCreateDXGIFactory, riid, ppFactory, "CreateDXGIFactory");
}
HRESULT WINAPI HookCreateDXGIFactory1(REFIID riid, void** ppFactory) {
    return HookCreateDXGIFactoryCommon(g_realCreateDXGIFactory1, riid, ppFactory, "CreateDXGIFactory1");
}

FARPROC WINAPI HookGetProcAddress(HMODULE module, LPCSTR procName) {
    if (!g_realGetProcAddress) return nullptr;
    FARPROC real = g_realGetProcAddress(module, procName);
    if (!procName || reinterpret_cast<uintptr_t>(procName) <= 0xFFFF) return real;

    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(module, modulePath, MAX_PATH);
    const wchar_t* base = wcsrchr(modulePath, L'\\');
    base = base ? base + 1 : modulePath;
    const bool isDxgi = _wcsicmp(base, L"dxgi.dll") == 0;
    if (!isDxgi) return real;

    if (std::strcmp(procName, "CreateDXGIFactory") == 0) {
        g_realCreateDXGIFactory = reinterpret_cast<CreateDXGIFactoryFn>(real);
        Log("Outlast2 resolved DXGI!CreateDXGIFactory; redirecting to safe COM wrapper.");
        return reinterpret_cast<FARPROC>(&HookCreateDXGIFactory);
    }
    if (std::strcmp(procName, "CreateDXGIFactory1") == 0) {
        g_realCreateDXGIFactory1 = reinterpret_cast<CreateDXGIFactoryFn>(real);
        Log("Outlast2 resolved DXGI!CreateDXGIFactory1; redirecting to safe COM wrapper.");
        return reinterpret_cast<FARPROC>(&HookCreateDXGIFactory1);
    }
    return real;
}

bool PatchExeGetProcAddressIAT() {
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) return false;
    auto* base = reinterpret_cast<unsigned char*>(exe);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return false;

    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; desc->Name; ++desc) {
        const char* dllName = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(dllName, "KERNEL32.dll") != 0 && _stricmp(dllName, "KERNELBASE.dll") != 0) continue;

        auto* first = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
        auto* orig = desc->OriginalFirstThunk
            ? reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->OriginalFirstThunk)
            : first;
        for (; orig->u1.AddressOfData; ++orig, ++first) {
            if (IMAGE_SNAP_BY_ORDINAL64(orig->u1.Ordinal)) continue;
            auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + orig->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(byName->Name), "GetProcAddress") != 0) continue;

            DWORD oldProtect = 0;
            if (!VirtualProtect(&first->u1.Function, sizeof(first->u1.Function), PAGE_READWRITE, &oldProtect)) return false;
            g_realGetProcAddress = reinterpret_cast<GetProcAddressFn>(first->u1.Function);
            first->u1.Function = reinterpret_cast<ULONGLONG>(&HookGetProcAddress);
            DWORD ignored = 0;
            VirtualProtect(&first->u1.Function, sizeof(first->u1.Function), oldProtect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), &first->u1.Function, sizeof(first->u1.Function));
            return true;
        }
    }
    return false;
}

} // namespace

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE a, DWORD b, REFIID c, LPVOID* d, LPUNKNOWN e) {
    using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    auto fn = RealDInputProc<Fn>("DirectInput8Create");
    return fn ? fn(a, b, c, d, e) : E_FAIL;
}
extern "C" HRESULT WINAPI DllCanUnloadNow() {
    using Fn = HRESULT (WINAPI*)();
    auto fn = RealDInputProc<Fn>("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID a, REFIID b, LPVOID* c) {
    using Fn = HRESULT (WINAPI*)(REFCLSID, REFIID, LPVOID*);
    auto fn = RealDInputProc<Fn>("DllGetClassObject");
    return fn ? fn(a, b, c) : CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI DllRegisterServer() {
    using Fn = HRESULT (WINAPI*)();
    auto fn = RealDInputProc<Fn>("DllRegisterServer");
    return fn ? fn() : E_NOTIMPL;
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
    using Fn = HRESULT (WINAPI*)();
    auto fn = RealDInputProc<Fn>("DllUnregisterServer");
    return fn ? fn() : E_NOTIMPL;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        // P9 deliberately leaves the failed P6/P8 camera experiments disabled.
        // Use the known-stable P7A/P2 camera bridge while we identify the final renderer matrices.
        g_getProcHookInstalled = PatchExeGetProcAddressIAT();
    } else if (reason == DLL_PROCESS_DETACH) {
        // If somebody dynamically unloads the proxy while the game is still alive,
        // put the renderer's original culling branches back.
        P11ERestoreFrustumCullBypass();
    }
    return TRUE;
}
