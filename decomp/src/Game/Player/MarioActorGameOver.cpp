#include "Game/LiveActor/HitSensor.hpp"
#include "port/port.h"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioFpView.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/RushEndInfo.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"

void MarioActor::forceKill(u32 type) {
    if (port_is_infinite_health()) {
        teleportToOriginalSpot();
        return;
    }
    if (mMario->getMovementStates()._1F) {
        return;
    }

    if (!isEnableNerveChange()) {
        return;
    }

    _A6E = 0;

    if (!MR::isDead(_994)) {
        _994->kill();
    }

    if (mMario->isStatusActive(MarioStatus_FpView)) {
        mMario->mFpView->forceClose();
        mMario->closeStatus(nullptr);
    }

    if (_934) {
        if (_924 != nullptr && _924->isType(ACTMES_GROUP_SHOW)) {
            _B91 = true;
            _B90 = true;
        }

        if (_934) {
            if (!_924->receiveMessage(ACTMES_RUSH_CANCEL, getSensor("body"))) {
                _924->receiveMessage(ACTMES_RUSH_FORCE_CANCEL, getSensor("body"));
            }

            if (_934) {
                RushEndInfo endInfo(nullptr, 4, TVec3f(0.0f, 0.0f, 0.0f), false, 0);
                endRush(&endInfo);
            }
        }

        _934 = false;
    }

    if (type == 0) {
        mMario->doAbyssDamage();
        return;
    }

    if (type == 3) {
        mMario->startPadVib(3);
    }

    if (type == 4) {
        mMario->startPadVib(3);
        initDarkMask();
        mMario->doDarkDamage();

        return;
    }

    switch (type) {
    case 1:
    case 2:
    case 5:
        MR::deactivateDefaultGameLayout();

    default:
        _39D = type;
        mHealth = 0;

        setNerve(GET_NERVE(MarioActor, MarioActorNrvGameOver));
        break;
    }
}

void MarioActor::exeGameOver() {
    if (port_is_infinite_health()) {
        teleportToOriginalSpot();
        return;
    }
    mVelocity.zero();
    calcHeadPos();
    updateRealMtx();

    if (getNerveStep() == 32) {
        _A6E = 0;
    }

    _46C = nullptr;

    PSMTXIdentity(mMarioAnim->_7C);

    damageDropThrowMemoSensor();

    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();

    if (MR::getPlayerLeft() == 0) {
        MR::startPlayerEvent("\x83\x51\x81\x5b\x83\x80\x83\x49\x81\x5b\x83\x6f\x81\x5b");
        changeGameOverAnimation();
    } else {
        switch (_39D) {
        case 2:
            MR::startPlayerEvent("\x83\x53\x81\x5b\x83\x58\x83\x67\x83\x8c\x81\x5b\x83\x58\x95\x89\x82\xaf");
            break;

        case 1:
            MR::startPlayerEvent("\x83\x8c\x81\x5b\x83\x58\x95\x89\x82\xaf");
            changeGameOverAnimation();
            break;

        case 5:
            MR::startPlayerEvent("\x83\x8c\x81\x5b\x83\x58\x95\x89\x82\xaf");
            break;

        default:
            MR::startPlayerEvent("\x83\x7d\x83\x8a\x83\x49\x83\x5f\x83\x45\x83\x93");
        }
    }

    MR::startGlobalEventCameraNoTarget("\x8f\xb8\x93\x56\x83\x4a\x83\x81\x83\x89", -1);
    MR::startStarPointerModeDemoMarioDeath(this);
}

void MarioActor::exeGameOverAbyss() {
    if (port_is_infinite_health()) {
        teleportToOriginalSpot();
        return;
    }
    mVelocity = getLastMove() * 0.99f + mCamDirZ * 35.0f * 0.01f;
    mLastMove = mVelocity;

    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();
    MR::startGlobalEventCameraNoTarget("\x93\xde\x97\x8e\x83\x4a\x83\x81\x83\x89", -1);

    _F44 = false;

    if (MR::getPlayerLeft() == 0) {
        MR::startPlayerEvent("\x83\x51\x81\x5b\x83\x80\x83\x49\x81\x5b\x83\x6f\x81\x5b");
    } else {
        MR::startPlayerEvent("\x83\x7d\x83\x8a\x83\x49\x93\xde\x97\x8e");
    }

    MR::startStarPointerModeDemoMarioDeath(this);
    mMario->changeAnimationNonStop("\x93\xde\x97\x8e\x83\x5f\x83\x45\x83\x93");

    _B90 = true;
}

void MarioActor::exeGameOverFire() {
    if (port_is_infinite_health()) {
        teleportToOriginalSpot();
        return;
    }
    mVelocity.zero();
    calcHeadPos();
    updateRealMtx();

    if (getNerveStep() == 120) {
        _A6E = 0;
    }

    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();

    if (MR::getPlayerLeft() == 0) {
        MR::startPlayerEvent("\x83\x51\x81\x5b\x83\x80\x83\x49\x81\x5b\x83\x6f\x81\x5b");
        MR::startGlobalEventCameraNoTarget("\x8f\xb8\x93\x56\x83\x4a\x83\x81\x83\x89", -1);
    } else {
        MR::startPlayerEvent("\x83\x7d\x83\x8a\x83\x49\x89\x8a\x83\x5f\x83\x45\x83\x93");
        MR::startGlobalEventCameraNoTarget("\x93\xde\x97\x8e\x83\x4a\x83\x81\x83\x89", -1);
    }

    MR::startStarPointerModeDemoMarioDeath(this);
}

void MarioActor::exeGameOverSink() {
    if (port_is_infinite_health()) {
        teleportToOriginalSpot();
        return;
    }
    mVelocity.zero();
    calcHeadPos();
    updateRealMtx();

    if (getNerveStep() == 120) {
        _A6E = 0;
    }

    if (MR::isFirstStep(this)) {
        MR::setCubeBgmChangeInvalid();
        MR::clearBgmQueue();
        changeGameOverAnimation();

        if (MR::getPlayerLeft() == 0) {
            MR::startPlayerEvent("\x83\x51\x81\x5b\x83\x80\x83\x49\x81\x5b\x83\x6f\x81\x5b");
            MR::startGlobalEventCameraNoTarget("\x8f\xb8\x93\x56\x83\x4a\x83\x81\x83\x89", -1);
        } else {
            MR::startPlayerEvent("\x83\x7d\x83\x8a\x83\x49\x83\x5f\x83\x45\x83\x93");
            MR::startGlobalEventCameraNoTarget("\x93\xde\x97\x8e\x83\x4a\x83\x81\x83\x89", -1);
        }

        MR::startStarPointerModeDemoMarioDeath(this);

        if (mMario->_960 == 25 || mMario->_960 == 17) {
            playEffect("\x8d\xbb\x96\x84\x82\xdc\x82\xe8\x91\xcc");
            playEffect("\x8d\xbb\x96\x84\x82\xdc\x82\xe8\x8e\xe8");
        }

        if (mMario->_960 == 31) {
            playEffect("\x93\x44\x96\x84\x82\xdc\x82\xe8\x91\xcc");
            playEffect("\x93\x44\x96\x84\x82\xdc\x82\xe8\x8e\xe8");
        }
    }

    if (mMario->_960 == 31 || mMario->_960 == 18) {
        playSound("\x8f\xc0\x8e\x80\x96\x53", -1);

        if (getNerveStep() == 90) {
            playSound("\x90\xba\x8f\xc0\x92\xbe\x82\xdd\x8e\x80\x96\x53", -1);
        }
    } else {
        playSound("\x8d\xbb\x8e\x80\x96\x53", -1);

        if (getNerveStep() == 90) {
            playSound("\x90\xba\x8d\xbb\x92\xbe\x82\xdd\x8e\x80\x96\x53", -1);
        }
    }
}

void MarioActor::exeGameOverNonStop() {
    if (port_is_infinite_health()) {
        teleportToOriginalSpot();
        return;
    }
    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();

    if (MR::getPlayerLeft() == 0) {
        MR::startPlayerEvent("\x83\x51\x81\x5b\x83\x80\x83\x49\x81\x5b\x83\x6f\x81\x5b");
        MR::startGlobalEventCameraNoTarget("\x8f\xb8\x93\x56\x83\x4a\x83\x81\x83\x89", -1);
    } else {
        MR::startPlayerEvent("\x83\x7d\x83\x8a\x83\x49\x89\x8a\x83\x5f\x83\x45\x83\x93");
        MR::startGlobalEventCameraNoTarget("\x93\xde\x97\x8e\x83\x4a\x83\x81\x83\x89", -1);
    }

    MR::startStarPointerModeDemoMarioDeath(this);
}

void MarioActor::exeTimeWait() {
    if (_FB8 != getNerveStep()) {
        return;
    }

    setNerve(_FB4);
    _FB4 = nullptr;
}
