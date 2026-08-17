#pragma once

#include <hal/hal.h>
#include "render/atlas_renderer.h"

class HalGraphics : public solar::IGraphics {
public:
    HalGraphics() : canvas(GetHAL().canvas) {}

    void clear(uint16_t color) override
    {
        canvas.fillScreen(color);
    }

    void drawPixel(int x, int y, uint16_t color) override
    {
        canvas.drawPixel(x, y, color);
    }

    void drawLine(int x0, int y0, int x1, int y1, uint16_t color) override
    {
        canvas.drawLine(x0, y0, x1, y1, color);
    }

    void drawCircle(int x, int y, int radius, uint16_t color) override
    {
        canvas.drawCircle(x, y, radius, color);
    }

    void fillCircle(int x, int y, int radius, uint16_t color) override
    {
        canvas.fillCircle(x, y, radius, color);
    }

    void drawString(int x, int y, const char* str, uint16_t color) override
    {
        canvas.setTextColor(color);
        canvas.drawString(str, x, y);
    }

    int fontHeight() override
    {
        return canvas.fontHeight();
    }

    int textWidth(const char* str) override
    {
        return canvas.textWidth(str);
    }

private:
    LGFX_Sprite& canvas;
};
