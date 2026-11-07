#include "pixel_editor.h"
#include <cassert>
#include <algorithm>
#include <climits>
#include <cstdio>

static int Filled(const hc::PixelArt& a){return(int)std::count_if(a.pixels.begin(),a.pixels.end(),[](uint8_t p){return p!=0;});}
int main(){
    hc::PixelArt source;hc::PixelEditorModel model(source);
    assert(model.GetArt().width==20&&model.GetArt().height==20&&!model.CanUndo());
    model.BeginStroke();assert(model.PaintLine(0,0,19,19));assert(model.EndStroke());
    assert(Filled(model.GetArt())==20&&Filled(source)==0);
    for(int i=0;i<20;i++)assert(model.GetArt().pixels[i*20+i]==255);
    assert(model.Undo()&&Filled(model.GetArt())==0&&model.CanRedo());
    model.BeginStroke();assert(!model.PaintCell(0,0,0));assert(!model.EndStroke());
    assert(model.CanRedo()&&model.Redo()&&Filled(model.GetArt())==20);
    model.BeginStroke();assert(model.PaintLine(0,0,19,19,0));model.EndStroke();
    assert(Filled(model.GetArt())==0&&model.Undo()&&Filled(model.GetArt())==20);
    assert(model.Clear()&&Filled(model.GetArt())==0&&model.Undo()&&Filled(model.GetArt())==20);

    hc::PixelEditorModel clipped(source);
    clipped.BeginStroke();assert(clipped.PaintLine(-100,10,100,10));clipped.EndStroke();
    assert(Filled(clipped.GetArt())==20);
    assert(clipped.Undo());clipped.BeginStroke();
    assert(clipped.PaintLine(INT_MIN,0,INT_MAX,0));clipped.EndStroke();assert(Filled(clipped.GetArt())==20);
    assert(clipped.Undo());clipped.BeginStroke();
    assert(!clipped.PaintLine(-100,-100,-10,-10));assert(!clipped.PaintCell(20,0));assert(!clipped.EndStroke());
    clipped.BeginStroke();assert(clipped.PaintLine(0,0,19,7));clipped.EndStroke();
    assert(Filled(clipped.GetArt())==20&&clipped.GetArt().pixels[7*20+19]==255);

    hc::PixelEditorModel resized(source);resized.BeginStroke();resized.PaintCell(9,9,127);resized.EndStroke();
    assert(resized.SetCanvasSize(40,40));assert(resized.GetArt().pixels[19*40+19]==127);
    assert(resized.SetCanvasSize(20,20));assert(resized.GetArt().pixels[9*20+9]==127);
    assert(resized.SetCanvasSize(30,10));assert(resized.GetArt().pixels[4*30+14]==127);
    assert(resized.Undo()&&resized.GetArt().width==20&&resized.GetArt().height==20);
    assert(resized.SetCanvasSize(1,1)&&Filled(resized.GetArt())==1);
    assert(resized.SetCanvasSize(1000,-3)&&resized.GetArt().width==100&&resized.GetArt().height==1);
    assert(resized.Undo()&&resized.GetArt().width==1&&resized.GetArt().height==1);
    assert(resized.Redo()&&resized.GetArt().width==100);

    hc::PixelEditorModel history(source);
    for(int i=0;i<65;i++){history.BeginStroke();history.PaintCell(i%20,i/20);history.EndStroke();}
    int undone=0;while(history.Undo())++undone;assert(undone==64&&Filled(history.GetArt())==1);
    int redone=0;while(history.Redo())++redone;assert(redone==64&&Filled(history.GetArt())==65);
    assert(history.Undo());history.BeginStroke();history.PaintCell(19,19);history.EndStroke();assert(!history.CanRedo());

    hc::PixelArt malformed;malformed.width=1000;malformed.height=-2;malformed.pixels={33};
    hc::PixelEditorModel normalized(malformed);
    assert(normalized.GetArt().width==100&&normalized.GetArt().height==1&&normalized.GetArt().pixels.size()==100);
    assert(normalized.GetArt().pixels[0]==0);
    std::puts("Pixel editor model: grouped strokes, clipped Bresenham, erase, undo/redo, resize/crop and copy isolation PASS.");
}
