#pragma once

#include "UICommon.h"
#include <vector>

namespace mobile_ui {

class GlesOverlayPipeline {
public:
    GlesOverlayPipeline();
    ~GlesOverlayPipeline();

    bool init();
    void shutdown();

    void render(int screenW, int screenH, const std::vector<UIVertex>& verts);

private:
    unsigned int mProg = 0;
    unsigned int mVao = 0;
    unsigned int mVbo = 0;
    unsigned int mFontTex = 0;
    int mAtlasW = 0;
    int mAtlasH = 0;
    bool mInitialized = false;
};

}  // namespace mobile_ui
