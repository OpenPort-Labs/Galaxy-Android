#include "Game/Enemy/KariKariDirector.hpp"
#include "port/port.h"
#include "Game/MapObj/BlackHole.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"

void MarioActor::initBlackHoleOut() {
    mPosRelativeToBlackHole = mPosition - mBlackHolePosition;

    TVec3f normalisedRelativePos(mPosRelativeToBlackHole);
    TVec3f normalisedRelativeCameraPos = mCamPos - mBlackHolePosition;

    MR::normalizeOrZero(&normalisedRelativeCameraPos);
    MR::normalizeOrZero(&normalisedRelativePos);

    Mtx rotation;
    TVec3f rotateAxis;

    rotateAxis.cross(normalisedRelativeCameraPos, normalisedRelativePos);

    f32 mag = mPosRelativeToBlackHole.length();
    TVec3f killed;
    f32 flt = MR::vecKillElement(mCamPos - mPosition, mCamDirZ, &killed);
    flt *= mConst->getTable()->mBlackHoleFirstRadius;

    PSMTXRotAxisRad(rotation, &rotateAxis, atan(flt / mag));
    PSMTXMultVec(rotation, &mPosRelativeToBlackHole, &mBlackHoleRotateAxis);
    MR::normalizeOrZero(&mBlackHoleRotateAxis);

    damageDropThrowMemoSensor();
    MR::removeAllClingingKarikari();
}

void MarioActor::exeGameOverBlackHole2() {
    if (port_is_infinite_health()) {
        teleportToOriginalSpot();
        return;
    }
    if (MR::isFirstStep(this)) {
        MR::setCubeBgmChangeInvalid();
        MR::clearBgmQueue();

        if (!(mBlackHole->tryStartDemoCamera()) && !mMario->getMovementStates()._37) {
            MR::startBlackHoleCamera("\x83\x75\x83\x89\x83\x62\x83\x4e\x83\x7a\x81\x5b\x83\x8b", mBlackHolePosition, mPosition);
        }

        _F44 = false;

        changeAnimationNonStop("\x83\x75\x83\x89\x83\x62\x83\x4e\x83\x7a\x81\x5b\x83\x8b\x97\x8e\x89\xba");
        playEffect("\x8b\xa4\x92\xca\x83\x75\x83\x89\x83\x62\x83\x4e\x83\x7a\x81\x5b\x83\x8b");
        playSound("\x83\x75\x83\x89\x83\x62\x83\x4e\x83\x7a\x81\x5b\x83\x8b\x8b\x7a\x82\xa2\x8d\x9e\x82\xdc\x82\xea", -1);
        playEffect(changeMorphString("DieBlackHole"));
        initBlackHoleOut();

        mMario->mMovementStates._3C = true;

        MR::startStarPointerModeDemoMarioDeath(this);
        MR::deactivateDefaultGameLayout();
    }

    if (getNerveStep() == 60) {
        if (!MR::getPlayerLeft()) {
            MR::startPlayerEvent("\x83\x51\x81\x5b\x83\x80\x83\x49\x81\x5b\x83\x6f\x81\x5b");
        } else {
            MR::startPlayerEvent("\x83\x7d\x83\x8a\x83\x49\x93\xde\x97\x8e");
        }
    }

    if (getNerveStep() == mConst->getTable()->mBlackHoleHideTime) {
        _482 = true;

        MR::hidePlayer();
        MR::emitEffect(this, "\x83\x75\x83\x89\x83\x62\x83\x4e\x83\x7a\x81\x5b\x83\x8b\x8f\xc1\x96\xc5");
    }

    f32 nervestepfloat = getNerveStep();
    f32 flt = 1.0f;

    if (nervestepfloat < 180.0f) {
        flt = MR::sqrt(nervestepfloat / 180.0f);
    }

    f32 angle = mConst->getTable()->mBlackHoleRotateSpeed;
    angle = nervestepfloat * angle;
    angle = flt * angle;

    if (angle > mConst->getTable()->mBlackHoleRotateLimit) {
        angle = mConst->getTable()->mBlackHoleRotateLimit;
    }

    Mtx rotationMatrix;
    PSMTXRotAxisRad(rotationMatrix, &mBlackHoleRotateAxis, angle);
    PSMTXMultVec(rotationMatrix, &mPosRelativeToBlackHole, &mPosRelativeToBlackHole);

    MR::vecBlendSphere(mBlackHoleRotateAxis, -mCamDirZ, &mBlackHoleRotateAxis, 0.01f);

    f32 distChangeFactor = 180 - getNerveStep();

    if (distChangeFactor < 0.0f) {
        distChangeFactor = 0.0f;
    }

    f32 newDistToBlackHole = mPosRelativeToBlackHole.length() * distChangeFactor / (1 + distChangeFactor);

    mPosRelativeToBlackHole.setLength(newDistToBlackHole);

    f32 scale = getNerveStep() * mConst->getTable()->mBlackHoleScaleSpeed;
    scale = 1 - scale;

    if (scale < mConst->getTable()->mBlackHoleScaleLimit) {
        scale = mConst->getTable()->mBlackHoleScaleLimit;
    }

    mScale.set(scale);

    mPosition = mBlackHolePosition + mPosRelativeToBlackHole;

    mVelocity.zero();
}
