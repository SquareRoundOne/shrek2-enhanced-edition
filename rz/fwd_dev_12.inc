    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return r->TestCooperativeLevel(); }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override { return r->GetAvailableTextureMem(); }
    HRESULT STDMETHODCALLTYPE ResourceManagerDiscardBytes(DWORD Bytes) override { return r->ResourceManagerDiscardBytes(Bytes); }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE * pMode) override { return r->GetDisplayMode(pMode); }
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS * pParameters) override { return r->GetCreationParameters(pParameters); }
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface8 * pCursorBitmap) override { return r->SetCursorProperties(XHotSpot, YHotSpot, pCursorBitmap); }
    void STDMETHODCALLTYPE SetCursorPosition(UINT XScreenSpace, UINT YScreenSpace,DWORD Flags) override { return r->SetCursorPosition(XScreenSpace, YScreenSpace, Flags); }
    WINBOOL STDMETHODCALLTYPE ShowCursor(WINBOOL bShow) override { return r->ShowCursor(bShow); }
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS * pPresentationParameters, IDirect3DSwapChain8 ** pSwapChain) override { return r->CreateAdditionalSwapChain(pPresentationParameters, pSwapChain); }
    HRESULT STDMETHODCALLTYPE Present(const RECT *src_rect, const RECT *dst_rect, HWND dst_window_override, const RGNDATA *dirty_region) override { return r->Present(src_rect, dst_rect, dst_window_override, dirty_region); }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT BackBuffer,D3DBACKBUFFER_TYPE Type,IDirect3DSurface8 ** ppBackBuffer) override { return r->GetBackBuffer(BackBuffer, Type, ppBackBuffer); }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS * pRasterStatus) override { return r->GetRasterStatus(pRasterStatus); }
    void STDMETHODCALLTYPE SetGammaRamp(DWORD flags, const D3DGAMMARAMP *ramp) override { return r->SetGammaRamp(flags, ramp); }
    void STDMETHODCALLTYPE GetGammaRamp(D3DGAMMARAMP * pRamp) override { return r->GetGammaRamp(pRamp); }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width,UINT Height,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DTexture8 ** ppTexture) override { return r->CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture); }
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width,UINT Height,UINT Depth,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DVolumeTexture8 ** ppVolumeTexture) override { return r->CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture); }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength,UINT Levels,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DCubeTexture8 ** ppCubeTexture) override { return r->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture); }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT Length,DWORD Usage,DWORD FVF,D3DPOOL Pool,IDirect3DVertexBuffer8 ** ppVertexBuffer) override { return r->CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer); }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT Length,DWORD Usage,D3DFORMAT Format,D3DPOOL Pool,IDirect3DIndexBuffer8 ** ppIndexBuffer) override { return r->CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer); }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width,UINT Height,D3DFORMAT Format,D3DMULTISAMPLE_TYPE MultiSample,WINBOOL Lockable,IDirect3DSurface8 ** ppSurface) override { return r->CreateRenderTarget(Width, Height, Format, MultiSample, Lockable, ppSurface); }
    HRESULT STDMETHODCALLTYPE CreateImageSurface(UINT Width,UINT Height,D3DFORMAT Format,IDirect3DSurface8 ** ppSurface) override { return r->CreateImageSurface(Width, Height, Format, ppSurface); }
    HRESULT STDMETHODCALLTYPE CopyRects(IDirect3DSurface8 *src_surface, const RECT *src_rects, UINT rect_count, IDirect3DSurface8 *dst_surface, const POINT *dst_points) override { return r->CopyRects(src_surface, src_rects, rect_count, dst_surface, dst_points); }
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture8 * pSourceTexture,IDirect3DBaseTexture8 * pDestinationTexture) override { return r->UpdateTexture(pSourceTexture, pDestinationTexture); }
    HRESULT STDMETHODCALLTYPE GetFrontBuffer(IDirect3DSurface8 * pDestSurface) override { return r->GetFrontBuffer(pDestSurface); }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirect3DSurface8 * pRenderTarget,IDirect3DSurface8 * pNewZStencil) override { return r->SetRenderTarget(pRenderTarget, pNewZStencil); }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(IDirect3DSurface8 ** ppRenderTarget) override { return r->GetRenderTarget(ppRenderTarget); }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface8 ** ppZStencilSurface) override { return r->GetDepthStencilSurface(ppZStencilSurface); }
    HRESULT STDMETHODCALLTYPE BeginScene() override { return r->BeginScene(); }
    HRESULT STDMETHODCALLTYPE EndScene() override { return r->EndScene(); }
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL8 *material) override { return r->SetMaterial(material); }
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL8 *pMaterial) override { return r->GetMaterial(pMaterial); }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD index, const D3DLIGHT8 *light) override { return r->SetLight(index, light); }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD Index,D3DLIGHT8 * pLight) override { return r->GetLight(Index, pLight); }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index,WINBOOL Enable) override { return r->LightEnable(Index, Enable); }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index,WINBOOL * pEnable) override { return r->GetLightEnable(Index, pEnable); }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD index, const float *plane) override { return r->SetClipPlane(index, plane); }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index,float * pPlane) override { return r->GetClipPlane(Index, pPlane); }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override { return r->BeginStateBlock(); }
    HRESULT STDMETHODCALLTYPE EndStateBlock(DWORD * pToken) override { return r->EndStateBlock(pToken); }
    HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD Token) override { return r->ApplyStateBlock(Token); }
    HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD Token) override { return r->CaptureStateBlock(Token); }
    HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD Token) override { return r->DeleteStateBlock(Token); }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE Type,DWORD * pToken) override { return r->CreateStateBlock(Type, pToken); }
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS8 *clip_status) override { return r->SetClipStatus(clip_status); }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS8 * pClipStatus) override { return r->GetClipStatus(pClipStatus); }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage,IDirect3DBaseTexture8 ** ppTexture) override { return r->GetTexture(Stage, ppTexture); }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage,IDirect3DBaseTexture8 * pTexture) override { return r->SetTexture(Stage, pTexture); }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage,D3DTEXTURESTAGESTATETYPE Type,DWORD * pValue) override { return r->GetTextureStageState(Stage, Type, pValue); }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage,D3DTEXTURESTAGESTATETYPE Type,DWORD Value) override { return r->SetTextureStageState(Stage, Type, Value); }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD * pNumPasses) override { return r->ValidateDevice(pNumPasses); }
    HRESULT STDMETHODCALLTYPE GetInfo(DWORD DevInfoID,void * pDevInfoStruct,DWORD DevInfoStructSize) override { return r->GetInfo(DevInfoID, pDevInfoStruct, DevInfoStructSize); }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT palette_idx, const PALETTEENTRY *entries) override { return r->SetPaletteEntries(palette_idx, entries); }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT PaletteNumber,PALETTEENTRY * pEntries) override { return r->GetPaletteEntries(PaletteNumber, pEntries); }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT PaletteNumber) override { return r->SetCurrentTexturePalette(PaletteNumber); }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT * PaletteNumber) override { return r->GetCurrentTexturePalette(PaletteNumber); }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType,UINT StartVertex,UINT PrimitiveCount) override { return r->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount); }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType,UINT minIndex,UINT NumVertices,UINT startIndex,UINT primCount) override { return r->DrawIndexedPrimitive(PrimitiveType, minIndex, NumVertices, startIndex, primCount); }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, const void *data, UINT stride) override { return r->DrawPrimitiveUP(primitive_type, primitive_count, data, stride); }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT min_vertex_idx, UINT vertex_count, UINT primitive_count, const void *index_data, D3DFORMAT index_format, const void *data, UINT stride) override { return r->DrawIndexedPrimitiveUP(primitive_type, min_vertex_idx, vertex_count, primitive_count, index_data, index_format, data, stride); }
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT SrcStartIndex,UINT DestIndex,UINT VertexCount,IDirect3DVertexBuffer8 * pDestBuffer,DWORD Flags) override { return r->ProcessVertices(SrcStartIndex, DestIndex, VertexCount, pDestBuffer, Flags); }
    HRESULT STDMETHODCALLTYPE GetVertexShader(DWORD * pHandle) override { return r->GetVertexShader(pHandle); }
    HRESULT STDMETHODCALLTYPE DeleteVertexShader(DWORD Handle) override { return r->DeleteVertexShader(Handle); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstant(DWORD Register,void * pConstantData,DWORD ConstantCount) override { return r->GetVertexShaderConstant(Register, pConstantData, ConstantCount); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderDeclaration(DWORD Handle,void * pData,DWORD * pSizeOfData) override { return r->GetVertexShaderDeclaration(Handle, pData, pSizeOfData); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderFunction(DWORD Handle,void * pData,DWORD * pSizeOfData) override { return r->GetVertexShaderFunction(Handle, pData, pSizeOfData); }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT StreamNumber,IDirect3DVertexBuffer8 * pStreamData,UINT Stride) override { return r->SetStreamSource(StreamNumber, pStreamData, Stride); }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT StreamNumber,IDirect3DVertexBuffer8 ** ppStreamData,UINT * pStride) override { return r->GetStreamSource(StreamNumber, ppStreamData, pStride); }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer8 * pIndexData,UINT BaseVertexIndex) override { return r->SetIndices(pIndexData, BaseVertexIndex); }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer8 ** ppIndexData,UINT * pBaseVertexIndex) override { return r->GetIndices(ppIndexData, pBaseVertexIndex); }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD *byte_code, DWORD *shader) override { return r->CreatePixelShader(byte_code, shader); }
    HRESULT STDMETHODCALLTYPE SetPixelShader(DWORD Handle) override { return r->SetPixelShader(Handle); }
    HRESULT STDMETHODCALLTYPE GetPixelShader(DWORD * pHandle) override { return r->GetPixelShader(pHandle); }
    HRESULT STDMETHODCALLTYPE DeletePixelShader(DWORD Handle) override { return r->DeletePixelShader(Handle); }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstant(DWORD reg_idx, const void *data, DWORD count) override { return r->SetPixelShaderConstant(reg_idx, data, count); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstant(DWORD Register,void * pConstantData,DWORD ConstantCount) override { return r->GetPixelShaderConstant(Register, pConstantData, ConstantCount); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderFunction(DWORD Handle,void * pData,DWORD * pSizeOfData) override { return r->GetPixelShaderFunction(Handle, pData, pSizeOfData); }
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT handle, const float *segment_count, const D3DRECTPATCH_INFO *patch_info) override { return r->DrawRectPatch(handle, segment_count, patch_info); }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT handle, const float *segment_count, const D3DTRIPATCH_INFO *patch_info) override { return r->DrawTriPatch(handle, segment_count, patch_info); }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) override { return r->DeletePatch(Handle); }
