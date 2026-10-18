#include "Game/Animation/XanimeCore.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/MapObj/CollectCounter.hpp"
#include "Game/MapObj/IceStep.hpp"
#include "Game/Player/FireMarioBall.hpp"
#include "Game/Player/JetTurtleShadow.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioNullBck.hpp"
#include "Game/Player/MarioParts.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Player/TornadoMario.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"

void MarioActor::init2D() {
    MR::getGameSceneLayoutHolder()->initLifeCount(mMaxHealth);

    _1B8 = new CollectCounter("\x83\x7d\x83\x8a\x83\x49\x98\x41\x91\xb1\x93\xa5\x82\xdd");

    _1B8->initWithoutIter();
}

void MarioActor::initParts() {
    mNullAnimation = new MarioNullBck("NULL\x83\x41\x83\x6a\x83\x81");
    mNullAnimation->initWithoutIter();

    mSearchLight = 0;
    mSearchLightThrowPos = nullptr;

    initSearchLight();
    initThrowing();

    _9C4 = new MarioParts(this, "\x95\x58\x8c\x8b\x83\x82\x83\x66\x83\x8b", "MarioFreezeIce", true, nullptr, nullptr);
    _9C4->initWithoutIter();
    _9C4->makeActorDead();
    _9C4->initFixedPosition(TVec3f(0.0f, 0.0f, 0.0f), TVec3f(0.0f, 0.0f, 0.0f), nullptr);

    _9A0 = new JetTurtleShadow("\x83\x4a\x83\x81\x83\x56\x83\x83\x83\x68\x83\x45\x83\x82\x83\x66\x83\x8b");
    _9A0->initWithoutIter();

    _994 = new MarioParts(this, "\x83\x58\x83\x73\x83\x93\x83\x60\x83\x52", "SpinTico", false, getBaseMtx(), nullptr);
    _994->initWithoutIter();
    _994->kill();
}

void MarioActor::updateBeeWingAnimation() {
    if (mPlayerMode != PlayerMode_Bee && mMario->getPlayerMode() != PlayerMode_Bee) {
        getJointCtrl("HandR")->setLocalScale(1.0f);
        getJointCtrl("HandL")->setLocalScale(1.0f);

        return;
    }

    getJointCtrl("HandR")->setLocalScale(0.9f);
    getJointCtrl("HandL")->setLocalScale(0.9f);

    if (mMario->checkLvlA() && mMario->_402 && getMovementStates().jumping) {
        if (_9F0 != 1) {
            MR::startBck(_9E8, "Fly");
            MR::startBva(_9E8, "Fly");
            MR::startBtk(_9E8, "Fly");
        }

        _9F0 = 1;
        return;
    }

    s32 val;
    if (getMovementStates()._1 || mMario->isStatusActive(MarioStatus_Stick)) {
        val = 0;
    } else {
        val = mMario->_402 != 0 ? 2 : 3;
    }

    if (_9F0 == val) {
        return;
    }

    switch (val) {
    case 0:
        MR::startBck(_9E8, "Wait");
        MR::startBva(_9E8, "Wait");
        break;

    case 2:
        MR::startBck(_9E8, "FlyWait");
        MR::startBva(_9E8, "FlyWait");
        break;

    case 3:
        MR::startBck(_9E8, "FlyFall");
        MR::startBva(_9E8, "FlyFall");
        break;
    }

    MR::stopBtk(_9E8);
    _9F0 = val;
}

void MarioActor::updateTornado() {
    if (mTornadoMario == nullptr) {
        return;
    }

    if (mMario->getMovementStates()._F && mMario->_544 > 1) {
        mTornadoMario->show();
    } else if ((!isAnimationRun("\x8b\xf3\x92\x86\x82\xd0\x82\xcb\x82\xe8") || mMario->_430 != 8) && !mMario->isStatusActive(MarioStatus_Magic)) {
        if (mMario->getDrawStates()._8 || _990 != 0) {
            mTornadoMario->hideForce();
        } else {
            mTornadoMario->hide();
        }

        _990 = 0;
    }

    mTornadoMario->setTrHeight(mPosition, mMario->mFrontVec, mMario->_54C, _240);
}

#pragma push
#pragma global_optimizer off
void MarioActor::updateTakingPosition() {
    if (_480) {
        const HitSensor* sensor = _424;
        if (!sensor)
            sensor = getCarrySensor();
        if (!sensor) {
            _480 = false;
        } else {
            switch (sensor->mType) {
            case ATYPE_BOMBHEI:
                if (mMario->isAnimationTerminate("\x83\x4a\x83\x75\x94\xb2\x82\xab")) {
                    stopAnimation(nullptr);
                    _480 = false;
                    mMario->changeAnimationUpper("\x83\x4a\x83\x75\x83\x45\x83\x47\x83\x43\x83\x67");
                }
                break;
            case ATYPE_JET_TURTLE:
            case ATYPE_JET_TURTLE_SLOW:
                if (mMario->isAnimationTerminate(nullptr))
                    _480 = false;
                break;
            }
        }
    }
    if (_B92 < 0) {
        Mtx base;
        TVec3f position;
        TVec3f rotation;
        bool updateAnimation;
        if (_B92 == -3) {
            _494->calc();
            _494->copyTrans(&position);
            _494->copyRotate(&rotation);
            MarioAnimator* animator = mMarioAnim;
            s32 stopped = !isAnimationRun(nullptr);
            s32 terminated = mMario->isAnimationTerminate(nullptr);
            terminated |= stopped;
            s32 landing = animator->isLandingAnimationRun();
            landing |= terminated;
            updateAnimation = landing;
        } else {
            f32 frame;
            if (_B92 == -1)
                frame = mMarioAnim->getUpperFrame();
            if (_B92 == -2)
                frame = mMarioAnim->getFrame();
            PSMTXConcat(getBaseMtx(), _E3C.toMtxPtr(), base);
            J3DModel* model = MR::getJ3DModel(mNullAnimation);
            PSMTXCopy(base, model->getBaseTRMtx());
            if (mNullAnimation->getFramePos(frame, &position, &rotation)) {
                clearNullAnimation(-3);
                if (_424)
                    MR::sendArbitraryMsg(ACTMES_TAKE_TOUCH, _424, getSensor("body"));
                else if (_428[0])
                    MR::sendArbitraryMsg(ACTMES_TAKE_TOUCH, _428[0], getSensor("body"));
                else {
                    _480 = false;
                    clearNullAnimation(0);
                }
            }
            updateAnimation = false;
        }
        if (updateAnimation) {
            if (_424)
                mMarioAnim->updateTakingAnimation(_424);
            else if (_468)
                mMarioAnim->updateTakingAnimation(_428[0]);
        }
        if (_424) {
            _424->mHost->mPosition = position;
            _424->mHost->mRotation = rotation;
            return;
        }
        if (getCarrySensor()) {
            getCarrySensor()->mHost->mPosition = position;
            getCarrySensor()->mHost->mRotation = rotation;
        }
    } else if (_468) {
        if (_428[0]->isType(ATYPE_COINTHROW) || _428[0]->isType(ATYPE_JET_TURTLE) || _428[0]->isType(ATYPE_JET_TURTLE_SLOW) ||
            _428[0]->isType(ATYPE_BOMBHEI)) {
            TVec3f position;
            _494->calc();
            _494->copyTrans(&position);
            TVec3f& carryPosition = _428[0]->mHost->mPosition;
            const TVec3f& velocity = mVelocity;
            carryPosition = position + velocity;
            _494->copyRotate(&_428[0]->mHost->mRotation);
            return;
        }
        TVec3f position;
        mNullAnimation->getLastPos(&position);
        PSMTXMultVec(_E3C.toMtxPtr(), &position, &position);
        PSMTXMultVec(getBaseMtx(), &position, &position);
        f32 offset = 10.0f * (mMario->mWalkSpeed * mMario->mWalkSpeed);
        offset *= JMASinRadian(_490);
        _490 += MR::getRandom(0.1f, 1.0f);
        position.y += offset;
        _428[0]->mHost->mPosition = position;
        TVec3f& carryRotation = _428[0]->mHost->mRotation;
        const TVec3f& rotation = mRotation;
        carryRotation = _438[0] + rotation;
    }
}
#pragma pop

const HitSensor* MarioActor::getCarrySensor() const {
    if (_468 == 0) {
        return nullptr;
    }

    return _428[0];
}

void MarioActor::changeSpecialModeAnimation(const char* pAnimName) {
    switch (mPlayerMode) {
    case 6:
        if (!strcmp(pAnimName, "\x93\xc1\x8e\xea\x83\x45\x83\x47\x83\x43\x83\x67""1A")) {
            changeTeresaAnimation("SleepStart", -1);
            return;
        }

        if (strcmp(pAnimName, "\x93\xc1\x8e\xea\x83\x45\x83\x47\x83\x43\x83\x67""1B")) {
            return;
        }

        changeTeresaAnimation("Sleep", 16);
        MR::emitEffect(_9A4, "Sleep");
    }
}

void MarioActor::updateSpecialModeAnimation() {
    if (!mMario->mMovementStates._A && mMario->getCurrentStatus() == MarioStatus_None) {
        if (mMario->mMovementStates._1 && mMario->_960 == 0x20 && mMarioAnim->isAnimationStop()) {
            mMarioAnim->mXanimePlayer->changeTrackAnimation(0, "\x93\x44\x92\xe1\x91\xac\x95\xe0\x8d\x73");
            mMarioAnim->mXanimePlayer->changeTrackAnimation(1, "\x93\x44\x8d\x82\x91\xac\x95\xe0\x8d\x73");
            _B96 = 2;
        }
    } else {
        _B96 = 0;
    }
    switch (mPlayerMode) {
    case PlayerMode_Teresa:
        updateTeresaAnimation();
        break;
    case PlayerMode_Bee:
        if (mBeeWallWalk && !isJumping() && mMarioAnim->isAnimationStop()) {
            mMarioAnim->mXanimePlayer->changeTrackAnimation(0, "\x83\x6e\x83\x60\x99\xb3\x99\xb4\x91\x4f\x90\x69");
            mMarioAnim->mXanimePlayer->changeTrackAnimation(1, "\x83\x6e\x83\x60\x99\xb3\x99\xb4\x91\x4f\x90\x69");
            mMarioAnim->mXanimePlayer->changeTrackAnimation(2, "\x83\x6e\x83\x60\x99\xb3\x99\xb4\x91\x4f\x90\x69");
            mMarioAnim->mXanimePlayer->changeTrackAnimation(3, "\x83\x6e\x83\x60\x99\xb3\x99\xb4\x83\x45\x83\x47\x83\x43\x83\x67");
        }
        break;
    default:
        mMario->_418 = 0;
        break;
    }
    if (_B96) {
        _B96--;
        if (!_B96 && mMario->mMovementStates._1 && mMarioAnim->isAnimationStop()) {
            mMarioAnim->mXanimePlayer->changeTrackAnimation(0, "\x93\xdd\x8d\x73");
            mMarioAnim->mXanimePlayer->changeTrackAnimation(1, "\x95\xe0\x8d\x73");
        }
    }
}

void MarioActor::initFireBall() {
    for (u32 idx = 0; idx < ARRAY_SIZE(_B54); idx++) {
        _B54[idx] = new FireMarioBall("\x83\x7d\x83\x8a\x83\x49\x89\x8a\x8b\x85");
        _B54[idx]->initWithoutIter();
    }
}

void MarioActor::shootFireBall() {
    if (isAnimationRun("\x83\x74\x83\x40\x83\x43\x83\x41\x93\x8a\x82\xb0"))
        return;
    if (isAnimationRun("\x83\x74\x83\x40\x83\x43\x83\x41\x83\x58\x83\x73\x83\x93\x8b\xf3\x92\x86"))
        return;
    if (isAnimationRun("\x83\x74\x83\x40\x83\x43\x83\x41\x83\x58\x83\x73\x83\x93"))
        return;
    if (getMovementStates()._8) {
        sendMsgToSensor(mMario->getWallPolygon()->mSensor, ACTMES_FIREBALL_ATTACK);
        changeAnimation("\x83\x74\x83\x40\x83\x43\x83\x41\x83\x58\x83\x73\x83\x93");
        return;
    }
    u32 index;
    for (index = 0; index < ARRAY_SIZE(_B54); index++) {
        if (MR::isDead(_B54[index]))
            break;
    }
    if (index == ARRAY_SIZE(_B54))
        return;
    FireMarioBall* ball = _B54[index];
    TVec3f direction;
    direction = mMario->mFrontVec;
    direction += mMario->mHeadVec;
    MR::normalize(&direction);
    TVec3f position;
    getRealPos("HandR", &position);
    position += mMario->mFrontVec * 30.0f;
    ball->appearAndThrow(position, direction);
    playSound("\x90\xba\x93\x8a\x82\xb0", -1);
    if (!isJumping()) {
        mMario->_420 = 45;
        changeAnimation("\x83\x74\x83\x40\x83\x43\x83\x41\x83\x58\x83\x73\x83\x93");
        return;
    }
    if (mMario->_42C >= 3) {
        changeAnimation("\x83\x74\x83\x40\x83\x43\x83\x41\x93\x8a\x82\xb0");
        return;
    }
    if (mMario->_42C == 0)
        changeAnimation("\x83\x74\x83\x40\x83\x43\x83\x41\x83\x58\x83\x73\x83\x93\x8b\xf3\x92\x86");
    else
        changeAnimation("\x83\x74\x83\x40\x83\x43\x83\x41\x93\x8a\x82\xb0");
    jumpHop();
    mMario->_42C++;
    f32 gravitySpeed = mMario->cutGravityElementFromJumpVec(true);
    Mario* mario = mMario;
    mario->mJumpVec.x *= 0.5f;
    mario->mJumpVec.y *= 0.5f;
    mario->mJumpVec.z *= 0.5f;
    mMario->mJumpVec += _240 * gravitySpeed;
}

void MarioActor::showFreezeModel() {
    _9C4->appear();
    MR::onCalcAnim(_9C4);
    MR::startBva(_9C4, "Nomal");
}

void MarioActor::hideFreezeModel() {
    MR::startBck(_9C4, "Break");
    MR::startBva(_9C4, "Break");

    mMario->startFreezeEnd();
}

void MarioActor::updateFairyStar() {
    if (_482)
        return;
    if (!_EEB)
        return;
    if (!isEnableNerveChange())
        return;
    bool enabled = true;
    if (selectAction("\x83\x58\x83\x73\x83\x93\x89\xf1\x95\x9c\x83\x47\x83\x74\x83\x46\x83\x4e\x83\x67") != 1)
        enabled = false;
    if (_94C && enabled) {
        if (MR::isDead(_994)) {
            _994->appear();
            _994->mRotation.set(0.0f, 0.0f, 0.0f);
            MR::startBck(_994, "SpinTimer");
            playSound("\x83\x58\x83\x73\x83\x93\x8b\x96\x89\xc2", -1);
        }
        TVec3f position;
        MR::copyJointPos(_994, "Center", &position);
        Color8 color(255, 225, 225, 255);
        f32 power = 0.001f;
        MR::requestPointLight(_994, position, color, power, 0);
        return;
    }
    if (!MR::isDead(_994))
        _994->kill();
}

void MarioActor::update2D() {
    GameSceneLayoutHolder* layoutHolder = MR::getGameSceneLayoutHolder();
    layoutHolder->setLifeCount(mHealth);

    if (mPlayerMode == PlayerMode_Bee || mMario->getPlayerMode() == PlayerMode_Bee) {
        f32 airWalkTime = mConst->getTable()->mAirWalkTime;
        f32 ratio = (airWalkTime > 0.0f) ? ((f32)mMario->_402 / airWalkTime) : 0.0f;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        layoutHolder->setBeePowerRatio(ratio);
    }

    if (mMario->isSwimming()) {
        layoutHolder->setOxygenRatio((f32)mMario->mSwim->mOxygen / mConst->getTable()->mOxygenMax);
    }

    if (_989 == 0) {
        _1B8->kill();
    }
}

void MarioActor::updateThrowVector() {
    HitSensor* sensor = _46C;
    if (sensor && !MR::isExistInAttributeGroupSearchTurtle(sensor->mHost))
        sensor = nullptr;
    if (sensor && _468) {
        if (!getDrawStates()._5) {
            if (_470 && _46C != _470)
                _470 = nullptr;
        }
        if (!getDrawStates()._5) {
            if (!_47C)
                _484 = _2A0;
            _47C++;
            TVec3f direction = _46C->mPosition - _484;
            if (direction.length() <= 40.0f + _46C->getRadius()) {
                _484 = _46C->mPosition;
                _470 = _46C;
                if (MR::isSensorEnemy(_46C)) {
                    _47C = 60;
                    return;
                }
                _47C = 2;
                return;
            }
            MR::normalize(&direction);
            _484 += direction * 40.0f;
            if (MR::checkStrikePointToMap(_484, nullptr)) {
                _47C = 0;
                _470 = nullptr;
            }
        }
    } else if (!getDrawStates()._5) {
        if (_47C)
            _47C--;
        if (!_47C)
            _470 = nullptr;
    }
}

void MarioActor::createIceFloor(const TVec3f& rVec) {
    TPos3f mtx;
    mtx.identity();

    TVec3f upVec;
    getUpVec(&upVec);

    MR::makeMtxUpFront(&mtx, -getAirGravityVec(), mMario->mFrontVec);

    TVec3f vec;
    mtx.getEuler(vec);

    vec *= 180.0f / PI;
    createIceFloor(rVec, vec);
}

void MarioActor::createIceFloor(const TVec3f& rVec1, const TVec3f& rVec2) {
    _B4C[_B50]->setOn(_B50, rVec1, rVec2);

    _B50 = (_B50 + 1) % 20;

    if (!MR::isDead(_B4C[_B50])) {
        _B4C[_B50]->destroy();
    }
}

void MarioActor::createIceWall(const TVec3f& rVec1, const TVec3f& rVec2) {
    TPos3f mtx;
    mtx.identity();
    TVec3f upVec;
    getUpVec(&upVec);

    MR::makeMtxFrontUp(&mtx, getGravityVector(), rVec2);

    TVec3f vec;
    mtx.getEuler(vec);
    vec *= 180.0f / PI;
    _B4C[_B50]->setOn(_B50, rVec1, vec);

    _B50 = (_B50 + 1) % 20;

    if (!MR::isDead(_B4C[_B50])) {
        _B4C[_B50]->destroy();
    }
}

void MarioActor::updateBaseMtxTeresa(MtxPtr mtx) {
    TVec3f horizontal;
    f32 tilt = MR::clamp(MR::vecKillElement(mVelocity, getGravityVec(), &horizontal) / 10.0f, -1.0f, 1.0f);
    if (!MR::isNearZero(mMario->getWorldPadDir())) {
        const TVec3f& front = mMario->mFrontVec;
        if (MR::diffAngleAbsHorizontal(mMario->getWorldPadDir(), front, getGravityVec()) > 0.3926991f)
            tilt = 0.0f;
    }
    _9AC = 0.98f * _9AC + 0.02f * tilt;
    f32 angle = mConst->getTable()->mTeresaAngleDown;
    if (_9AC > 0.0f)
        angle = mConst->getTable()->mTeresaAngleUp;
    PSMTXConcat(mtx, MR::tmpMtxRotXRad(_9AC * angle), mtx);
}

bool MarioActor::finalizeFreezeModel() {
    if (MR::isBckStopped(_9C4)) {
        _9C4->kill();
        MR::offCalcAnim(_9C4);

        return false;
    }

    return true;
}

void MarioActor::offTakingFlag() {
    _480 = false;
}
