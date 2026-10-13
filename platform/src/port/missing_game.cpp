// Definitions the original binary had (or never needed) that the decomp does
// not provide as out-of-line code.
#include <string.h>

#include "Game/Effect/ParticleEmitter.hpp"
#include "Game/System/StationedArchiveLoader.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "nw4r/lyt/pane.h"
#include "nw4r/ut/TextWriterBase.h"

// Base-class draw of the J3D packet hierarchy does nothing.
void J3DPacket::draw() {}

// Only derived conditions are ever instantiated; the base accepts everything.
bool StationedArchiveLoader::Condition::isExecute(const MR::StationedFileInfo*) const { return true; }

namespace nw4r {
    namespace lyt {
        Pane::Pane() {
            Init();
            mBasePosition = 4;  // centre/centre
            memset(mName, 0, sizeof(mName));
            memset(mUserData, 0, sizeof(mUserData));
            mTranslate = math::VEC3(0.0f, 0.0f, 0.0f);
            mRotate = math::VEC3(0.0f, 0.0f, 0.0f);
            mScale = math::VEC2(1.0f, 1.0f);
            mSize = Size(0.0f, 0.0f);
            mAlpha = 0xFF;
            mGlbAlpha = mAlpha;
            mFlag = 0;
            SetVisible(true);
        }
    }  // namespace lyt

    namespace ut {
        // The game only prints wide strings (see CustomTagProcessor.cpp).
        template <>
        f32 TextWriterBase< char >::PrintImpl(StreamType str, int length) {
            (void)str;
            (void)length;
            return 0.0f;
        }
    }  // namespace ut
}  // namespace nw4r

extern "C" {
__attribute__((weak)) int port_is_fullscreen_aspect(void) { return 0; }
__attribute__((weak)) float port_get_display_aspect(void) { return 16.0f / 9.0f; }
__attribute__((weak)) int port_is_fly_mode(void) { return 0; }
__attribute__((weak)) int port_is_skip_cutscenes(void) { return 0; }
__attribute__((weak)) int port_is_infinite_health(void) { return 0; }
__attribute__((weak)) int port_input_real_a(void) { return 0; }
__attribute__((weak)) int port_input_real_z(void) { return 0; }
}
