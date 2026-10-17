#include "Game/LiveActor/ClippingJudge.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/Util/CameraUtil.hpp"

void ClippingJudge_FORCE_MATCH_SDATA2() {
    0.0f;
}

ClippingJudge::ClippingJudge(const char* pName) : NameObj(pName), mFrustum() {
    for (s32 i = 0; i < 8; i++) {
        mClipDistances[i] = -1.0f;
    }

    mClipDistances[0] = 0.0f;
    mClipDistances[1] = 60000.0f;
    mClipDistances[2] = 50000.0f;
    mClipDistances[3] = 40000.0f;
    mClipDistances[4] = 30000.0f;
    mClipDistances[5] = 20000.0f;
    mClipDistances[6] = 10000.0f;
    mClipDistances[7] = 5000.0f;
}

void ClippingJudge::init(const JMapInfoIter& rIter) {
}

void ClippingJudge::movement() {
    calcViewingVolume(&mFrustum, MR::getFarZ());

    for (u32 i = 1; i < 8; i++) {
        calcViewingVolume(&mClipFrustums[i], mClipDistances[i]);
    }
}

bool ClippingJudge::isJudgedToClipFrustum(const TVec3f& rVec, f32 a2) const {
    return !mFrustum.mayIntersectBall3(rVec, a2);
}

bool ClippingJudge::isJudgedToClipFrustum(const TVec3f& rVec, f32 a2, s32 index) const {
    return (!index) ? !mFrustum.mayIntersectBall3(rVec, a2) : !mClipFrustums[index].mayIntersectBall3(rVec, a2);
}

void ClippingJudge::calcViewingVolume(THex3f* pVolume, f32 farClip) {
    f32 nearClip = 500.0f;
    if (MR::isSubjectiveCameraOnForObjClipping()) {
        nearClip = 100.0f;
    }

    f32 aspect = MR::getAspect();
    f32 fovy = MR::getFovy();
    TPos3f cameraMtx;
    cameraMtx.setPositionFromLookAt(MR::getCameraViewMtx());
    f32 height = nearClip * static_cast< f32 >(tan(fovy * (JGeometry::TUtil< f32 >::PI() / 360.0f)));
    f32 width = aspect * height;
    // On widescreen and modern ultrawide screens (19.5:9, 20:9, 21:9), extend the horizontal frustum
    // margin so actors at the screen periphery don't pop in/out during fast camera pans.
    if (aspect > 1.77f) {
        width *= 1.25f;
    } else {
        width *= 1.10f;
    }
#ifdef TARGET_PC
    {
        // In VR, cull against what the headset actually sees.
        float vrMtx[12], tanX, tanY;
        if (port_vr_cull_view(vrMtx, &tanX, &tanY)) {
            nearClip = 20.0f;
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 4; c++) {
                    cameraMtx.mMtx[r][c] = vrMtx[r * 4 + c];
                }
            }
            width = nearClip * tanX;
            height = nearClip * tanY;
        }
    }
#endif

    TVec3f front;
    cameraMtx.getZDir(front);
    TVec3f position;
    TVec3f bottomRight(width, -height, nearClip);
    TVec3f topRight(width, height, nearClip);
    TVec3f topLeft(-width, height, nearClip);
    TVec3f bottomLeft(-width, -height, nearClip);
    cameraMtx.getTrans(position);
    cameraMtx.mult(topRight, topRight);
    cameraMtx.mult(topLeft, topLeft);
    cameraMtx.mult(bottomLeft, bottomLeft);
    cameraMtx.mult(bottomRight, bottomRight);

    pVolume->mPlanes[3].set(position, topLeft, topRight);
    pVolume->mPlanes[1].set(position, topRight, bottomRight);
    pVolume->mPlanes[2].set(position, bottomRight, bottomLeft);
    pVolume->mPlanes[0].set(position, bottomLeft, topLeft);

    TVec3f back;
    back.negate(front);
    TVec3f planePosition;
    planePosition.scaleAdd(farClip, front, position);
    pVolume->mPlanes[5].set(back, planePosition);
    planePosition.scaleAdd(nearClip, front, position);
    pVolume->mPlanes[4].set(front, planePosition);
}

namespace MR {
    ClippingJudge* getClippingJudge() {
        return getClippingDirector()->mJudge;
    }
}  // namespace MR

ClippingJudge::~ClippingJudge() {
}
