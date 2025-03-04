#pragma once

extern "C"
{

    enum
    {
        Direct2DPluginOp_setOrigin,
        Direct2DPluginOp_addTransform,
        Direct2DPluginOp_getPhysicalPixelScaleFactor,
        Direct2DPluginOp_clipToRectangle,
        Direct2DPluginOp_clipToRectangleList,
        Direct2DPluginOp_excludeClipRectangle,
        Direct2DPluginOp_clipToPath,
        Direct2DPluginOp_saveState,
        Direct2DPluginOp_restoreState,
        Direct2DPluginOp_beginTransparencyLayer,
        Direct2DPluginOp_endTransparencyLayer,
        Direct2DPluginOp_setFlatColourFill,
        Direct2DPluginOp_setOpacity,
        Direct2DPluginOp_setInterpolationQuality,
        Direct2DPluginOp_fillIntRect,
        Direct2DPluginOp_fillFloatRect,
        Direct2DPluginOp_fillRectList,
        Direct2DPluginOp_drawLine,
        Direct2DPluginOp_drawGlyphs,
        Direct2DPluginOp_drawLineWithThickness,
        Direct2DPluginOp_drawImage,
        Direct2DPluginOp_setFont,
        Direct2DPluginOp_getFont,
        Direct2DPluginOp_fillPath
    };

    typedef struct Direct2DPluginPoint
    {
        int x, y;
    } Direct2DPluginPoint;

    typedef struct Direct2DPluginIntRect
    {
        int x, y, width, height;
    } Direct2DPluginIntRect;

    typedef struct Direct2DPluginFloatRect
    {
        float x, y, width, height;
    } Direct2DPluginFloatRect;

    typedef struct Direct2DPluginIntRectList
    {
        int const* rectangles;
        int numRectangles;
    } Direct2DPluginIntRectList;

    typedef struct Direct2DPluginFloatRectList
    {
        float const* rectangles;
        int numRectangles;
    } Direct2DPluginFloatRectList;

    typedef struct Direct2DPluginTransform
    {
        float m00, m01, m10, m11, dx, dy;
    } Direct2DPluginTransform;

    typedef struct Direct2DPluginFillIntRect
    {
        int x, y, width, height;
        int replaceExistingContents;
    } Direct2DPluginFillIntRect;

    typedef struct Direct2DPluginOp
    {
        int op;
        union
        {
            Direct2DPluginPoint point;
            Direct2DPluginTransform transform;
            float scaleFactor;
            Direct2DPluginIntRect intRect;
            Direct2DPluginFloatRect floatRect;
            int colour;
            float opacity;
            int interpolationQuality;
            Direct2DPluginFillIntRect fillIntRect;
            Direct2DPluginIntRectList intRectList;
            Direct2DPluginFloatRectList floatRectList;
        } u;
    } Direct2DPluginOp;

#if DIRECT2D_PLUGIN_EXPORTS
#define DIRECT2D_PLUGIN_API __declspec(dllexport)
#else
#define DIRECT2D_PLUGIN_API __declspec(dllimport)
#endif

    int DIRECT2D_PLUGIN_API D2DPlugin_openHwnd(void* hwnd);
    void DIRECT2D_PLUGIN_API D2DPlugin_close(int handle);
    int DIRECT2D_PLUGIN_API D2DPlugin_startFrame(int handle, float dpiScale, bool sizing);
    void DIRECT2D_PLUGIN_API D2DPlugin_endFrame();
    void DIRECT2D_PLUGIN_API D2DPlugin_execute(int handle, Direct2DPluginOp* op);

    void DIRECT2D_PLUGIN_API D2DPlugin_setWindowSize(int handle, int width, int height);

}
