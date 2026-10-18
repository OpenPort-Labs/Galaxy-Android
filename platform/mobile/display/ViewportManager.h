#pragma once

namespace mobile_display {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

class ViewportManager {
public:
    ViewportManager() = default;

    // Calculates the active game viewport given screen dimensions and fullscreen setting
    void update(int screenW, int screenH, bool fullscreen);

    // Returns the current viewport rectangle
    const Rect& getViewport() const { return mViewport; }
    int x() const { return mViewport.x; }
    int y() const { return mViewport.y; }
    int w() const { return mViewport.w; }
    int h() const { return mViewport.h; }

    // Computes desired offscreen render target dimensions (aspect-ratio & scale-matched)
    static void computeTargetSize(int screenW, int screenH, int scaleChoice, bool batterySaver,
                                  bool fullscreen, int& outW, int& outH);

    // Computes touch coordinate scale bounds for virtual pointer
    static void computePointerBounds(int screenW, int screenH, bool fullscreen,
                                     const Rect& vp, Rect& outBounds);

private:
    Rect mViewport;
};

}  // namespace mobile_display
