#include <windows.h>
#include <JuceHeader.h>

#include <juce_graphics/native/juce_Direct2DMetrics_windows.h>
#include <juce_graphics/native/juce_Direct2DGraphicsContext_windows.h>
#include <juce_graphics/native/juce_Direct2DHwndContext_windows.h>

#include "Direct2DPluginAPI.h"

class Instance;
int nextHandle = 1;
Instance* instances = nullptr;

class Instance
{
public:
    Instance(void *hwndIn) :
        hwnd((HWND)hwndIn),
        handle(nextHandle++),
        context(new Direct2DHwndContext(hwnd, []() {}))
    {
        next = instances;
        instances = this;
    }

    void setOrigin(const Direct2DPluginPoint& origin)
    {
        context->setOrigin({ origin.x, origin.y });
    }

    void addTransform(const Direct2DPluginTransform& transform)
    {
        context->addTransform({ transform.m00, transform.m01, transform.m10, transform.m11, transform.dx, transform.dy });
    }

    void getPhysicalPixelScaleFactor(float& scaleFactor)
    {
        scaleFactor = context->getPhysicalPixelScaleFactor();
    }

    void clipToRectangle(const Direct2DPluginIntRect& rect)
    {
        context->clipToRectangle({ rect.x, rect.y, rect.width, rect.height });
    }

    void clipToRectangleList(const Direct2DPluginIntRectList& rectangles)
    {
        RectangleList<int> list;
        list.ensureStorageAllocated(rectangles.numRectangles);

        for (int i = 0; i < rectangles.numRectangles * 4; i += 4)
        {
            list.addWithoutMerging({ rectangles.rectangles[i], rectangles.rectangles[i + 1], rectangles.rectangles[i + 2], rectangles.rectangles[i + 3] });
        }

        context->clipToRectangleList(list);
    }

    void excludeClipRectangle(const Direct2DPluginIntRect& rect)
    {
        context->excludeClipRectangle({ rect.x, rect.y, rect.width, rect.height });
    }

    void setFlatColourFill(int colour)
    {
        context->setFill(juce::Colour{ (uint32_t)colour });
    }

    void fillIntRect(const Direct2DPluginFillIntRect& rect)
    {
        context->fillRect({ rect.x, rect.y, rect.width, rect.height }, rect.replaceExistingContents);
    }

    void fillFloatRect(const Direct2DPluginFloatRect& rect)
    {
        context->fillRect({ rect.x, rect.y, rect.width, rect.height });
    }

    void fillRectList(const Direct2DPluginFloatRectList& rectangles)
    {
        RectangleList<float> list;
        list.ensureStorageAllocated(rectangles.numRectangles);

        for (int i = 0; i < rectangles.numRectangles * 4; i += 4)
        {
            list.addWithoutMerging({ rectangles.rectangles[i], rectangles.rectangles[i + 1], rectangles.rectangles[i + 2], rectangles.rectangles[i + 3] });
        }

        context->fillRectList(list);
    }

    void execute(Direct2DPluginOp* op)
    {
        switch (op->op)
        {
        case Direct2DPluginOp_setOrigin:
            setOrigin(op->u.point);
            break;

        case Direct2DPluginOp_addTransform:
            addTransform(op->u.transform);
            break;

        case Direct2DPluginOp_getPhysicalPixelScaleFactor:
            getPhysicalPixelScaleFactor(op->u.scaleFactor);
            break;

         case Direct2DPluginOp_clipToRectangle:
             clipToRectangle(op->u.intRect);
             break;

         case Direct2DPluginOp_clipToRectangleList:
             clipToRectangleList(op->u.intRectList);
             break;

         case Direct2DPluginOp_excludeClipRectangle:
             excludeClipRectangle(op->u.intRect);
             break;

         case Direct2DPluginOp_clipToPath:
             break;

         case Direct2DPluginOp_saveState:
             context->saveState();
             break;

         case Direct2DPluginOp_restoreState:
             context->restoreState();
             break;

         case Direct2DPluginOp_beginTransparencyLayer:
             context->beginTransparencyLayer(op->u.scaleFactor);
             break;

         case Direct2DPluginOp_endTransparencyLayer:
             context->endTransparencyLayer();
             break;

         case Direct2DPluginOp_setFlatColourFill:
             setFlatColourFill(op->u.colour);
             break;

         case Direct2DPluginOp_setOpacity:
             context->setOpacity(op->u.opacity);
             break;

         case Direct2DPluginOp_setInterpolationQuality:
             context->setInterpolationQuality((juce::Graphics::ResamplingQuality)op->u.interpolationQuality);
             break;

         case Direct2DPluginOp_fillIntRect:
             fillIntRect(op->u.fillIntRect);
             break;

         case Direct2DPluginOp_fillRectList:
             fillRectList(op->u.floatRectList);
             break;

         case Direct2DPluginOp_drawLine:
             break;

         case Direct2DPluginOp_drawGlyphs:
             break;

         case Direct2DPluginOp_drawLineWithThickness:
             break;

         case Direct2DPluginOp_drawImage:
             break;

         case Direct2DPluginOp_setFont:
             break;

         case Direct2DPluginOp_getFont:
             break;

         case Direct2DPluginOp_fillPath:
             break;

         default:
             break;
        }
    }

    HWND hwnd = nullptr;
    int handle = -1;

    Instance* next = nullptr;
    std::unique_ptr<Direct2DHwndContext> context;
};

int DIRECT2D_PLUGIN_API D2DPlugin_openHwnd(void *hwnd)
{
    return (new Instance{ hwnd })->handle;
}

void D2DPlugin_close(int handle)
{
    if (instances && instances->handle == handle)
    {
        auto temp = instances;
        instances = instances->next;
        delete temp;
        return;
    }

    auto instance = instances;
    while (instance)
    {
        if (instance->next && instance->next->handle == handle)
        {
            auto temp = instance->next;
            instance->next = temp->next;
            delete temp;
            return;
        }

        instance = instance->next;
    }
}

void D2DPlugin_execute(int handle, Direct2DPluginOp* op)
{
    auto instance = instances;
    while (instance)
    {
        if (instance->handle == handle)
        {
            instance->execute(op);
            return;
        }

        instance = instance->next;
    }
}


