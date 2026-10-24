    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { PT pt_; return r->TestCooperativeLevel(); }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override { PT pt_; return r->GetAvailableTextureMem(); }
    HRESULT STDMETHODCALLTYPE ResourceManagerDiscardBytes(DWORD Bytes) override { PT pt_; return r->ResourceManagerDiscardBytes(Bytes); }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE * pMode) override { PT pt_; return r->GetDisplayMode(pMode); }
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS * pParameters) override { PT pt_; return r->GetCreationParameters(pParameters); }
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface8 * pCursorBitmap) override { PT pt_; return r->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap); }
    void STDMETHODCALLTYPE SetCursorPosition(UINT XScreenSpace, UINT YScreenSpace,DWORD Flags) override { PT pt_; return r->SetCursorPosition(XScreenSpace, YScreenSpace, Flags); }
    WINBOOL STDMETHODCALLTYPE ShowCursor(WINBOOL bShow) override { PT pt_; return r->ShowCursor(bShow); }
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS * pPresentationParameters, IDirect3DSwapChain8 ** pSwapChain) override { PT pt_; return r->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain); }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT BackBuffer,D3DBACKBUFFER_TYPE Type,IDirect3DSurface8 ** ppBackBuffer) override { PT pt_; return r->GetBackBuffer(BackBuffer, Type, ppBackBuffer); }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS * pRasterStatus) override { PT pt_; return r->GetRasterStatus(pRasterStatus); }
    void STDMETHODCALLTYPE SetGammaRamp(DWORD flags, const D3DGAMMARAMP *ramp) override { PT pt_; return r->SetGammaRamp(flags, ramp); }
    void STDMETHODCALLTYPE GetGammaRamp(D3DGAMMARAMP * pRamp) override { PT pt_; return r->GetGammaRamp(pRamp); }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture8 ** ppTexture) override { PT pt_; return r->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture); }
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture8 ** ppVolumeTexture) override { PT pt_; return r->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture); }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DCubeTexture8 ** ppCubeTexture) override { PT pt_; return r->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture); }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT Length,DWORD Usage,DWORD FVF,D3DPOOL Pool,IDirect3DVertexBuffer8 ** ppVertexBuffer) override { PT pt_; return r->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer); }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer8 ** ppIndexBuffer) override { PT pt_; return r->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer); }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,WINBOOL Lockable,IDirect3DSurface8 ** ppSurface) override { PT pt_; return r->CreateRenderTarget(Width, Height, Format, MultiSample, Lockable, ppSurface); }
    HRESULT STDMETHODCALLTYPE CreateImageSurface(UINT Width,UINT Height,D3DFORMAT Format,IDirect3DSurface8 ** ppSurface) override { PT pt_; return r->CreateImageSurface(Width, Height, Format, ppSurface); }
    HRESULT STDMETHODCALLTYPE CopyRects(IDirect3DSurface8 *src_surface, const RECT *src_rects, UINT rect_count, IDirect3DSurface8 *dst_surface, const POINT *dst_points) override { PT pt_; return r->CopyRects(src_surface, src_rects, rect_count, dst_surface, dst_points); }
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture8 * pSourceTexture,IDirect3DBaseTexture8 * pDestinationTexture) override { PT pt_; return r->UpdateTexture(pSourceTexture, pDestinationTexture); }
    HRESULT STDMETHODCALLTYPE GetFrontBuffer(IDirect3DSurface8 * pDestSurface) override { PT pt_; return r->GetFrontBuffer(pDestSurface); }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirect3DSurface8 * pRenderTarget,IDirect3DSurface8 * pNewZStencil) override { ppUnbind(); PT pt_; HRESULT h = r->SetRenderTarget(pRenderTarget, pNewZStencil); if (dev9) pp_rtChanged(dev9); return h; }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(IDirect3DSurface8 ** ppRenderTarget) override { PT pt_; return r->GetRenderTarget(ppRenderTarget); }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface8 ** ppZStencilSurface) override { PT pt_; return r->GetDepthStencilSurface(ppZStencilSurface); }
    HRESULT STDMETHODCALLTYPE BeginScene() override { PT pt_; return r->BeginScene(); }
    HRESULT STDMETHODCALLTYPE EndScene() override { PT pt_; return r->EndScene(); }
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL8 *material) override { genC++; PT pt_; return r->SetMaterial(material); }
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL8 *pMaterial) override { PT pt_; return r->GetMaterial(pMaterial); }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD index, const D3DLIGHT8 *light) override { genC++; PT pt_; return r->SetLight(index, light); }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD Index,D3DLIGHT8 * pLight) override { PT pt_; return r->GetLight(Index, pLight); }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index,WINBOOL Enable) override { genC++; PT pt_; return r->LightEnable(Index, Enable); }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index,WINBOOL * pEnable) override { PT pt_; return r->GetLightEnable(Index, pEnable); }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD index, const float *plane) override { PT pt_; return r->SetClipPlane(index, plane); }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index,float * pPlane) override { PT pt_; return r->GetClipPlane(Index, pPlane); }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override { ppUnbind(); PT pt_; return r->BeginStateBlock(); }
    HRESULT STDMETHODCALLTYPE EndStateBlock(DWORD * pToken) override { ppUnbind(); PT pt_; return r->EndStateBlock(pToken); }
    HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD Token) override { ppUnbind(); PT pt_; HRESULT h = r->ApplyStateBlock(Token); reloadShadow(); if (dev9) pp_stateApplied(dev9); return h; }
    HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD Token) override { ppUnbind(); PT pt_; return r->CaptureStateBlock(Token); }
    HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD Token) override { PT pt_; return r->DeleteStateBlock(Token); }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE Type,DWORD * pToken) override { ppUnbind(); PT pt_; return r->CreateStateBlock(Type, pToken); }
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS8 *clip_status) override { PT pt_; return r->SetClipStatus(clip_status); }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS8 * pClipStatus) override { PT pt_; return r->GetClipStatus(pClipStatus); }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage,IDirect3DBaseTexture8 ** ppTexture) override { PT pt_; return r->GetTexture(Stage, ppTexture); }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage,IDirect3DBaseTexture8 * pTexture) override { if (Stage >= 8 || texShadow[Stage] != pTexture) { if (Stage < 8) texShadow[Stage] = pTexture; genK++; } PT pt_; return r->SetTexture(Stage, pTexture); }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage,D3DTEXTURESTAGESTATETYPE Type,DWORD * pValue) override { PT pt_; return r->GetTextureStageState(Stage, Type, pValue); }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage,D3DTEXTURESTAGESTATETYPE Type,DWORD Value) override { if (Stage >= 8 || (DWORD)Type >= 32 || tssShadow[Stage][Type] != Value) { if (Stage < 8 && (DWORD)Type < 32) tssShadow[Stage][Type] = Value; genK++; } PT pt_; return r->SetTextureStageState(Stage, Type, Value); }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD * pNumPasses) override { PT pt_; return r->ValidateDevice(pNumPasses); }
    HRESULT STDMETHODCALLTYPE GetInfo(DWORD DevInfoID,void * pDevInfoStruct,DWORD DevInfoStructSize) override { PT pt_; return r->GetInfo(DevInfoID, pDevInfoStruct, DevInfoStructSize); }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT palette_idx, const PALETTEENTRY *entries) override { PT pt_; return r->SetPaletteEntries(palette_idx, entries); }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT PaletteNumber,PALETTEENTRY * pEntries) override { PT pt_; return r->GetPaletteEntries(PaletteNumber, pEntries); }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT PaletteNumber) override { PT pt_; return r->SetCurrentTexturePalette(PaletteNumber); }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT * PaletteNumber) override { PT pt_; return r->GetCurrentTexturePalette(PaletteNumber); }
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT SrcStartIndex,UINT DestIndex,UINT VertexCount,IDirect3DVertexBuffer8 * pDestBuffer,DWORD Flags) override { PT pt_; return r->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, Flags); }
    HRESULT STDMETHODCALLTYPE GetVertexShader(DWORD * pHandle) override { PT pt_; return r->GetVertexShader(pHandle); }
    HRESULT STDMETHODCALLTYPE DeleteVertexShader(DWORD Handle) override { PT pt_; return r->DeleteVertexShader(Handle); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstant(DWORD Register,void * pConstantData,DWORD ConstantCount) override { PT pt_; return r->GetVertexShaderConstant(Register, pConstantData, ConstantCount); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderDeclaration(DWORD Handle,void * pData,DWORD * pSizeOfData) override { PT pt_; return r->GetVertexShaderDeclaration(Handle, pData, pSizeOfData); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderFunction(DWORD Handle,void * pData,DWORD * pSizeOfData) override { PT pt_; return r->GetVertexShaderFunction(Handle, pData, pSizeOfData); }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT StreamNumber,IDirect3DVertexBuffer8 * pStreamData,UINT Stride) override { if (StreamNumber == 0) curStride = Stride; genK++; PT pt_; return r->SetStreamSource(StreamNumber, pStreamData, Stride); }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT StreamNumber,IDirect3DVertexBuffer8 ** ppStreamData,UINT * pStride) override { PT pt_; return r->GetStreamSource(StreamNumber, ppStreamData, pStride); }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer8 * pIndexData,UINT BaseVertexIndex) override { baseVtx = BaseVertexIndex; PT pt_; return r->SetIndices(pIndexData, BaseVertexIndex); }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer8 ** ppIndexData,UINT * pBaseVertexIndex) override { PT pt_; return r->GetIndices(ppIndexData, pBaseVertexIndex); }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD *byte_code, DWORD *shader) override { PT pt_; return r->CreatePixelShader(byte_code, shader); }
    HRESULT STDMETHODCALLTYPE SetPixelShader(DWORD Handle) override { genK++; PT pt_; return r->SetPixelShader(Handle); }
    HRESULT STDMETHODCALLTYPE GetPixelShader(DWORD * pHandle) override { PT pt_; return r->GetPixelShader(pHandle); }
    HRESULT STDMETHODCALLTYPE DeletePixelShader(DWORD Handle) override { PT pt_; return r->DeletePixelShader(Handle); }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstant(DWORD reg_idx, const void *data, DWORD count) override { PT pt_; return r->SetPixelShaderConstant(reg_idx, data, count); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstant(DWORD Register,void * pConstantData,DWORD ConstantCount) override { PT pt_; return r->GetPixelShaderConstant(Register, pConstantData, ConstantCount); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderFunction(DWORD Handle,void * pData,DWORD * pSizeOfData) override { PT pt_; return r->GetPixelShaderFunction(Handle, pData, pSizeOfData); }
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT handle, const float *segment_count, const D3DRECTPATCH_INFO *patch_info) override { PT pt_; return r->DrawRectPatch(handle, segment_count, patch_info); }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT handle, const float *segment_count, const D3DTRIPATCH_INFO *patch_info) override { PT pt_; return r->DrawTriPatch(handle, segment_count, patch_info); }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) override { PT pt_; return r->DeletePatch(Handle); }
