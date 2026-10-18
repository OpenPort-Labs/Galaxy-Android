#include "Game/Camera/CameraTargetHolder.hpp"
#include "Game/Camera/CameraTargetObj.hpp"

namespace {
    static const f32 sMovingThreshold = 1.0f;
};  // namespace

CameraTargetHolder::CameraTargetHolder() {
    mTarget = nullptr;
    mTargetActor = new CameraTargetActor("\x83\x41\x83\x4e\x83\x5e\x81\x5b\x92\x8d\x96\xda");
    mTargetPlayer = new CameraTargetPlayer("\x83\x7d\x83\x8a\x83\x49\x92\x8d\x96\xda");
}

void CameraTargetHolder::movement() {
    if (mTarget) {
        mTarget->movement();
    }
}

CameraTargetObj* CameraTargetHolder::get() {
    return mTarget;
}

void CameraTargetHolder::set(CameraTargetObj* pTarget) {
    mTarget = pTarget;
}

void CameraTargetHolder::set(const LiveActor* pActor) {
    mTargetActor->mActor = pActor;
    mTarget = mTargetActor;
}

void CameraTargetHolder::set(const MarioActor* pActor) {
    mTargetPlayer->mActor = pActor;
    mTarget = mTargetPlayer;
}

bool CameraTargetHolder::isOnGround() const {
    if (!mTarget) return true;
    return !mTarget->isJumping() || mTarget->isWaterMode() || mTarget->isOnWaterSurface();
}

bool CameraTargetHolder::isMoving() const {
    if (!mTarget) return false;
    return mTarget->getLastMove().length() > ::sMovingThreshold;
}
