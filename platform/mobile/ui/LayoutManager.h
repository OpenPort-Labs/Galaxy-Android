#pragma once

#include "Settings.h"
#include <string>

namespace mobile_ui {

struct ButtonLayout {
    float ax, ay, ar;
    float bx, by, br;
    float zx, zy, zr;
    float spinX, spinY, spinR;
};

struct StarStickLayout {
    float cx, cy, r;
};

class LayoutManager {
public:
    LayoutManager();

    ButtonLayout calculateButtonLayout(int screenW, int screenH, const Settings& settings) const;
    StarStickLayout calculateStarStickLayout(int screenW, int screenH, const Settings& settings) const;

    bool isEditMode() const { return mEditMode; }
    void setEditMode(bool active) { mEditMode = active; }

    void clearLayoutOffsets(Settings& settings);
    void resetLayoutOffsets(Settings& settings, const std::string& savePath);

    bool handleEditTouch(float px, float py, int32_t action, int screenW, int screenH,
                         Settings& settings, const std::string& savePath);

    int draggingCluster() const { return mDraggingCluster; }
    int draggingButton() const { return mDraggingButton; }

private:
    bool mEditMode = false;
    int mDraggingCluster = 0; // 0=none, 1=stick, 2=btn cluster, 3=dpad, 4=star stick
    int mDraggingButton = -1; // 0=A, 1=B, 2=Z, 3=SPIN
    float mInitialOwnOfsX = 0.0f;
    float mInitialOwnOfsY = 0.0f;
    float mDragStartX = 0.0f;
    float mDragStartY = 0.0f;
    float mInitialStickOfsX = 0.0f;
    float mInitialStickOfsY = 0.0f;
    float mInitialBtnOfsX = 0.0f;
    float mInitialBtnOfsY = 0.0f;
    float mInitialDpadOfsX = 0.0f;
    float mInitialDpadOfsY = 0.0f;
    float mInitialStarStickOfsX = 0.0f;
    float mInitialStarStickOfsY = 0.0f;
};

}  // namespace mobile_ui
