// P39: P37 camera-capture scanner retired.
// Right grip still tracks the native camera toggle through g_p37CameraActive,
// but no D3D11 render-target inspection runs anymore. P39 draws a tiny custom
// OpenXR camera HUD instead, eliminating the doubled HUD / black-frame path.
std::atomic<bool> g_p37CameraActive{false};
void P37CameraTargetBound(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*){}
ID3D11Texture2D* P37AcquireCameraHud(){return nullptr;}
