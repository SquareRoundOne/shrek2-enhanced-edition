    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void * pInitializeFunction) override { return r->RegisterSoftwareDevice(pInitializeFunction); }
    UINT STDMETHODCALLTYPE GetAdapterCount() override { return r->GetAdapterCount(); }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER8 * pIdentifier) override { return r->GetAdapterIdentifier(Adapter, Flags, pIdentifier); }
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT Adapter) override { return r->GetAdapterModeCount(Adapter); }
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT Adapter, UINT Mode, D3DDISPLAYMODE * pMode) override { return r->EnumAdapterModes(Adapter, Mode, pMode); }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE * pMode) override { return r->GetAdapterDisplayMode(Adapter, pMode); }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT Adapter, D3DDEVTYPE CheckType, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat, WINBOOL Windowed) override { return r->CheckDeviceType(Adapter, CheckType, DisplayFormat, BackBufferFormat, Windowed); }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) override { return r->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat); }
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, WINBOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType) override { return r->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType); }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) override { return r->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat); }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT Adapter) override { return r->GetAdapterMonitor(Adapter); }
