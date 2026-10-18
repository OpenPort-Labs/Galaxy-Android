#include "ViewportManager.h"

#include <algorithm>
#include <math.h>

namespace mobile_display {

void ViewportManager::update(int screenW, int screenH, bool fullscreen) {
    if (screenW <= 0 || screenH <= 0) {
        mViewport = {0, 0, 0, 0};
        return;
    }

    if (fullscreen) {
        mViewport = {0, 0, screenW, screenH};
        return;
    }

    float targetAspect = 16.0f / 9.0f;
    float screenAspect = (screenW >= screenH)
        ? ((float)screenW / (float)screenH)
        : ((float)screenH / (float)screenW);

    if (screenW >= screenH) {
        if (screenAspect > targetAspect) {
            mViewport.h = screenH;
            mViewport.w = (int)lroundf(screenH * targetAspect);
            mViewport.x = (screenW - mViewport.w) / 2;
            mViewport.y = 0;
        } else {
            mViewport.w = screenW;
            mViewport.h = (int)lroundf(screenW / targetAspect);
            mViewport.x = 0;
            mViewport.y = (screenH - mViewport.h) / 2;
        }
    } else {
        mViewport.w = screenW;
        mViewport.h = (int)lroundf(screenW / targetAspect);
        mViewport.x = 0;
        mViewport.y = (screenH - mViewport.h) / 2;
    }
}

void ViewportManager::computeTargetSize(int screenW, int screenH, int scaleChoice,
                                       bool batterySaver, bool fullscreen,
                                       int& outW, int& outH) {
    if (batterySaver) {
        scaleChoice = std::min(scaleChoice, 1);
    }

    int dispH = std::min(screenW, screenH);
    int dispW = std::max(screenW, screenH);
    int targetH = 1080;

    if (scaleChoice == 0) targetH = 480;       // Native 480p (Wii accurate / max FPS)
    else if (scaleChoice == 1) targetH = 720;  // 720p HD (budget chips)
    else if (scaleChoice == 2) targetH = 1080; // 1080p FHD
    else if (scaleChoice == 3) targetH = 1440; // 1440p 2K Ultra
    else if (dispH > 1080) {
        targetH = std::min(dispH, 1440);
    }

    int targetW;
    if (fullscreen) {
        float nativeAspect = (float)dispW / (float)dispH;
        targetW = (int)lroundf(targetH * nativeAspect);
    } else {
        targetW = (targetH * 16) / 9;
    }

    outW = targetW;
    outH = targetH;
}

void ViewportManager::computePointerBounds(int screenW, int screenH, bool fullscreen,
                                          const Rect& vp, Rect& outBounds) {
    if (fullscreen) {
        int ptrW = (int)lroundf((float)screenH * (16.0f / 9.0f));
        int ptrX = (screenW - ptrW) / 2;
        outBounds = {ptrX, vp.y, ptrW, vp.h};
    } else {
        outBounds = vp;
    }
}

}  // namespace mobile_display
