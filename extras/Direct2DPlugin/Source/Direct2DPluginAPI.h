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
    Direct2DPluginOp_fillRect,
    Direct2DPluginOp_fillRectList,
    Direct2DPluginOp_drawLine,
    Direct2DPluginOp_drawGlyphs,
    Direct2DPluginOp_drawLineWithThickness,
    Direct2DPluginOp_drawImage,
    Direct2DPluginOp_setFont,
    Direct2DPluginOp_getFont,
    Direct2DPluginOp_fillPath
};

typedef struct Direct2DPluginOrigin
{
    int x, y;
} Direct2DPluginOrigin;

typedef struct Direct2DPluginTransform
{
    float m00, m01, m10, m11, dx, dy;
} Direct2DPluginTransform;;

typedef struct Direct2DPluginOp
{
    int op;
    union
    {
        Direct2DPluginOrigin origin;
        Direct2DPluginTransform transform;
    } u;
} Direct2DPluginOp;

#if DIRECT2D_PLUGIN_EXPORTS
#define DIRECT2D_PLUGIN_API __declspec(dllexport)
#else
#define DIRECT2D_PLUGIN_API __declspec(dllimport)
#endif

int DIRECT2D_PLUGIN_API D2DPlugin_open();
void DIRECT2D_PLUGIN_API D2DPlugin_close(int handle);

void DIRECT2D_PLUGIN_API D2DPlugin_setWindowSize(int handle, int width, int height);

void DIRECT2D_PLUGIN_API D2DPlugin_setOrigin(int handle, int x, int y);
void DIRECT2D_PLUGIN_API D2DPlugin_addTransform(int handle, float m00, float m01, float m10, float m11, float dx, float dy);
float DIRECT2D_PLUGIN_API D2DPlugin_getPhysicalPixelScaleFactor(int handle);
bool DIRECT2D_PLUGIN_API D2DPlugin_clipToRectangle(int handle, int x, int y, int width, int height);
bool DIRECT2D_PLUGIN_API D2DPlugin_clipToRectangleList(int handle, int const* rectangles, int numRectangles);
void DIRECT2D_PLUGIN_API D2DPlugin_excludeClipRectangle(int handle, int x, int y, int width, int height);
void DIRECT2D_PLUGIN_API D2DPlugin_clipToPath(int handle, float const * markers, int numMarkers, float m00, float m01, float m10, float m11, float dx, float dy);

void DIRECT2D_PLUGIN_API D2DPlugin_saveState(int handle);
void DIRECT2D_PLUGIN_API D2DPlugin_restoreState(int handle);
void DIRECT2D_PLUGIN_API D2DPlugin_beginTransparencyLayer(int handle, float opacity);
void DIRECT2D_PLUGIN_API D2DPlugin_endTransparencyLayer(int handle);

void DIRECT2D_PLUGIN_API D2DPlugin_setFlatColourFill(int handle, int colour);
void DIRECT2D_PLUGIN_API D2DPlugin_setOpacity(int handle, float opacity);
void DIRECT2D_PLUGIN_API D2DPlugin_setInterpolationQuality(int handle, int quality);

void DIRECT2D_PLUGIN_API D2DPlugin_fillRect(int handle, int x, int y, int width, int height, bool replaceExistingContents);
void DIRECT2D_PLUGIN_API D2DPlugin_fillRectList(int handle, int const* rectangles, int numRectangles);

void DIRECT2D_PLUGIN_API D2DPlugin_drawLine(int handle, float x1, float y1, float x2, float y2);
void DIRECT2D_PLUGIN_API D2DPlugin_drawGlyphs(uint16_t const* glyphs, float const* positions, int numGlyphs, float m00, float m01, float m10, float m11, float dx, float dy);
void DIRECT2D_PLUGIN_API D2DPlugin_drawLineWithThickness(int handle, float x1, float y1, float x2, float y2, float thickness);
void DIRECT2D_PLUGIN_API D2DPlugin_drawImage(int handle, void* image, float m00, float m01, float m10, float m11, float dx, float dy);

void DIRECT2D_PLUGIN_API D2DPlugin_setFont(int handle, const char* typefaceName, float height, int styleFlags);
void DIRECT2D_PLUGIN_API D2DPlugin_getFont(int handle, char* typefaceName, float* height, int* styleFlags);

void DIRECT2D_PLUGIN_API D2DPlugin_fillPath(int handle, float const* markers, int numMarkers, float m00, float m01, float m10, float m11, float dx, float dy);

}