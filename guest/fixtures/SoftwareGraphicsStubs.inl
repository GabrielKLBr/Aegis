// Typed COM failure bases. Only overrides in SoftwareGraphicsGuest.cpp are implemented.
// Interface declarations follow the Windows SDK ABI; no native vtable is forwarded.
#pragma warning(push)
#pragma warning(disable:4100)
class Stub_ID2D1Factory:public GraphicsUnknown<ID2D1Factory> {
public:
    Stub_ID2D1Factory():GraphicsUnknown<ID2D1Factory>(1){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(ID2D1Factory));}
    HRESULT STDMETHODCALLTYPE ReloadSystemMetrics() override {return Unsupported(3);}
    void STDMETHODCALLTYPE GetDesktopDpi(FLOAT *dpiX, FLOAT *dpiY) override {Unsupported(4,true);}
    HRESULT STDMETHODCALLTYPE CreateRectangleGeometry(const D2D1_RECT_F *rectangle, ID2D1RectangleGeometry **rectangleGeometry) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE CreateRoundedRectangleGeometry(const D2D1_ROUNDED_RECT *roundedRectangle, ID2D1RoundedRectangleGeometry **roundedRectangleGeometry) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE CreateEllipseGeometry(const D2D1_ELLIPSE *ellipse, ID2D1EllipseGeometry **ellipseGeometry) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE CreateGeometryGroup(D2D1_FILL_MODE fillMode, ID2D1Geometry **geometries, UINT32 geometriesCount, ID2D1GeometryGroup **geometryGroup) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE CreateTransformedGeometry(ID2D1Geometry *sourceGeometry, const D2D1_MATRIX_3X2_F *transform, ID2D1TransformedGeometry **transformedGeometry) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE CreatePathGeometry(ID2D1PathGeometry **pathGeometry) override {return Unsupported(10);}
    HRESULT STDMETHODCALLTYPE CreateStrokeStyle(const D2D1_STROKE_STYLE_PROPERTIES *strokeStyleProperties, const FLOAT *dashes, UINT32 dashesCount, ID2D1StrokeStyle **strokeStyle) override {return Unsupported(11);}
    HRESULT STDMETHODCALLTYPE CreateDrawingStateBlock(const D2D1_DRAWING_STATE_DESCRIPTION *drawingStateDescription, IDWriteRenderingParams *textRenderingParams, ID2D1DrawingStateBlock **drawingStateBlock) override {return Unsupported(12);}
    HRESULT STDMETHODCALLTYPE CreateWicBitmapRenderTarget(IWICBitmap *target, const D2D1_RENDER_TARGET_PROPERTIES *renderTargetProperties, ID2D1RenderTarget **renderTarget) override {return Unsupported(13);}
    HRESULT STDMETHODCALLTYPE CreateHwndRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES *renderTargetProperties, const D2D1_HWND_RENDER_TARGET_PROPERTIES *hwndRenderTargetProperties, ID2D1HwndRenderTarget **hwndRenderTarget) override {return Unsupported(14);}
    HRESULT STDMETHODCALLTYPE CreateDxgiSurfaceRenderTarget(IDXGISurface *dxgiSurface, const D2D1_RENDER_TARGET_PROPERTIES *renderTargetProperties, ID2D1RenderTarget **renderTarget) override {return Unsupported(15);}
    HRESULT STDMETHODCALLTYPE CreateDCRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES *renderTargetProperties, ID2D1DCRenderTarget **dcRenderTarget) override {return Unsupported(16);}
};
class Stub_ID2D1HwndRenderTarget:public GraphicsUnknown<ID2D1HwndRenderTarget> {
public:
    Stub_ID2D1HwndRenderTarget():GraphicsUnknown<ID2D1HwndRenderTarget>(2){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(ID2D1HwndRenderTarget))||Same(iid,__uuidof(ID2D1RenderTarget))||Same(iid,__uuidof(ID2D1Resource));}
    void STDMETHODCALLTYPE GetFactory(ID2D1Factory **factory) const override {Unsupported(3,true);}
    HRESULT STDMETHODCALLTYPE CreateBitmap(D2D1_SIZE_U size, const void *srcData, UINT32 pitch, const D2D1_BITMAP_PROPERTIES *bitmapProperties, ID2D1Bitmap **bitmap) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE CreateBitmapFromWicBitmap(IWICBitmapSource *wicBitmapSource, const D2D1_BITMAP_PROPERTIES *bitmapProperties, ID2D1Bitmap **bitmap) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE CreateSharedBitmap(REFIID riid, void *data, const D2D1_BITMAP_PROPERTIES *bitmapProperties, ID2D1Bitmap **bitmap) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE CreateBitmapBrush(ID2D1Bitmap *bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES *bitmapBrushProperties, const D2D1_BRUSH_PROPERTIES *brushProperties, ID2D1BitmapBrush **bitmapBrush) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE CreateSolidColorBrush(const D2D1_COLOR_F *color, const D2D1_BRUSH_PROPERTIES *brushProperties, ID2D1SolidColorBrush **solidColorBrush) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE CreateGradientStopCollection(const D2D1_GRADIENT_STOP *gradientStops, UINT32 gradientStopsCount, D2D1_GAMMA colorInterpolationGamma, D2D1_EXTEND_MODE extendMode, ID2D1GradientStopCollection **gradientStopCollection) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE CreateLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *linearGradientBrushProperties, const D2D1_BRUSH_PROPERTIES *brushProperties, ID2D1GradientStopCollection *gradientStopCollection, ID2D1LinearGradientBrush **linearGradientBrush) override {return Unsupported(10);}
    HRESULT STDMETHODCALLTYPE CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES *radialGradientBrushProperties, const D2D1_BRUSH_PROPERTIES *brushProperties, ID2D1GradientStopCollection *gradientStopCollection, ID2D1RadialGradientBrush **radialGradientBrush) override {return Unsupported(11);}
    HRESULT STDMETHODCALLTYPE CreateCompatibleRenderTarget(const D2D1_SIZE_F *desiredSize, const D2D1_SIZE_U *desiredPixelSize, const D2D1_PIXEL_FORMAT *desiredFormat, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS options, ID2D1BitmapRenderTarget **bitmapRenderTarget) override {return Unsupported(12);}
    HRESULT STDMETHODCALLTYPE CreateLayer(const D2D1_SIZE_F *size, ID2D1Layer **layer) override {return Unsupported(13);}
    HRESULT STDMETHODCALLTYPE CreateMesh(ID2D1Mesh **mesh) override {return Unsupported(14);}
    void STDMETHODCALLTYPE DrawLine(D2D1_POINT_2F point0, D2D1_POINT_2F point1, ID2D1Brush *brush, FLOAT strokeWidth, ID2D1StrokeStyle *strokeStyle) override {Unsupported(15,true);}
    void STDMETHODCALLTYPE DrawRectangle(const D2D1_RECT_F *rect, ID2D1Brush *brush, FLOAT strokeWidth, ID2D1StrokeStyle *strokeStyle) override {Unsupported(16,true);}
    void STDMETHODCALLTYPE FillRectangle(const D2D1_RECT_F *rect, ID2D1Brush *brush) override {Unsupported(17,true);}
    void STDMETHODCALLTYPE DrawRoundedRectangle(const D2D1_ROUNDED_RECT *roundedRect, ID2D1Brush *brush, FLOAT strokeWidth, ID2D1StrokeStyle *strokeStyle) override {Unsupported(18,true);}
    void STDMETHODCALLTYPE FillRoundedRectangle(const D2D1_ROUNDED_RECT *roundedRect, ID2D1Brush *brush) override {Unsupported(19,true);}
    void STDMETHODCALLTYPE DrawEllipse(const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush, FLOAT strokeWidth, ID2D1StrokeStyle *strokeStyle) override {Unsupported(20,true);}
    void STDMETHODCALLTYPE FillEllipse(const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush) override {Unsupported(21,true);}
    void STDMETHODCALLTYPE DrawGeometry(ID2D1Geometry *geometry, ID2D1Brush *brush, FLOAT strokeWidth, ID2D1StrokeStyle *strokeStyle) override {Unsupported(22,true);}
    void STDMETHODCALLTYPE FillGeometry(ID2D1Geometry *geometry, ID2D1Brush *brush, ID2D1Brush *opacityBrush) override {Unsupported(23,true);}
    void STDMETHODCALLTYPE FillMesh(ID2D1Mesh *mesh, ID2D1Brush *brush) override {Unsupported(24,true);}
    void STDMETHODCALLTYPE FillOpacityMask(ID2D1Bitmap *opacityMask, ID2D1Brush *brush, D2D1_OPACITY_MASK_CONTENT content, const D2D1_RECT_F *destinationRectangle, const D2D1_RECT_F *sourceRectangle) override {Unsupported(25,true);}
    void STDMETHODCALLTYPE DrawBitmap(ID2D1Bitmap *bitmap, const D2D1_RECT_F *destinationRectangle, FLOAT opacity, D2D1_BITMAP_INTERPOLATION_MODE interpolationMode, const D2D1_RECT_F *sourceRectangle) override {Unsupported(26,true);}
    void STDMETHODCALLTYPE DrawText(const WCHAR *string, UINT32 stringLength, IDWriteTextFormat *textFormat, const D2D1_RECT_F *layoutRect, ID2D1Brush *defaultFillBrush, D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE measuringMode) override {Unsupported(27,true);}
    void STDMETHODCALLTYPE DrawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout *textLayout, ID2D1Brush *defaultFillBrush, D2D1_DRAW_TEXT_OPTIONS options) override {Unsupported(28,true);}
    void STDMETHODCALLTYPE DrawGlyphRun(D2D1_POINT_2F baselineOrigin, const DWRITE_GLYPH_RUN *glyphRun, ID2D1Brush *foregroundBrush, DWRITE_MEASURING_MODE measuringMode) override {Unsupported(29,true);}
    void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F *transform) override {Unsupported(30,true);}
    void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F *transform) const override {Unsupported(31,true);}
    void STDMETHODCALLTYPE SetAntialiasMode(D2D1_ANTIALIAS_MODE antialiasMode) override {Unsupported(32,true);}
    D2D1_ANTIALIAS_MODE STDMETHODCALLTYPE GetAntialiasMode() const override {Unsupported(33,true);return {};}
    void STDMETHODCALLTYPE SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE textAntialiasMode) override {Unsupported(34,true);}
    D2D1_TEXT_ANTIALIAS_MODE STDMETHODCALLTYPE GetTextAntialiasMode() const override {Unsupported(35,true);return {};}
    void STDMETHODCALLTYPE SetTextRenderingParams(IDWriteRenderingParams *textRenderingParams) override {Unsupported(36,true);}
    void STDMETHODCALLTYPE GetTextRenderingParams(IDWriteRenderingParams **textRenderingParams) const override {Unsupported(37,true);}
    void STDMETHODCALLTYPE SetTags(D2D1_TAG tag1, D2D1_TAG tag2) override {Unsupported(38,true);}
    void STDMETHODCALLTYPE GetTags(D2D1_TAG *tag1, D2D1_TAG *tag2) const override {Unsupported(39,true);}
    void STDMETHODCALLTYPE PushLayer(const D2D1_LAYER_PARAMETERS *layerParameters, ID2D1Layer *layer) override {Unsupported(40,true);}
    void STDMETHODCALLTYPE PopLayer() override {Unsupported(41,true);}
    HRESULT STDMETHODCALLTYPE Flush(D2D1_TAG *tag1, D2D1_TAG *tag2) override {return Unsupported(42);}
    void STDMETHODCALLTYPE SaveDrawingState(ID2D1DrawingStateBlock *drawingStateBlock) const override {Unsupported(43,true);}
    void STDMETHODCALLTYPE RestoreDrawingState(ID2D1DrawingStateBlock *drawingStateBlock) override {Unsupported(44,true);}
    void STDMETHODCALLTYPE PushAxisAlignedClip(const D2D1_RECT_F *clipRect, D2D1_ANTIALIAS_MODE antialiasMode) override {Unsupported(45,true);}
    void STDMETHODCALLTYPE PopAxisAlignedClip() override {Unsupported(46,true);}
    void STDMETHODCALLTYPE Clear(const D2D1_COLOR_F *clearColor) override {Unsupported(47,true);}
    void STDMETHODCALLTYPE BeginDraw() override {Unsupported(48,true);}
    HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG *tag1, D2D1_TAG *tag2) override {return Unsupported(49);}
    D2D1_PIXEL_FORMAT STDMETHODCALLTYPE GetPixelFormat() const override {Unsupported(50,true);return {};}
    void STDMETHODCALLTYPE SetDpi(FLOAT dpiX, FLOAT dpiY) override {Unsupported(51,true);}
    void STDMETHODCALLTYPE GetDpi(FLOAT *dpiX, FLOAT *dpiY) const override {Unsupported(52,true);}
    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override {Unsupported(53,true);return {};}
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override {Unsupported(54,true);return {};}
    UINT32 STDMETHODCALLTYPE GetMaximumBitmapSize() const override {Unsupported(55,true);return {};}
    BOOL STDMETHODCALLTYPE IsSupported(const D2D1_RENDER_TARGET_PROPERTIES *renderTargetProperties) const override {Unsupported(56,true);return {};}
    D2D1_WINDOW_STATE STDMETHODCALLTYPE CheckWindowState() override {Unsupported(57,true);return {};}
    HRESULT STDMETHODCALLTYPE Resize(const D2D1_SIZE_U *pixelSize) override {return Unsupported(58);}
    HWND STDMETHODCALLTYPE GetHwnd() const override {Unsupported(59,true);return {};}
};
class Stub_ID2D1DCRenderTarget:public GraphicsUnknown<ID2D1DCRenderTarget> {
public:
    Stub_ID2D1DCRenderTarget():GraphicsUnknown<ID2D1DCRenderTarget>(3){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(ID2D1DCRenderTarget))||Same(iid,__uuidof(ID2D1RenderTarget))||Same(iid,__uuidof(ID2D1Resource));}
    void STDMETHODCALLTYPE GetFactory(ID2D1Factory **factory) const override {Unsupported(3,true);}
    HRESULT STDMETHODCALLTYPE CreateBitmap(D2D1_SIZE_U size, const void *srcData, UINT32 pitch, const D2D1_BITMAP_PROPERTIES *bitmapProperties, ID2D1Bitmap **bitmap) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE CreateBitmapFromWicBitmap(IWICBitmapSource *wicBitmapSource, const D2D1_BITMAP_PROPERTIES *bitmapProperties, ID2D1Bitmap **bitmap) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE CreateSharedBitmap(REFIID riid, void *data, const D2D1_BITMAP_PROPERTIES *bitmapProperties, ID2D1Bitmap **bitmap) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE CreateBitmapBrush(ID2D1Bitmap *bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES *bitmapBrushProperties, const D2D1_BRUSH_PROPERTIES *brushProperties, ID2D1BitmapBrush **bitmapBrush) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE CreateSolidColorBrush(const D2D1_COLOR_F *color, const D2D1_BRUSH_PROPERTIES *brushProperties, ID2D1SolidColorBrush **solidColorBrush) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE CreateGradientStopCollection(const D2D1_GRADIENT_STOP *gradientStops, UINT32 gradientStopsCount, D2D1_GAMMA colorInterpolationGamma, D2D1_EXTEND_MODE extendMode, ID2D1GradientStopCollection **gradientStopCollection) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE CreateLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *linearGradientBrushProperties, const D2D1_BRUSH_PROPERTIES *brushProperties, ID2D1GradientStopCollection *gradientStopCollection, ID2D1LinearGradientBrush **linearGradientBrush) override {return Unsupported(10);}
    HRESULT STDMETHODCALLTYPE CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES *radialGradientBrushProperties, const D2D1_BRUSH_PROPERTIES *brushProperties, ID2D1GradientStopCollection *gradientStopCollection, ID2D1RadialGradientBrush **radialGradientBrush) override {return Unsupported(11);}
    HRESULT STDMETHODCALLTYPE CreateCompatibleRenderTarget(const D2D1_SIZE_F *desiredSize, const D2D1_SIZE_U *desiredPixelSize, const D2D1_PIXEL_FORMAT *desiredFormat, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS options, ID2D1BitmapRenderTarget **bitmapRenderTarget) override {return Unsupported(12);}
    HRESULT STDMETHODCALLTYPE CreateLayer(const D2D1_SIZE_F *size, ID2D1Layer **layer) override {return Unsupported(13);}
    HRESULT STDMETHODCALLTYPE CreateMesh(ID2D1Mesh **mesh) override {return Unsupported(14);}
    void STDMETHODCALLTYPE DrawLine(D2D1_POINT_2F point0, D2D1_POINT_2F point1, ID2D1Brush *brush, FLOAT strokeWidth, ID2D1StrokeStyle *strokeStyle) override {Unsupported(15,true);}
    void STDMETHODCALLTYPE DrawRectangle(const D2D1_RECT_F *rect, ID2D1Brush *brush, FLOAT strokeWidth, ID2D1StrokeStyle *strokeStyle) override {Unsupported(16,true);}
    void STDMETHODCALLTYPE FillRectangle(const D2D1_RECT_F *rect, ID2D1Brush *brush) override {Unsupported(17,true);}
    void STDMETHODCALLTYPE DrawRoundedRectangle(const D2D1_ROUNDED_RECT *roundedRect, ID2D1Brush *brush, FLOAT strokeWidth, ID2D1StrokeStyle *strokeStyle) override {Unsupported(18,true);}
    void STDMETHODCALLTYPE FillRoundedRectangle(const D2D1_ROUNDED_RECT *roundedRect, ID2D1Brush *brush) override {Unsupported(19,true);}
    void STDMETHODCALLTYPE DrawEllipse(const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush, FLOAT strokeWidth, ID2D1StrokeStyle *strokeStyle) override {Unsupported(20,true);}
    void STDMETHODCALLTYPE FillEllipse(const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush) override {Unsupported(21,true);}
    void STDMETHODCALLTYPE DrawGeometry(ID2D1Geometry *geometry, ID2D1Brush *brush, FLOAT strokeWidth, ID2D1StrokeStyle *strokeStyle) override {Unsupported(22,true);}
    void STDMETHODCALLTYPE FillGeometry(ID2D1Geometry *geometry, ID2D1Brush *brush, ID2D1Brush *opacityBrush) override {Unsupported(23,true);}
    void STDMETHODCALLTYPE FillMesh(ID2D1Mesh *mesh, ID2D1Brush *brush) override {Unsupported(24,true);}
    void STDMETHODCALLTYPE FillOpacityMask(ID2D1Bitmap *opacityMask, ID2D1Brush *brush, D2D1_OPACITY_MASK_CONTENT content, const D2D1_RECT_F *destinationRectangle, const D2D1_RECT_F *sourceRectangle) override {Unsupported(25,true);}
    void STDMETHODCALLTYPE DrawBitmap(ID2D1Bitmap *bitmap, const D2D1_RECT_F *destinationRectangle, FLOAT opacity, D2D1_BITMAP_INTERPOLATION_MODE interpolationMode, const D2D1_RECT_F *sourceRectangle) override {Unsupported(26,true);}
    void STDMETHODCALLTYPE DrawText(const WCHAR *string, UINT32 stringLength, IDWriteTextFormat *textFormat, const D2D1_RECT_F *layoutRect, ID2D1Brush *defaultFillBrush, D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE measuringMode) override {Unsupported(27,true);}
    void STDMETHODCALLTYPE DrawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout *textLayout, ID2D1Brush *defaultFillBrush, D2D1_DRAW_TEXT_OPTIONS options) override {Unsupported(28,true);}
    void STDMETHODCALLTYPE DrawGlyphRun(D2D1_POINT_2F baselineOrigin, const DWRITE_GLYPH_RUN *glyphRun, ID2D1Brush *foregroundBrush, DWRITE_MEASURING_MODE measuringMode) override {Unsupported(29,true);}
    void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F *transform) override {Unsupported(30,true);}
    void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F *transform) const override {Unsupported(31,true);}
    void STDMETHODCALLTYPE SetAntialiasMode(D2D1_ANTIALIAS_MODE antialiasMode) override {Unsupported(32,true);}
    D2D1_ANTIALIAS_MODE STDMETHODCALLTYPE GetAntialiasMode() const override {Unsupported(33,true);return {};}
    void STDMETHODCALLTYPE SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE textAntialiasMode) override {Unsupported(34,true);}
    D2D1_TEXT_ANTIALIAS_MODE STDMETHODCALLTYPE GetTextAntialiasMode() const override {Unsupported(35,true);return {};}
    void STDMETHODCALLTYPE SetTextRenderingParams(IDWriteRenderingParams *textRenderingParams) override {Unsupported(36,true);}
    void STDMETHODCALLTYPE GetTextRenderingParams(IDWriteRenderingParams **textRenderingParams) const override {Unsupported(37,true);}
    void STDMETHODCALLTYPE SetTags(D2D1_TAG tag1, D2D1_TAG tag2) override {Unsupported(38,true);}
    void STDMETHODCALLTYPE GetTags(D2D1_TAG *tag1, D2D1_TAG *tag2) const override {Unsupported(39,true);}
    void STDMETHODCALLTYPE PushLayer(const D2D1_LAYER_PARAMETERS *layerParameters, ID2D1Layer *layer) override {Unsupported(40,true);}
    void STDMETHODCALLTYPE PopLayer() override {Unsupported(41,true);}
    HRESULT STDMETHODCALLTYPE Flush(D2D1_TAG *tag1, D2D1_TAG *tag2) override {return Unsupported(42);}
    void STDMETHODCALLTYPE SaveDrawingState(ID2D1DrawingStateBlock *drawingStateBlock) const override {Unsupported(43,true);}
    void STDMETHODCALLTYPE RestoreDrawingState(ID2D1DrawingStateBlock *drawingStateBlock) override {Unsupported(44,true);}
    void STDMETHODCALLTYPE PushAxisAlignedClip(const D2D1_RECT_F *clipRect, D2D1_ANTIALIAS_MODE antialiasMode) override {Unsupported(45,true);}
    void STDMETHODCALLTYPE PopAxisAlignedClip() override {Unsupported(46,true);}
    void STDMETHODCALLTYPE Clear(const D2D1_COLOR_F *clearColor) override {Unsupported(47,true);}
    void STDMETHODCALLTYPE BeginDraw() override {Unsupported(48,true);}
    HRESULT STDMETHODCALLTYPE EndDraw(D2D1_TAG *tag1, D2D1_TAG *tag2) override {return Unsupported(49);}
    D2D1_PIXEL_FORMAT STDMETHODCALLTYPE GetPixelFormat() const override {Unsupported(50,true);return {};}
    void STDMETHODCALLTYPE SetDpi(FLOAT dpiX, FLOAT dpiY) override {Unsupported(51,true);}
    void STDMETHODCALLTYPE GetDpi(FLOAT *dpiX, FLOAT *dpiY) const override {Unsupported(52,true);}
    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override {Unsupported(53,true);return {};}
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override {Unsupported(54,true);return {};}
    UINT32 STDMETHODCALLTYPE GetMaximumBitmapSize() const override {Unsupported(55,true);return {};}
    BOOL STDMETHODCALLTYPE IsSupported(const D2D1_RENDER_TARGET_PROPERTIES *renderTargetProperties) const override {Unsupported(56,true);return {};}
    HRESULT STDMETHODCALLTYPE BindDC(const HDC hDC, const RECT *pSubRect) override {return Unsupported(57);}
};
class Stub_ID2D1SolidColorBrush:public GraphicsUnknown<ID2D1SolidColorBrush> {
public:
    Stub_ID2D1SolidColorBrush():GraphicsUnknown<ID2D1SolidColorBrush>(4){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(ID2D1SolidColorBrush))||Same(iid,__uuidof(ID2D1Brush))||Same(iid,__uuidof(ID2D1Resource));}
    void STDMETHODCALLTYPE GetFactory(ID2D1Factory **factory) const override {Unsupported(3,true);}
    void STDMETHODCALLTYPE SetOpacity(FLOAT opacity) override {Unsupported(4,true);}
    void STDMETHODCALLTYPE SetTransform(const D2D1_MATRIX_3X2_F *transform) override {Unsupported(5,true);}
    FLOAT STDMETHODCALLTYPE GetOpacity() const override {Unsupported(6,true);return {};}
    void STDMETHODCALLTYPE GetTransform(D2D1_MATRIX_3X2_F *transform) const override {Unsupported(7,true);}
    void STDMETHODCALLTYPE SetColor(const D2D1_COLOR_F *color) override {Unsupported(8,true);}
    D2D1_COLOR_F STDMETHODCALLTYPE GetColor() const override {Unsupported(9,true);return {};}
};
class Stub_ID2D1Bitmap:public GraphicsUnknown<ID2D1Bitmap> {
public:
    Stub_ID2D1Bitmap():GraphicsUnknown<ID2D1Bitmap>(5){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(ID2D1Bitmap))||Same(iid,__uuidof(ID2D1Image))||Same(iid,__uuidof(ID2D1Resource));}
    void STDMETHODCALLTYPE GetFactory(ID2D1Factory **factory) const override {Unsupported(3,true);}
    D2D1_SIZE_F STDMETHODCALLTYPE GetSize() const override {Unsupported(4,true);return {};}
    D2D1_SIZE_U STDMETHODCALLTYPE GetPixelSize() const override {Unsupported(5,true);return {};}
    D2D1_PIXEL_FORMAT STDMETHODCALLTYPE GetPixelFormat() const override {Unsupported(6,true);return {};}
    void STDMETHODCALLTYPE GetDpi(FLOAT *dpiX, FLOAT *dpiY) const override {Unsupported(7,true);}
    HRESULT STDMETHODCALLTYPE CopyFromBitmap(const D2D1_POINT_2U *destPoint, ID2D1Bitmap *bitmap, const D2D1_RECT_U *srcRect) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE CopyFromRenderTarget(const D2D1_POINT_2U *destPoint, ID2D1RenderTarget *renderTarget, const D2D1_RECT_U *srcRect) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE CopyFromMemory(const D2D1_RECT_U *dstRect, const void *srcData, UINT32 pitch) override {return Unsupported(10);}
};
class Stub_IDWriteFactory:public GraphicsUnknown<IDWriteFactory> {
public:
    Stub_IDWriteFactory():GraphicsUnknown<IDWriteFactory>(6){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(IDWriteFactory));}
    HRESULT STDMETHODCALLTYPE GetSystemFontCollection(IDWriteFontCollection** fontCollection, BOOL checkForUpdates = FALSE) override {return Unsupported(3);}
    HRESULT STDMETHODCALLTYPE CreateCustomFontCollection(IDWriteFontCollectionLoader* collectionLoader, void const* collectionKey, UINT32 collectionKeySize, IDWriteFontCollection** fontCollection) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE RegisterFontCollectionLoader(IDWriteFontCollectionLoader* fontCollectionLoader) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE UnregisterFontCollectionLoader(IDWriteFontCollectionLoader* fontCollectionLoader) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE CreateFontFileReference(WCHAR const* filePath, FILETIME const* lastWriteTime, IDWriteFontFile** fontFile) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE CreateCustomFontFileReference(void const* fontFileReferenceKey, UINT32 fontFileReferenceKeySize, IDWriteFontFileLoader* fontFileLoader, IDWriteFontFile** fontFile) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE CreateFontFace(DWRITE_FONT_FACE_TYPE fontFaceType, UINT32 numberOfFiles, IDWriteFontFile* const* fontFiles, UINT32 faceIndex, DWRITE_FONT_SIMULATIONS fontFaceSimulationFlags, IDWriteFontFace** fontFace) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE CreateRenderingParams(IDWriteRenderingParams** renderingParams) override {return Unsupported(10);}
    HRESULT STDMETHODCALLTYPE CreateMonitorRenderingParams(HMONITOR monitor, IDWriteRenderingParams** renderingParams) override {return Unsupported(11);}
    HRESULT STDMETHODCALLTYPE CreateCustomRenderingParams(FLOAT gamma, FLOAT enhancedContrast, FLOAT clearTypeLevel, DWRITE_PIXEL_GEOMETRY pixelGeometry, DWRITE_RENDERING_MODE renderingMode, IDWriteRenderingParams** renderingParams) override {return Unsupported(12);}
    HRESULT STDMETHODCALLTYPE RegisterFontFileLoader(IDWriteFontFileLoader* fontFileLoader) override {return Unsupported(13);}
    HRESULT STDMETHODCALLTYPE UnregisterFontFileLoader(IDWriteFontFileLoader* fontFileLoader) override {return Unsupported(14);}
    HRESULT STDMETHODCALLTYPE CreateTextFormat(WCHAR const* fontFamilyName, IDWriteFontCollection* fontCollection, DWRITE_FONT_WEIGHT fontWeight, DWRITE_FONT_STYLE fontStyle, DWRITE_FONT_STRETCH fontStretch, FLOAT fontSize, WCHAR const* localeName, IDWriteTextFormat** textFormat) override {return Unsupported(15);}
    HRESULT STDMETHODCALLTYPE CreateTypography(IDWriteTypography** typography) override {return Unsupported(16);}
    HRESULT STDMETHODCALLTYPE GetGdiInterop(IDWriteGdiInterop** gdiInterop) override {return Unsupported(17);}
    HRESULT STDMETHODCALLTYPE CreateTextLayout(WCHAR const* string, UINT32 stringLength, IDWriteTextFormat* textFormat, FLOAT maxWidth, FLOAT maxHeight, IDWriteTextLayout** textLayout) override {return Unsupported(18);}
    HRESULT STDMETHODCALLTYPE CreateGdiCompatibleTextLayout(WCHAR const* string, UINT32 stringLength, IDWriteTextFormat* textFormat, FLOAT layoutWidth, FLOAT layoutHeight, FLOAT pixelsPerDip, DWRITE_MATRIX const* transform, BOOL useGdiNatural, IDWriteTextLayout** textLayout) override {return Unsupported(19);}
    HRESULT STDMETHODCALLTYPE CreateEllipsisTrimmingSign(IDWriteTextFormat* textFormat, IDWriteInlineObject** trimmingSign) override {return Unsupported(20);}
    HRESULT STDMETHODCALLTYPE CreateTextAnalyzer(IDWriteTextAnalyzer** textAnalyzer) override {return Unsupported(21);}
    HRESULT STDMETHODCALLTYPE CreateNumberSubstitution(DWRITE_NUMBER_SUBSTITUTION_METHOD substitutionMethod, WCHAR const* localeName, BOOL ignoreUserOverride, IDWriteNumberSubstitution** numberSubstitution) override {return Unsupported(22);}
    HRESULT STDMETHODCALLTYPE CreateGlyphRunAnalysis(DWRITE_GLYPH_RUN const* glyphRun, FLOAT pixelsPerDip, DWRITE_MATRIX const* transform, DWRITE_RENDERING_MODE renderingMode, DWRITE_MEASURING_MODE measuringMode, FLOAT baselineOriginX, FLOAT baselineOriginY, IDWriteGlyphRunAnalysis** glyphRunAnalysis) override {return Unsupported(23);}
};
class Stub_IDWriteTextFormat:public GraphicsUnknown<IDWriteTextFormat> {
public:
    Stub_IDWriteTextFormat():GraphicsUnknown<IDWriteTextFormat>(7){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(IDWriteTextFormat));}
    HRESULT STDMETHODCALLTYPE SetTextAlignment(DWRITE_TEXT_ALIGNMENT textAlignment) override {return Unsupported(3);}
    HRESULT STDMETHODCALLTYPE SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE SetWordWrapping(DWRITE_WORD_WRAPPING wordWrapping) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE SetReadingDirection(DWRITE_READING_DIRECTION readingDirection) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE SetFlowDirection(DWRITE_FLOW_DIRECTION flowDirection) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE SetIncrementalTabStop(FLOAT incrementalTabStop) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE SetTrimming(DWRITE_TRIMMING const* trimmingOptions, IDWriteInlineObject* trimmingSign) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE SetLineSpacing(DWRITE_LINE_SPACING_METHOD lineSpacingMethod, FLOAT lineSpacing, FLOAT baseline) override {return Unsupported(10);}
    DWRITE_TEXT_ALIGNMENT STDMETHODCALLTYPE GetTextAlignment() override {Unsupported(11,true);return {};}
    DWRITE_PARAGRAPH_ALIGNMENT STDMETHODCALLTYPE GetParagraphAlignment() override {Unsupported(12,true);return {};}
    DWRITE_WORD_WRAPPING STDMETHODCALLTYPE GetWordWrapping() override {Unsupported(13,true);return {};}
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetReadingDirection() override {Unsupported(14,true);return {};}
    DWRITE_FLOW_DIRECTION STDMETHODCALLTYPE GetFlowDirection() override {Unsupported(15,true);return {};}
    FLOAT STDMETHODCALLTYPE GetIncrementalTabStop() override {Unsupported(16,true);return {};}
    HRESULT STDMETHODCALLTYPE GetTrimming(DWRITE_TRIMMING* trimmingOptions, IDWriteInlineObject** trimmingSign) override {return Unsupported(17);}
    HRESULT STDMETHODCALLTYPE GetLineSpacing(DWRITE_LINE_SPACING_METHOD* lineSpacingMethod, FLOAT* lineSpacing, FLOAT* baseline) override {return Unsupported(18);}
    HRESULT STDMETHODCALLTYPE GetFontCollection(IDWriteFontCollection** fontCollection) override {return Unsupported(19);}
    UINT32 STDMETHODCALLTYPE GetFontFamilyNameLength() override {Unsupported(20,true);return {};}
    HRESULT STDMETHODCALLTYPE GetFontFamilyName(WCHAR* fontFamilyName, UINT32 nameSize) override {return Unsupported(21);}
    DWRITE_FONT_WEIGHT STDMETHODCALLTYPE GetFontWeight() override {Unsupported(22,true);return {};}
    DWRITE_FONT_STYLE STDMETHODCALLTYPE GetFontStyle() override {Unsupported(23,true);return {};}
    DWRITE_FONT_STRETCH STDMETHODCALLTYPE GetFontStretch() override {Unsupported(24,true);return {};}
    FLOAT STDMETHODCALLTYPE GetFontSize() override {Unsupported(25,true);return {};}
    UINT32 STDMETHODCALLTYPE GetLocaleNameLength() override {Unsupported(26,true);return {};}
    HRESULT STDMETHODCALLTYPE GetLocaleName(WCHAR* localeName, UINT32 nameSize) override {return Unsupported(27);}
};
class Stub_IWICImagingFactory:public GraphicsUnknown<IWICImagingFactory> {
public:
    Stub_IWICImagingFactory():GraphicsUnknown<IWICImagingFactory>(8){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(IWICImagingFactory));}
    HRESULT STDMETHODCALLTYPE CreateDecoderFromFilename(__RPC__in LPCWSTR wzFilename, __RPC__in_opt const GUID *pguidVendor, DWORD dwDesiredAccess, WICDecodeOptions metadataOptions, __RPC__deref_out_opt IWICBitmapDecoder **ppIDecoder) override {return Unsupported(3);}
    HRESULT STDMETHODCALLTYPE CreateDecoderFromStream(__RPC__in_opt IStream *pIStream, __RPC__in_opt const GUID *pguidVendor, WICDecodeOptions metadataOptions, __RPC__deref_out_opt IWICBitmapDecoder **ppIDecoder) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE CreateDecoderFromFileHandle(ULONG_PTR hFile, __RPC__in_opt const GUID *pguidVendor, WICDecodeOptions metadataOptions, __RPC__deref_out_opt IWICBitmapDecoder **ppIDecoder) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE CreateComponentInfo(__RPC__in REFCLSID clsidComponent, __RPC__deref_out_opt IWICComponentInfo **ppIInfo) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE CreateDecoder(__RPC__in REFGUID guidContainerFormat, __RPC__in_opt const GUID *pguidVendor, __RPC__deref_out_opt IWICBitmapDecoder **ppIDecoder) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE CreateEncoder(__RPC__in REFGUID guidContainerFormat, __RPC__in_opt const GUID *pguidVendor, __RPC__deref_out_opt IWICBitmapEncoder **ppIEncoder) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE CreatePalette(__RPC__deref_out_opt IWICPalette **ppIPalette) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE CreateFormatConverter(__RPC__deref_out_opt IWICFormatConverter **ppIFormatConverter) override {return Unsupported(10);}
    HRESULT STDMETHODCALLTYPE CreateBitmapScaler(__RPC__deref_out_opt IWICBitmapScaler **ppIBitmapScaler) override {return Unsupported(11);}
    HRESULT STDMETHODCALLTYPE CreateBitmapClipper(__RPC__deref_out_opt IWICBitmapClipper **ppIBitmapClipper) override {return Unsupported(12);}
    HRESULT STDMETHODCALLTYPE CreateBitmapFlipRotator(__RPC__deref_out_opt IWICBitmapFlipRotator **ppIBitmapFlipRotator) override {return Unsupported(13);}
    HRESULT STDMETHODCALLTYPE CreateStream(__RPC__deref_out_opt IWICStream **ppIWICStream) override {return Unsupported(14);}
    HRESULT STDMETHODCALLTYPE CreateColorContext(__RPC__deref_out_opt IWICColorContext **ppIWICColorContext) override {return Unsupported(15);}
    HRESULT STDMETHODCALLTYPE CreateColorTransformer(__RPC__deref_out_opt IWICColorTransform **ppIWICColorTransform) override {return Unsupported(16);}
    HRESULT STDMETHODCALLTYPE CreateBitmap(UINT uiWidth, UINT uiHeight, __RPC__in REFWICPixelFormatGUID pixelFormat, WICBitmapCreateCacheOption option, __RPC__deref_out_opt IWICBitmap **ppIBitmap) override {return Unsupported(17);}
    HRESULT STDMETHODCALLTYPE CreateBitmapFromSource(__RPC__in_opt IWICBitmapSource *pIBitmapSource, WICBitmapCreateCacheOption option, __RPC__deref_out_opt IWICBitmap **ppIBitmap) override {return Unsupported(18);}
    HRESULT STDMETHODCALLTYPE CreateBitmapFromSourceRect(__RPC__in_opt IWICBitmapSource *pIBitmapSource, UINT x, UINT y, UINT width, UINT height, __RPC__deref_out_opt IWICBitmap **ppIBitmap) override {return Unsupported(19);}
    HRESULT STDMETHODCALLTYPE CreateBitmapFromMemory(UINT uiWidth, UINT uiHeight, __RPC__in REFWICPixelFormatGUID pixelFormat, UINT cbStride, UINT cbBufferSize, __RPC__in_ecount_full(cbBufferSize) BYTE *pbBuffer, __RPC__deref_out_opt IWICBitmap **ppIBitmap) override {return Unsupported(20);}
    HRESULT STDMETHODCALLTYPE CreateBitmapFromHBITMAP(__RPC__in HBITMAP hBitmap, __RPC__in_opt HPALETTE hPalette, WICBitmapAlphaChannelOption options, __RPC__deref_out_opt IWICBitmap **ppIBitmap) override {return Unsupported(21);}
    HRESULT STDMETHODCALLTYPE CreateBitmapFromHICON(__RPC__in HICON hIcon, __RPC__deref_out_opt IWICBitmap **ppIBitmap) override {return Unsupported(22);}
    HRESULT STDMETHODCALLTYPE CreateComponentEnumerator(DWORD componentTypes, DWORD options, __RPC__deref_out_opt IEnumUnknown **ppIEnumUnknown) override {return Unsupported(23);}
    HRESULT STDMETHODCALLTYPE CreateFastMetadataEncoderFromDecoder(__RPC__in_opt IWICBitmapDecoder *pIDecoder, __RPC__deref_out_opt IWICFastMetadataEncoder **ppIFastEncoder) override {return Unsupported(24);}
    HRESULT STDMETHODCALLTYPE CreateFastMetadataEncoderFromFrameDecode(__RPC__in_opt IWICBitmapFrameDecode *pIFrameDecoder, __RPC__deref_out_opt IWICFastMetadataEncoder **ppIFastEncoder) override {return Unsupported(25);}
    HRESULT STDMETHODCALLTYPE CreateQueryWriter(__RPC__in REFGUID guidMetadataFormat, __RPC__in_opt const GUID *pguidVendor, __RPC__deref_out_opt IWICMetadataQueryWriter **ppIQueryWriter) override {return Unsupported(26);}
    HRESULT STDMETHODCALLTYPE CreateQueryWriterFromReader(__RPC__in_opt IWICMetadataQueryReader *pIQueryReader, __RPC__in_opt const GUID *pguidVendor, __RPC__deref_out_opt IWICMetadataQueryWriter **ppIQueryWriter) override {return Unsupported(27);}
};
class Stub_IWICStream:public GraphicsUnknown<IWICStream> {
public:
    Stub_IWICStream():GraphicsUnknown<IWICStream>(9){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(IWICStream))||Same(iid,__uuidof(IStream))||Same(iid,__uuidof(ISequentialStream));}
    HRESULT STDMETHODCALLTYPE Read(void *pv, ULONG cb, ULONG *pcbRead) override {return Unsupported(3);}
    HRESULT STDMETHODCALLTYPE Write(const void *pv, ULONG cb, ULONG *pcbWritten) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE CopyTo(IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE Commit(DWORD grfCommitFlags) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE Revert(void) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override {return Unsupported(10);}
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType) override {return Unsupported(11);}
    HRESULT STDMETHODCALLTYPE Stat(__RPC__out STATSTG *pstatstg, DWORD grfStatFlag) override {return Unsupported(12);}
    HRESULT STDMETHODCALLTYPE Clone(__RPC__deref_out_opt IStream **ppstm) override {return Unsupported(13);}
    HRESULT STDMETHODCALLTYPE InitializeFromIStream(__RPC__in_opt IStream *pIStream) override {return Unsupported(14);}
    HRESULT STDMETHODCALLTYPE InitializeFromFilename(__RPC__in LPCWSTR wzFileName, DWORD dwDesiredAccess) override {return Unsupported(15);}
    HRESULT STDMETHODCALLTYPE InitializeFromMemory(__RPC__in_ecount_full(cbBufferSize) WICInProcPointer pbBuffer, DWORD cbBufferSize) override {return Unsupported(16);}
    HRESULT STDMETHODCALLTYPE InitializeFromIStreamRegion(__RPC__in_opt IStream *pIStream, ULARGE_INTEGER ulOffset, ULARGE_INTEGER ulMaxSize) override {return Unsupported(17);}
};
class Stub_IWICBitmapDecoder:public GraphicsUnknown<IWICBitmapDecoder> {
public:
    Stub_IWICBitmapDecoder():GraphicsUnknown<IWICBitmapDecoder>(10){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(IWICBitmapDecoder));}
    HRESULT STDMETHODCALLTYPE QueryCapability(__RPC__in_opt IStream *pIStream, __RPC__out DWORD *pdwCapability) override {return Unsupported(3);}
    HRESULT STDMETHODCALLTYPE Initialize(__RPC__in_opt IStream *pIStream, WICDecodeOptions cacheOptions) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE GetContainerFormat(__RPC__out GUID *pguidContainerFormat) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE GetDecoderInfo(__RPC__deref_out_opt IWICBitmapDecoderInfo **ppIDecoderInfo) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE CopyPalette(__RPC__in_opt IWICPalette *pIPalette) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE GetMetadataQueryReader(__RPC__deref_out_opt IWICMetadataQueryReader **ppIMetadataQueryReader) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE GetPreview(__RPC__deref_out_opt IWICBitmapSource **ppIBitmapSource) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE GetColorContexts(UINT cCount, __RPC__inout_ecount_full_opt(cCount) IWICColorContext **ppIColorContexts, __RPC__out UINT *pcActualCount) override {return Unsupported(10);}
    HRESULT STDMETHODCALLTYPE GetThumbnail(__RPC__deref_out_opt IWICBitmapSource **ppIThumbnail) override {return Unsupported(11);}
    HRESULT STDMETHODCALLTYPE GetFrameCount(__RPC__out UINT *pCount) override {return Unsupported(12);}
    HRESULT STDMETHODCALLTYPE GetFrame(UINT index, __RPC__deref_out_opt IWICBitmapFrameDecode **ppIBitmapFrame) override {return Unsupported(13);}
};
class Stub_IWICBitmapFrameDecode:public GraphicsUnknown<IWICBitmapFrameDecode> {
public:
    Stub_IWICBitmapFrameDecode():GraphicsUnknown<IWICBitmapFrameDecode>(11){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(IWICBitmapFrameDecode))||Same(iid,__uuidof(IWICBitmapSource));}
    HRESULT STDMETHODCALLTYPE GetSize(__RPC__out UINT *puiWidth, __RPC__out UINT *puiHeight) override {return Unsupported(3);}
    HRESULT STDMETHODCALLTYPE GetPixelFormat(__RPC__out WICPixelFormatGUID *pPixelFormat) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE GetResolution(__RPC__out double *pDpiX, __RPC__out double *pDpiY) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE CopyPalette(__RPC__in_opt IWICPalette *pIPalette) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE CopyPixels(__RPC__in_opt const WICRect *prc, UINT cbStride, UINT cbBufferSize, __RPC__out_ecount_full(cbBufferSize) BYTE *pbBuffer) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE GetMetadataQueryReader(__RPC__deref_out_opt IWICMetadataQueryReader **ppIMetadataQueryReader) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE GetColorContexts(UINT cCount, __RPC__inout_ecount_full_opt(cCount) IWICColorContext **ppIColorContexts, __RPC__out UINT *pcActualCount) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE GetThumbnail(__RPC__deref_out_opt IWICBitmapSource **ppIThumbnail) override {return Unsupported(10);}
};
class Stub_IWICFormatConverter:public GraphicsUnknown<IWICFormatConverter> {
public:
    Stub_IWICFormatConverter():GraphicsUnknown<IWICFormatConverter>(12){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(IWICFormatConverter))||Same(iid,__uuidof(IWICBitmapSource));}
    HRESULT STDMETHODCALLTYPE GetSize(__RPC__out UINT *puiWidth, __RPC__out UINT *puiHeight) override {return Unsupported(3);}
    HRESULT STDMETHODCALLTYPE GetPixelFormat(__RPC__out WICPixelFormatGUID *pPixelFormat) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE GetResolution(__RPC__out double *pDpiX, __RPC__out double *pDpiY) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE CopyPalette(__RPC__in_opt IWICPalette *pIPalette) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE CopyPixels(__RPC__in_opt const WICRect *prc, UINT cbStride, UINT cbBufferSize, __RPC__out_ecount_full(cbBufferSize) BYTE *pbBuffer) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE Initialize(__RPC__in_opt IWICBitmapSource *pISource, __RPC__in REFWICPixelFormatGUID dstFormat, WICBitmapDitherType dither, __RPC__in_opt IWICPalette *pIPalette, double alphaThresholdPercent, WICBitmapPaletteType paletteTranslate) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE CanConvert(__RPC__in REFWICPixelFormatGUID srcPixelFormat, __RPC__in REFWICPixelFormatGUID dstPixelFormat, __RPC__out BOOL *pfCanConvert) override {return Unsupported(9);}
};
class Stub_ID3D11Device:public GraphicsUnknown<ID3D11Device> {
public:
    Stub_ID3D11Device():GraphicsUnknown<ID3D11Device>(13){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(ID3D11Device));}
    HRESULT STDMETHODCALLTYPE CreateBuffer(const D3D11_BUFFER_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Buffer **ppBuffer) override {return Unsupported(3);}
    HRESULT STDMETHODCALLTYPE CreateTexture1D(const D3D11_TEXTURE1D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture1D **ppTexture1D) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE CreateTexture2D(const D3D11_TEXTURE2D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE CreateTexture3D(const D3D11_TEXTURE3D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture3D **ppTexture3D) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView(ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc, ID3D11ShaderResourceView **ppSRView) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc, ID3D11UnorderedAccessView **ppUAView) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC *pDesc, ID3D11RenderTargetView **ppRTView) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE CreateDepthStencilView(ID3D11Resource *pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc, ID3D11DepthStencilView **ppDepthStencilView) override {return Unsupported(10);}
    HRESULT STDMETHODCALLTYPE CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements, const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, ID3D11InputLayout **ppInputLayout) override {return Unsupported(11);}
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11VertexShader **ppVertexShader) override {return Unsupported(12);}
    HRESULT STDMETHODCALLTYPE CreateGeometryShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11GeometryShader **ppGeometryShader) override {return Unsupported(13);}
    HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(const void *pShaderBytecode, SIZE_T BytecodeLength, const D3D11_SO_DECLARATION_ENTRY *pSODeclaration, UINT NumEntries, const UINT *pBufferStrides, UINT NumStrides, UINT RasterizedStream, ID3D11ClassLinkage *pClassLinkage, ID3D11GeometryShader **ppGeometryShader) override {return Unsupported(14);}
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11PixelShader **ppPixelShader) override {return Unsupported(15);}
    HRESULT STDMETHODCALLTYPE CreateHullShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11HullShader **ppHullShader) override {return Unsupported(16);}
    HRESULT STDMETHODCALLTYPE CreateDomainShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11DomainShader **ppDomainShader) override {return Unsupported(17);}
    HRESULT STDMETHODCALLTYPE CreateComputeShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ID3D11ClassLinkage *pClassLinkage, ID3D11ComputeShader **ppComputeShader) override {return Unsupported(18);}
    HRESULT STDMETHODCALLTYPE CreateClassLinkage(ID3D11ClassLinkage **ppLinkage) override {return Unsupported(19);}
    HRESULT STDMETHODCALLTYPE CreateBlendState(const D3D11_BLEND_DESC *pBlendStateDesc, ID3D11BlendState **ppBlendState) override {return Unsupported(20);}
    HRESULT STDMETHODCALLTYPE CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC *pDepthStencilDesc, ID3D11DepthStencilState **ppDepthStencilState) override {return Unsupported(21);}
    HRESULT STDMETHODCALLTYPE CreateRasterizerState(const D3D11_RASTERIZER_DESC *pRasterizerDesc, ID3D11RasterizerState **ppRasterizerState) override {return Unsupported(22);}
    HRESULT STDMETHODCALLTYPE CreateSamplerState(const D3D11_SAMPLER_DESC *pSamplerDesc, ID3D11SamplerState **ppSamplerState) override {return Unsupported(23);}
    HRESULT STDMETHODCALLTYPE CreateQuery(const D3D11_QUERY_DESC *pQueryDesc, ID3D11Query **ppQuery) override {return Unsupported(24);}
    HRESULT STDMETHODCALLTYPE CreatePredicate(const D3D11_QUERY_DESC *pPredicateDesc, ID3D11Predicate **ppPredicate) override {return Unsupported(25);}
    HRESULT STDMETHODCALLTYPE CreateCounter(const D3D11_COUNTER_DESC *pCounterDesc, ID3D11Counter **ppCounter) override {return Unsupported(26);}
    HRESULT STDMETHODCALLTYPE CreateDeferredContext(UINT ContextFlags, ID3D11DeviceContext **ppDeferredContext) override {return Unsupported(27);}
    HRESULT STDMETHODCALLTYPE OpenSharedResource(HANDLE hResource, REFIID ReturnedInterface, void **ppResource) override {return Unsupported(28);}
    HRESULT STDMETHODCALLTYPE CheckFormatSupport(DXGI_FORMAT Format, UINT *pFormatSupport) override {return Unsupported(29);}
    HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(DXGI_FORMAT Format, UINT SampleCount, UINT *pNumQualityLevels) override {return Unsupported(30);}
    void STDMETHODCALLTYPE CheckCounterInfo(D3D11_COUNTER_INFO *pCounterInfo) override {Unsupported(31,true);}
    HRESULT STDMETHODCALLTYPE CheckCounter(const D3D11_COUNTER_DESC *pDesc, D3D11_COUNTER_TYPE *pType, UINT *pActiveCounters, LPSTR szName, UINT *pNameLength, LPSTR szUnits, UINT *pUnitsLength, LPSTR szDescription, UINT *pDescriptionLength) override {return Unsupported(32);}
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D11_FEATURE Feature, void *pFeatureSupportData, UINT FeatureSupportDataSize) override {return Unsupported(33);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override {return Unsupported(34);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override {return Unsupported(35);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override {return Unsupported(36);}
    D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel(void) override {Unsupported(37,true);return {};}
    UINT STDMETHODCALLTYPE GetCreationFlags(void) override {Unsupported(38,true);return {};}
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason(void) override {return Unsupported(39);}
    void STDMETHODCALLTYPE GetImmediateContext(ID3D11DeviceContext **ppImmediateContext) override {Unsupported(40,true);}
    HRESULT STDMETHODCALLTYPE SetExceptionMode(UINT RaiseFlags) override {return Unsupported(41);}
    UINT STDMETHODCALLTYPE GetExceptionMode(void) override {Unsupported(42,true);return {};}
};
class Stub_ID3D11DeviceContext:public GraphicsUnknown<ID3D11DeviceContext> {
public:
    Stub_ID3D11DeviceContext():GraphicsUnknown<ID3D11DeviceContext>(14){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(ID3D11DeviceContext))||Same(iid,__uuidof(ID3D11DeviceChild));}
    void STDMETHODCALLTYPE GetDevice(ID3D11Device **ppDevice) override {Unsupported(3,true);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override {return Unsupported(6);}
    void STDMETHODCALLTYPE VSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) override {Unsupported(7,true);}
    void STDMETHODCALLTYPE PSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews) override {Unsupported(8,true);}
    void STDMETHODCALLTYPE PSSetShader(ID3D11PixelShader *pPixelShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances) override {Unsupported(9,true);}
    void STDMETHODCALLTYPE PSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers) override {Unsupported(10,true);}
    void STDMETHODCALLTYPE VSSetShader(ID3D11VertexShader *pVertexShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances) override {Unsupported(11,true);}
    void STDMETHODCALLTYPE DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) override {Unsupported(12,true);}
    void STDMETHODCALLTYPE Draw(UINT VertexCount, UINT StartVertexLocation) override {Unsupported(13,true);}
    HRESULT STDMETHODCALLTYPE Map(ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE *pMappedResource) override {return Unsupported(14);}
    void STDMETHODCALLTYPE Unmap(ID3D11Resource *pResource, UINT Subresource) override {Unsupported(15,true);}
    void STDMETHODCALLTYPE PSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) override {Unsupported(16,true);}
    void STDMETHODCALLTYPE IASetInputLayout(ID3D11InputLayout *pInputLayout) override {Unsupported(17,true);}
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppVertexBuffers, const UINT *pStrides, const UINT *pOffsets) override {Unsupported(18,true);}
    void STDMETHODCALLTYPE IASetIndexBuffer(ID3D11Buffer *pIndexBuffer, DXGI_FORMAT Format, UINT Offset) override {Unsupported(19,true);}
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) override {Unsupported(20,true);}
    void STDMETHODCALLTYPE DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) override {Unsupported(21,true);}
    void STDMETHODCALLTYPE GSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) override {Unsupported(22,true);}
    void STDMETHODCALLTYPE GSSetShader(ID3D11GeometryShader *pShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances) override {Unsupported(23,true);}
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology) override {Unsupported(24,true);}
    void STDMETHODCALLTYPE VSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews) override {Unsupported(25,true);}
    void STDMETHODCALLTYPE VSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers) override {Unsupported(26,true);}
    void STDMETHODCALLTYPE Begin(ID3D11Asynchronous *pAsync) override {Unsupported(27,true);}
    void STDMETHODCALLTYPE End(ID3D11Asynchronous *pAsync) override {Unsupported(28,true);}
    HRESULT STDMETHODCALLTYPE GetData(ID3D11Asynchronous *pAsync, void *pData, UINT DataSize, UINT GetDataFlags) override {return Unsupported(29);}
    void STDMETHODCALLTYPE SetPredication(ID3D11Predicate *pPredicate, BOOL PredicateValue) override {Unsupported(30,true);}
    void STDMETHODCALLTYPE GSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews) override {Unsupported(31,true);}
    void STDMETHODCALLTYPE GSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers) override {Unsupported(32,true);}
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView) override {Unsupported(33,true);}
    void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView *const *ppRenderTargetViews, ID3D11DepthStencilView *pDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts) override {Unsupported(34,true);}
    void STDMETHODCALLTYPE OMSetBlendState(ID3D11BlendState *pBlendState, const FLOAT BlendFactor[ 4 ], UINT SampleMask) override {Unsupported(35,true);}
    void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D11DepthStencilState *pDepthStencilState, UINT StencilRef) override {Unsupported(36,true);}
    void STDMETHODCALLTYPE SOSetTargets(UINT NumBuffers, ID3D11Buffer *const *ppSOTargets, const UINT *pOffsets) override {Unsupported(37,true);}
    void STDMETHODCALLTYPE DrawAuto(void) override {Unsupported(38,true);}
    void STDMETHODCALLTYPE DrawIndexedInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs) override {Unsupported(39,true);}
    void STDMETHODCALLTYPE DrawInstancedIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs) override {Unsupported(40,true);}
    void STDMETHODCALLTYPE Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override {Unsupported(41,true);}
    void STDMETHODCALLTYPE DispatchIndirect(ID3D11Buffer *pBufferForArgs, UINT AlignedByteOffsetForArgs) override {Unsupported(42,true);}
    void STDMETHODCALLTYPE RSSetState(ID3D11RasterizerState *pRasterizerState) override {Unsupported(43,true);}
    void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT *pViewports) override {Unsupported(44,true);}
    void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D11_RECT *pRects) override {Unsupported(45,true);}
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox) override {Unsupported(46,true);}
    void STDMETHODCALLTYPE CopyResource(ID3D11Resource *pDstResource, ID3D11Resource *pSrcResource) override {Unsupported(47,true);}
    void STDMETHODCALLTYPE UpdateSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) override {Unsupported(48,true);}
    void STDMETHODCALLTYPE CopyStructureCount(ID3D11Buffer *pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView *pSrcView) override {Unsupported(49,true);}
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D11RenderTargetView *pRenderTargetView, const FLOAT ColorRGBA[ 4 ]) override {Unsupported(50,true);}
    void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView *pUnorderedAccessView, const UINT Values[ 4 ]) override {Unsupported(51,true);}
    void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView *pUnorderedAccessView, const FLOAT Values[ 4 ]) override {Unsupported(52,true);}
    void STDMETHODCALLTYPE ClearDepthStencilView(ID3D11DepthStencilView *pDepthStencilView, UINT ClearFlags, FLOAT Depth, UINT8 Stencil) override {Unsupported(53,true);}
    void STDMETHODCALLTYPE GenerateMips(ID3D11ShaderResourceView *pShaderResourceView) override {Unsupported(54,true);}
    void STDMETHODCALLTYPE SetResourceMinLOD(ID3D11Resource *pResource, FLOAT MinLOD) override {Unsupported(55,true);}
    FLOAT STDMETHODCALLTYPE GetResourceMinLOD(ID3D11Resource *pResource) override {Unsupported(56,true);return {};}
    void STDMETHODCALLTYPE ResolveSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, ID3D11Resource *pSrcResource, UINT SrcSubresource, DXGI_FORMAT Format) override {Unsupported(57,true);}
    void STDMETHODCALLTYPE ExecuteCommandList(ID3D11CommandList *pCommandList, BOOL RestoreContextState) override {Unsupported(58,true);}
    void STDMETHODCALLTYPE HSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews) override {Unsupported(59,true);}
    void STDMETHODCALLTYPE HSSetShader(ID3D11HullShader *pHullShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances) override {Unsupported(60,true);}
    void STDMETHODCALLTYPE HSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers) override {Unsupported(61,true);}
    void STDMETHODCALLTYPE HSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) override {Unsupported(62,true);}
    void STDMETHODCALLTYPE DSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews) override {Unsupported(63,true);}
    void STDMETHODCALLTYPE DSSetShader(ID3D11DomainShader *pDomainShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances) override {Unsupported(64,true);}
    void STDMETHODCALLTYPE DSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers) override {Unsupported(65,true);}
    void STDMETHODCALLTYPE DSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) override {Unsupported(66,true);}
    void STDMETHODCALLTYPE CSSetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView *const *ppShaderResourceViews) override {Unsupported(67,true);}
    void STDMETHODCALLTYPE CSSetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView *const *ppUnorderedAccessViews, const UINT *pUAVInitialCounts) override {Unsupported(68,true);}
    void STDMETHODCALLTYPE CSSetShader(ID3D11ComputeShader *pComputeShader, ID3D11ClassInstance *const *ppClassInstances, UINT NumClassInstances) override {Unsupported(69,true);}
    void STDMETHODCALLTYPE CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState *const *ppSamplers) override {Unsupported(70,true);}
    void STDMETHODCALLTYPE CSSetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer *const *ppConstantBuffers) override {Unsupported(71,true);}
    void STDMETHODCALLTYPE VSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers) override {Unsupported(72,true);}
    void STDMETHODCALLTYPE PSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews) override {Unsupported(73,true);}
    void STDMETHODCALLTYPE PSGetShader(ID3D11PixelShader **ppPixelShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override {Unsupported(74,true);}
    void STDMETHODCALLTYPE PSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers) override {Unsupported(75,true);}
    void STDMETHODCALLTYPE VSGetShader(ID3D11VertexShader **ppVertexShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override {Unsupported(76,true);}
    void STDMETHODCALLTYPE PSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers) override {Unsupported(77,true);}
    void STDMETHODCALLTYPE IAGetInputLayout(ID3D11InputLayout **ppInputLayout) override {Unsupported(78,true);}
    void STDMETHODCALLTYPE IAGetVertexBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppVertexBuffers, UINT *pStrides, UINT *pOffsets) override {Unsupported(79,true);}
    void STDMETHODCALLTYPE IAGetIndexBuffer(ID3D11Buffer **pIndexBuffer, DXGI_FORMAT *Format, UINT *Offset) override {Unsupported(80,true);}
    void STDMETHODCALLTYPE GSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers) override {Unsupported(81,true);}
    void STDMETHODCALLTYPE GSGetShader(ID3D11GeometryShader **ppGeometryShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override {Unsupported(82,true);}
    void STDMETHODCALLTYPE IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY *pTopology) override {Unsupported(83,true);}
    void STDMETHODCALLTYPE VSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews) override {Unsupported(84,true);}
    void STDMETHODCALLTYPE VSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers) override {Unsupported(85,true);}
    void STDMETHODCALLTYPE GetPredication(ID3D11Predicate **ppPredicate, BOOL *pPredicateValue) override {Unsupported(86,true);}
    void STDMETHODCALLTYPE GSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews) override {Unsupported(87,true);}
    void STDMETHODCALLTYPE GSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers) override {Unsupported(88,true);}
    void STDMETHODCALLTYPE OMGetRenderTargets(UINT NumViews, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView) override {Unsupported(89,true);}
    void STDMETHODCALLTYPE OMGetRenderTargetsAndUnorderedAccessViews(UINT NumRTVs, ID3D11RenderTargetView **ppRenderTargetViews, ID3D11DepthStencilView **ppDepthStencilView, UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews) override {Unsupported(90,true);}
    void STDMETHODCALLTYPE OMGetBlendState(ID3D11BlendState **ppBlendState, FLOAT BlendFactor[ 4 ], UINT *pSampleMask) override {Unsupported(91,true);}
    void STDMETHODCALLTYPE OMGetDepthStencilState(ID3D11DepthStencilState **ppDepthStencilState, UINT *pStencilRef) override {Unsupported(92,true);}
    void STDMETHODCALLTYPE SOGetTargets(UINT NumBuffers, ID3D11Buffer **ppSOTargets) override {Unsupported(93,true);}
    void STDMETHODCALLTYPE RSGetState(ID3D11RasterizerState **ppRasterizerState) override {Unsupported(94,true);}
    void STDMETHODCALLTYPE RSGetViewports(UINT *pNumViewports, D3D11_VIEWPORT *pViewports) override {Unsupported(95,true);}
    void STDMETHODCALLTYPE RSGetScissorRects(UINT *pNumRects, D3D11_RECT *pRects) override {Unsupported(96,true);}
    void STDMETHODCALLTYPE HSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews) override {Unsupported(97,true);}
    void STDMETHODCALLTYPE HSGetShader(ID3D11HullShader **ppHullShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override {Unsupported(98,true);}
    void STDMETHODCALLTYPE HSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers) override {Unsupported(99,true);}
    void STDMETHODCALLTYPE HSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers) override {Unsupported(100,true);}
    void STDMETHODCALLTYPE DSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews) override {Unsupported(101,true);}
    void STDMETHODCALLTYPE DSGetShader(ID3D11DomainShader **ppDomainShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override {Unsupported(102,true);}
    void STDMETHODCALLTYPE DSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers) override {Unsupported(103,true);}
    void STDMETHODCALLTYPE DSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers) override {Unsupported(104,true);}
    void STDMETHODCALLTYPE CSGetShaderResources(UINT StartSlot, UINT NumViews, ID3D11ShaderResourceView **ppShaderResourceViews) override {Unsupported(105,true);}
    void STDMETHODCALLTYPE CSGetUnorderedAccessViews(UINT StartSlot, UINT NumUAVs, ID3D11UnorderedAccessView **ppUnorderedAccessViews) override {Unsupported(106,true);}
    void STDMETHODCALLTYPE CSGetShader(ID3D11ComputeShader **ppComputeShader, ID3D11ClassInstance **ppClassInstances, UINT *pNumClassInstances) override {Unsupported(107,true);}
    void STDMETHODCALLTYPE CSGetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState **ppSamplers) override {Unsupported(108,true);}
    void STDMETHODCALLTYPE CSGetConstantBuffers(UINT StartSlot, UINT NumBuffers, ID3D11Buffer **ppConstantBuffers) override {Unsupported(109,true);}
    void STDMETHODCALLTYPE ClearState(void) override {Unsupported(110,true);}
    void STDMETHODCALLTYPE Flush(void) override {Unsupported(111,true);}
    D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE GetType(void) override {Unsupported(112,true);return {};}
    UINT STDMETHODCALLTYPE GetContextFlags(void) override {Unsupported(113,true);return {};}
    HRESULT STDMETHODCALLTYPE FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList **ppCommandList) override {return Unsupported(114);}
};
class Stub_ID3D11Texture2D:public GraphicsUnknown<ID3D11Texture2D> {
public:
    Stub_ID3D11Texture2D():GraphicsUnknown<ID3D11Texture2D>(15){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(ID3D11Texture2D))||Same(iid,__uuidof(ID3D11Resource))||Same(iid,__uuidof(ID3D11DeviceChild));}
    void STDMETHODCALLTYPE GetDevice(ID3D11Device **ppDevice) override {Unsupported(3,true);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override {return Unsupported(6);}
    void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION *pResourceDimension) override {Unsupported(7,true);}
    void STDMETHODCALLTYPE SetEvictionPriority(UINT EvictionPriority) override {Unsupported(8,true);}
    UINT STDMETHODCALLTYPE GetEvictionPriority(void) override {Unsupported(9,true);return {};}
    void STDMETHODCALLTYPE GetDesc(D3D11_TEXTURE2D_DESC *pDesc) override {Unsupported(10,true);}
};
class Stub_ID3D11RenderTargetView:public GraphicsUnknown<ID3D11RenderTargetView> {
public:
    Stub_ID3D11RenderTargetView():GraphicsUnknown<ID3D11RenderTargetView>(16){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(ID3D11RenderTargetView))||Same(iid,__uuidof(ID3D11View))||Same(iid,__uuidof(ID3D11DeviceChild));}
    void STDMETHODCALLTYPE GetDevice(ID3D11Device **ppDevice) override {Unsupported(3,true);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override {return Unsupported(6);}
    void STDMETHODCALLTYPE GetResource(ID3D11Resource **ppResource) override {Unsupported(7,true);}
    void STDMETHODCALLTYPE GetDesc(D3D11_RENDER_TARGET_VIEW_DESC *pDesc) override {Unsupported(8,true);}
};
class Stub_ID3D11Query:public GraphicsUnknown<ID3D11Query> {
public:
    Stub_ID3D11Query():GraphicsUnknown<ID3D11Query>(17){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(ID3D11Query))||Same(iid,__uuidof(ID3D11Asynchronous))||Same(iid,__uuidof(ID3D11DeviceChild));}
    void STDMETHODCALLTYPE GetDevice(ID3D11Device **ppDevice) override {Unsupported(3,true);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override {return Unsupported(6);}
    UINT STDMETHODCALLTYPE GetDataSize(void) override {Unsupported(7,true);return {};}
    void STDMETHODCALLTYPE GetDesc(D3D11_QUERY_DESC *pDesc) override {Unsupported(8,true);}
};
class Stub_IDXGIFactory:public GraphicsUnknown<IDXGIFactory> {
public:
    Stub_IDXGIFactory():GraphicsUnknown<IDXGIFactory>(18){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(IDXGIFactory))||Same(iid,__uuidof(IDXGIObject));}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) override {return Unsupported(3);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT Adapter, IDXGIAdapter **ppAdapter) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND WindowHandle, UINT Flags) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND *pWindowHandle) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain) override {return Unsupported(10);}
    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter **ppAdapter) override {return Unsupported(11);}
};
class Stub_IDXGIAdapter:public GraphicsUnknown<IDXGIAdapter> {
public:
    Stub_IDXGIAdapter():GraphicsUnknown<IDXGIAdapter>(19){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(IDXGIAdapter))||Same(iid,__uuidof(IDXGIObject));}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) override {return Unsupported(3);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE EnumOutputs(UINT Output, IDXGIOutput **ppOutput) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC *pDesc) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID InterfaceName, LARGE_INTEGER *pUMDVersion) override {return Unsupported(9);}
};
class Stub_IDXGIDevice:public GraphicsUnknown<IDXGIDevice> {
public:
    Stub_IDXGIDevice():GraphicsUnknown<IDXGIDevice>(20){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(IDXGIDevice))||Same(iid,__uuidof(IDXGIObject));}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) override {return Unsupported(3);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE GetAdapter(IDXGIAdapter **pAdapter) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE CreateSurface(const DXGI_SURFACE_DESC *pDesc, UINT NumSurfaces, DXGI_USAGE Usage, const DXGI_SHARED_RESOURCE *pSharedResource, IDXGISurface **ppSurface) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE QueryResourceResidency(IUnknown *const *ppResources, DXGI_RESIDENCY *pResidencyStatus, UINT NumResources) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT Priority) override {return Unsupported(10);}
    HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT *pPriority) override {return Unsupported(11);}
};
class Stub_IDXGISwapChain:public GraphicsUnknown<IDXGISwapChain> {
public:
    Stub_IDXGISwapChain():GraphicsUnknown<IDXGISwapChain>(21){}
    bool Accept(REFIID iid) const override {return Same(iid,__uuidof(IDXGISwapChain))||Same(iid,__uuidof(IDXGIDeviceSubObject))||Same(iid,__uuidof(IDXGIObject));}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) override {return Unsupported(3);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) override {return Unsupported(4);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) override {return Unsupported(5);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent) override {return Unsupported(6);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppDevice) override {return Unsupported(7);}
    HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override {return Unsupported(8);}
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void **ppSurface) override {return Unsupported(9);}
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput *pTarget) override {return Unsupported(10);}
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL *pFullscreen, IDXGIOutput **ppTarget) override {return Unsupported(11);}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC *pDesc) override {return Unsupported(12);}
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) override {return Unsupported(13);}
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC *pNewTargetParameters) override {return Unsupported(14);}
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput **ppOutput) override {return Unsupported(15);}
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS *pStats) override {return Unsupported(16);}
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT *pLastPresentCount) override {return Unsupported(17);}
};
#pragma warning(pop)
