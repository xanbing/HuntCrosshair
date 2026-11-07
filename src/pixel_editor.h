#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "core.h"

namespace hc {
class PixelEditorModel {
public:
    explicit PixelEditorModel(const PixelArt& art);
    const PixelArt& GetArt() const { return art_; }
    bool SetCanvasSize(int width, int height);
    void BeginStroke();
    bool PaintCell(int x, int y, uint8_t alpha=255);
    bool PaintLine(int x0, int y0, int x1, int y1, uint8_t alpha=255);
    bool EndStroke();
    bool Undo();
    bool Redo();
    bool Clear();
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
private:
    PixelArt art_, strokeStart_;
    std::vector<PixelArt> undo_, redo_;
    bool stroke_=false, changed_=false;
    void Commit(const PixelArt& before);
};

// Modal, owned, DPI-aware. Work is copied; Cancel leaves art unchanged.
bool EditPixelArt(HWND owner, HINSTANCE instance, PixelArt& art,
                  Color color, const wchar_t* title);
}
