#include "Game/Camera/CameraDirector.hpp"
#include "Game/Boss/BossStinkBug.hpp"
#include "Game/Camera/CameraCover.hpp"
#include "Game/Camera/CameraHolder.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraManEvent.hpp"
#include "Game/Camera/CameraManGame.hpp"
#include "Game/Camera/CameraManPause.hpp"
#include "Game/Camera/CameraManSubjective.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraParamChunkHolder.hpp"
#include "Game/Camera/CameraParamChunkID.hpp"
#include "Game/Camera/CameraPoseParam.hpp"
#include "Game/Camera/CameraRailHolder.hpp"
#include "Game/Camera/CameraRegisterHolder.hpp"
#include "Game/Camera/CameraRotChecker.hpp"
#include "Game/Camera/CameraShaker.hpp"
#include "Game/Camera/CameraTargetHolder.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/Camera/CameraViewInterpolator.hpp"
#include "Game/Camera/GameCameraCreator.hpp"
#include "Game/Camera/OnlyCamera.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <cstring>

extern "C" void port_input_get_right_stick(int chan, float* x, float* y);
extern "C" int port_input_camera_inverted(void);

static inline TVec3f rotateVectorAxis(const TVec3f& v, const TVec3f& axis, f32 angle) {
    f32 cosA = MR::cos(angle);
    f32 sinA = MR::sin(angle);
    TVec3f cross = axis.cross(v);
    f32 dot = axis.dot(v);
    return v * cosA + cross * sinA + axis * (dot * (1.0f - cosA));
}

void CameraDirector_FORCE_MATCH_SDATA2() {
    (void)1.0f;
    (void)0.0f;
    (void)0.5f;
    (void)2.0f;
    (void)MR::pi();
}

namespace {
    // static const s32 sDefaultBlendFrame =
    static const s32 sSubjectivePosInterpolateFrame = 0;
    static const s32 sSubjectiveRotInterpolateFrame = 10;
    static const s32 sSubjectiveInterpolateFrame = 20;
    // static const s32 sAnimCamBlendFrame =
    // static const f32 sAnimCamRate =
    static f32 sDefaultFovy = 45.0f;
    static const char* sTalkCameraName = "\x8b\xa4\x92\xca\x89\xef\x98\x62\x83\x4a\x83\x81\x83\x89";
    static const char* sStartAnimCameraName = "\x83\x58\x83\x5e\x81\x5b\x83\x67\x83\x41\x83\x6a\x83\x81\x83\x4a\x83\x81\x83\x89";
    static const char* sSubjectiveCameraName = "\x8e\xe5\x8a\xcf\x83\x4a\x83\x81\x83\x89";
    // static const s32 sTestAnimCameraName =
    // static const s32 sConvertSize =
    // static const s32 sConvertQuality =
    static s32 sUpdateCounter;
    // static const s32 sAnimCameraName =
    // static const s32 sAnimCameraZoneID =
};  // namespace

CameraDirector::CameraDirector(const char* pName) : NameObj(pName) {
    mTargetObj = nullptr;
    mStack = new CameraManStack();
    mOnlyCamera = new OnlyCamera("OnlyCamera");
    mPoseParam1 = new CameraPoseParam();
    mPoseParam2 = new CameraPoseParam();
    mHolder = new CameraHolder("\x83\x4a\x83\x81\x83\x89\x83\x7a\x83\x8b\x83\x5f\x81\x5b");
    mChunkHolder = new CameraParamChunkHolder(mHolder, "\x83\x70\x83\x89\x83\x81\x81\x5b\x83\x5e");
    mCameraCreator = new GameCameraCreator(mChunkHolder);
    mRailHolder = new CameraRailHolder("\x83\x4a\x83\x81\x83\x89\x83\x8c\x81\x5b\x83\x8b\x8a\xc7\x97\x9d");
    mRegisterHolder = new CameraRegisterHolder("\x83\x4a\x83\x81\x83\x89\x83\x8c\x83\x57\x83\x58\x83\x5e");
    mTargetHolder = new CameraTargetHolder();
    mShaker = new CameraShaker("\x90\x55\x93\xae");
    mViewInterpolator = new CameraViewInterpolator();
    mCover = new CameraCover("CameraCover");
    mRotChecker = new CameraRotChecker();
    mCameraManGame = new CameraManGame(mHolder, mChunkHolder, "\x83\x51\x81\x5b\x83\x80\x83\x4a\x83\x81\x83\x89\x83\x7d\x83\x93");
    mCameraManEvent = new CameraManEvent(mHolder, mChunkHolder, "\x83\x43\x83\x78\x83\x93\x83\x67\x83\x4a\x83\x81\x83\x89\x83\x7d\x83\x93");
    mCameraManPause = new CameraManPause("\x83\x7c\x81\x5b\x83\x59\x83\x4a\x83\x81\x83\x89\x83\x7d\x83\x93");
    mCameraManSubjective = new CameraManSubjective("\x8e\xe5\x8a\xcf\x83\x4a\x83\x81\x83\x89\x83\x7d\x83\x93");
    _58 = false;
    mEventNum = 0;
    mIsStartCameraActive = true;
    mStartTime = 0;
    mStartCameraCreated = false;
    mCameraTargetMtx = new CameraTargetMtx("\x83\x4a\x83\x81\x83\x89\x83\x5e\x81\x5b\x83\x51\x83\x62\x83\x67\x83\x5f\x83\x7e\x81\x5b");
    mRequestCameraManReset = false;
    mRequestCameraLocalOffsetReset = false;
    mIsSubjectiveCamera = false;
    mIsStartSubjectiveCamera = false;
    mSubjectiveFrame = 0;
    mNearZ = -100.0f;
    mIsSubjectiveCalced = false;
    _1F1 = true;
    mIsCameraNG = false;

    MR::connectToSceneCamera(this);
    push(mCameraManGame);
    mTargetMtx.identity();
    mCameraTargetMtx->setMtx(mTargetMtx);
    setInterpolation(0);
    mCameraManSubjective->owned(this);
    mViewMtx.identity();
    MR::createCenterScreenBlur();
}

void CameraDirector::init(const JMapInfoIter& rIter) {
}

void CameraDirector::movement() {
    ::sUpdateCounter++;

    backLastMtx();
    mTargetHolder->movement();
    updateCameraMan();
    calcPose();
    createViewMtx();
    CameraMan* curMan = getCurrentCameraMan();
    if (curMan) {
        curMan->mMatrix.set(MR::getCameraInvViewMtx());
        if (curMan->mPoseParam && mPoseParam2) {
            mPoseParam2->copyFrom(*curMan->mPoseParam);
        }
    }
    calcSubjective();
    mShaker->movement();
    checkStartCondition();
    checkEndOfEventCamera();
    mRotChecker->update();

    mRequestCameraManReset = false;
    mRequestCameraLocalOffsetReset = false;
}

void CameraDirector::setTarget(CameraTargetObj* pTarget) {
    mTargetHolder->set(pTarget);
}

CameraTargetObj* CameraDirector::getTarget() {
    return mTargetHolder->get();
}

void CameraDirector::push(CameraMan* pMan) {
    if (mStack->mCount != 0) {
        getCurrentCameraMan()->deactivate(this);
    }

    mStack->mElements[mStack->mCount++] = pMan;

    pMan->owned(this);
    pMan->activate(this);
}

CameraMan* CameraDirector::pop() {
    CameraMan* man = mStack->mElements[--mStack->mCount];

    man->deactivate(this);
    man->released(this);

    if (mStack->mCount != 0) {
        CameraMan* newMan = getCurrentCameraMan();
        newMan->activate(this);
    }

    return man;
}

void CameraDirector::backLastMtx() {
    if (mIsSubjectiveCalced) {
        MR::setCameraViewMtx(mViewMtx, false, false, TVec3f(0.0f, 0.0f, 0.0f));

        TVec3f invTrans;
        MR::getCameraInvViewMtx().getTrans(invTrans);
    }
}

void CameraDirector::updateTarget() {
}

CameraMan* CameraDirector::getCurrentCameraMan() const {
    if (!mStack || mStack->mCount <= 0) {
        return mCameraManGame;
    }
    CameraMan* man = mStack->mElements[mStack->mCount - 1];
    return man ? man : mCameraManGame;
}

void CameraDirector::updateCameraMan() {
    if (mRequestCameraManReset) {
        resetCameraMan();
    }

    CameraMan* man = getCurrentCameraMan();
    if (man) {
        if (mRequestCameraLocalOffsetReset) {
            man->mRequestLOfsReset = true;
        }
        man->movement();
    }
    controlCameraSE();
}

void CameraDirector::calcPose() {
    switchAntiOscillation();

    CameraMan* man = getCurrentCameraMan();
    if (!man) {
        return;
    }

    if (man->isCollisionOff()) {
        mViewInterpolator->mIsCollisionOff = true;
    }

    if (man->isCorrectingErpPositionOff()) {
        mViewInterpolator->mIsCorrectErpPositionOn = false;
    }

    if (man->isZeroFrameMoveOff()) {
        mOnlyCamera->mIsZeroFrameMoveOff = true;
    }

    mOnlyCamera->calcPose(man);
    mPoseParam1->copyFrom(*mOnlyCamera->mPoseParam);

    // Pillar 2: True 360-Degree Analog Right-Stick Orbital Camera
    static f32 sOrbitYaw = 0.0f;
    static f32 sOrbitPitch = 0.0f;

    // Reset orbit on camera reset button or force camera change
    if (CameraLocalUtil::testCameraPadTriggerReset() ||
        MR::testSubPadButtonC(WPAD_CHAN0) ||
        MR::testCorePadTriggerDown(WPAD_CHAN0) ||
        mViewInterpolator->mIsForceCameraChange) {
        sOrbitYaw = 0.0f;
        sOrbitPitch = 0.0f;
    }

    float rx = 0.0f, ry = 0.0f;
    port_input_get_right_stick(0, &rx, &ry);
    if (port_input_camera_inverted()) {
        rx = -rx;
    }

    if (rx > 0.005f || rx < -0.005f) {
        sOrbitYaw += rx * 0.080f;
        while (sOrbitYaw > 3.14159265f) sOrbitYaw -= 6.2831853f;
        while (sOrbitYaw < -3.14159265f) sOrbitYaw += 6.2831853f;
    }
    if (ry > 0.005f || ry < -0.005f) {
        sOrbitPitch += ry * 0.060f;
        if (sOrbitPitch > 1.20f) sOrbitPitch = 1.20f;
        if (sOrbitPitch < -1.05f) sOrbitPitch = -1.05f;
    }

    if (!mIsSubjectiveCamera && !isEventCameraActive() && !MR::isDemoActive()) {
        if (sOrbitYaw > 0.001f || sOrbitYaw < -0.001f || sOrbitPitch > 0.001f || sOrbitPitch < -0.001f) {
            TVec3f offset = mPoseParam1->mPos - mPoseParam1->mWatchPos;
            f32 dist = offset.length();
            if (dist > 10.0f) {
                TVec3f up = mPoseParam1->mUpVec;
                MR::normalizeOrZero(&up);
                if (up.length() < 0.5f) {
                    up.set(0.0f, 1.0f, 0.0f);
                }

                // Horizontal orbit around Up vector
                TVec3f yawed = rotateVectorAxis(offset, up, sOrbitYaw);

                // Vertical pitch around new Right vector
                TVec3f right = up.cross(yawed);
                MR::normalizeOrZero(&right);

                TVec3f finalOffset = rotateVectorAxis(yawed, right, sOrbitPitch);
                finalOffset.setLength(dist);

                mPoseParam1->mPos = mPoseParam1->mWatchPos + finalOffset;

                // Re-align up vector orthogonal to view direction
                TVec3f look = mPoseParam1->mWatchPos - mPoseParam1->mPos;
                MR::normalizeOrZero(&look);
                TVec3f side = up.cross(look);
                MR::normalizeOrZero(&side);
                mPoseParam1->mUpVec = look.cross(side);
                MR::normalizeOrZero(&mPoseParam1->mUpVec);
            }
        }
    }
}

void CameraDirector::calcSubjective() {
    mViewMtx.set(MR::getCameraViewMtx());
    mIsSubjectiveCalced = true;

    if (MR::isDemoActive()) {
        MR::stopPlayerFpView();
    }

    if (mIsSubjectiveCamera) {
        mSubjectiveFrame++;
        if (mSubjectiveFrame > ::sSubjectiveInterpolateFrame) {
            mSubjectiveFrame = ::sSubjectiveInterpolateFrame;
        }
    } else {
        mSubjectiveFrame--;
        if (mSubjectiveFrame <= 0) {
            mSubjectiveFrame = 0;
            if (mIsStartSubjectiveCamera) {
                if (mNearZ >= 0.0f) {
                    MR::setNearZ(mNearZ);
                }

                mNearZ = -100.0f;
                MR::turnOnDOFInSubjective();
                mIsStartSubjectiveCamera = false;
            }
        }
    }

    if (!mIsStartSubjectiveCamera) {
        return;
    }

    TPos3f inv;
    inv.set(MR::getCameraInvViewMtx());
    mCameraManSubjective->calc();

    TPos3f viewMtx;
    calcViewMtxFromPoseParam(&viewMtx, mCameraManSubjective->mPoseParam);

    s32 posFrame = mSubjectiveFrame - ::sSubjectivePosInterpolateFrame;
    if (posFrame > ::sSubjectiveInterpolateFrame - ::sSubjectivePosInterpolateFrame) {
        posFrame = ::sSubjectiveInterpolateFrame - ::sSubjectivePosInterpolateFrame;
    }

    f32 posRate;
    if (mSubjectiveFrame >= ::sSubjectiveInterpolateFrame - ::sSubjectivePosInterpolateFrame) {
        posRate = 1.0f;
    } else {
        posRate = MR::cos(MR::pi() + posFrame * MR::pi() / (::sSubjectiveInterpolateFrame - ::sSubjectivePosInterpolateFrame)) * 0.5f + 0.5f;
    }

    const CameraPoseParam* param = mCameraManSubjective->mPoseParam;
    TVec3f localPos;
    inv.getTrans(localPos);

    TVec3f newPos = localPos * (1.0f - posRate) + param->mPos * posRate;

    s32 rotFrame = mSubjectiveFrame - ::sSubjectiveRotInterpolateFrame;
    if (rotFrame < 0) {
        rotFrame = 0;
    }

    f32 rotRate = MR::cos(MR::pi() + rotFrame * MR::pi() / (::sSubjectiveInterpolateFrame - ::sSubjectiveRotInterpolateFrame)) * 0.5f + 0.5f;

    TQuat4f invRot;
    inv.getQuat(invRot);
    TQuat4f viewRot;
    viewMtx.getQuat(viewRot);

    TQuat4f newRot;
    newRot.slerp(invRot, viewRot, rotRate);

    viewMtx.makeQuat(newRot);

    TVec3f front;
    viewMtx.getZDir(front);
    viewMtx.setTrans(newPos);
    viewMtx.invert(viewMtx);

    MR::setCameraViewMtx(viewMtx, false, false, TVec3f(0.0f, 0.0f, 0.0f));
}

bool CameraDirector::isInterpolationOff() {
    CameraMan* man = getCurrentCameraMan();
    return (man && man->isInterpolationOff()) || (mViewInterpolator && mViewInterpolator->mIsForceCameraChange);
}

void CameraDirector::switchAntiOscillation() {
    if (isInterpolationOff()) {
        mViewInterpolator->mIsAntiOscillation = false;
    } else {
        mViewInterpolator->mIsAntiOscillation = true;
    }
}

void CameraDirector::createViewMtx() {
    TPos3f view;
    calcViewMtxFromPoseParam(&view, mPoseParam1);

    mViewInterpolator->updateCameraMtx(view, mPoseParam1->getWatchPos(), getTargetObj(), CameraLocalUtil::getFovy(getCurrentCameraMan()));
}

void CameraDirector::checkStartCondition() {
    if (mIsStartCameraActive && getCurrentCameraMan() == mCameraManGame && mStartTime++ > 30 && mTargetHolder->isMoving()) {
        mIsStartCameraActive = false;
        mCameraManGame->endStartPosCamera();
    }
}

void CameraDirector::startEvent(s32 zoneID, const char* pName, const CameraTargetArg& rTargetArg, s32 frame) {
    mViewInterpolator->mIsInterpolationOff = false;
    removeEndEventAtLanding(zoneID, pName);

    if (getCurrentCameraMan() != mCameraManEvent) {
        if (mStack->mCount != 0) {
            if (getCurrentCameraMan() == mCameraManGame) {
                mCameraManEvent->mPoseParam->copyFrom(*mCameraManGame->mPoseParam);
                mCameraManEvent->mMatrix.set(MR::getCameraInvViewMtx());
            }
        }

        push(mCameraManEvent);
    }

    mCameraManEvent->start(zoneID, pName, rTargetArg, frame);
}

void CameraDirector::endEvent(s32 zoneID, const char* pName, bool resetView, s32 frame) {
    if (getCurrentCameraMan() == mCameraManEvent) {
        mCameraManEvent->end(zoneID, pName, frame);

        if (!mCameraManEvent->isActive()) {
            pop();

            if (!isForceCameraChange() && resetView && getCurrentCameraMan() == mCameraManGame) {
                mCameraManGame->mPoseParam->copyFrom(*mPoseParam1);
                mCameraManGame->mMatrix.set(MR::getCameraInvViewMtx());
            }

            mViewInterpolator->mIsInterpolationOff = true;
        }
    }
}

void CameraDirector::endEventAtLanding(s32 zoneID, const char* pName, s32 frame) {
    if (getCurrentCameraMan() == mCameraManEvent) {
        mEvents[mEventNum].mZoneID = zoneID;
        strcpy(mEvents[mEventNum].mName, pName);
        mEvents[mEventNum].mFrame = frame;
        mEventNum++;
    }
}

CameraParamChunkEvent* CameraDirector::getEventParameter(s32 zoneID, const char* pName) {
    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createEventID(zoneID, pName);

    return reinterpret_cast< CameraParamChunkEvent* >(mChunkHolder->getChunk(chunkID));
}

void CameraDirector::requestToResetCameraMan() {
    mRequestCameraManReset = true;
}

void CameraDirector::setInterpolation(u32 time) {
    mViewInterpolator->setInterpolation(time);

    if (time == 0 && !mIsStartCameraActive) {
        mViewInterpolator->mIsCollisionOff = true;
        mCover->cover(2);
    }
}

void CameraDirector::cover(u32 time) {
    mCover->cover(time);
}

void CameraDirector::closeCreatingCameraChunk() {
    mCameraCreator->scanArea();
    mCameraCreator->scanStartPos();
    createStartAnimCamera();
    createTalkCamera();
    createSubjectiveCamera();
    mCameraManGame->closeCreatingCameraChunk();
    mChunkHolder->loadCameraParameters();
    mChunkHolder->sort();
    mIsStartCameraActive = true;
    mCameraManGame->startStartPosCamera(false);
}

void CameraDirector::initCameraCodeCollection(const char* a1, s32 a2) {
    mCameraCreator->initCameraCodeCollection(a1, a2);
}

void CameraDirector::registerCameraCode(u32 code) {
    mCameraCreator->registerCameraCode(code);
}

void CameraDirector::termCameraCodeCollection() {
    mCameraCreator->termCameraCodeCollection();
}

void CameraDirector::declareEvent(s32 zoneID, const char* pName) {
    CameraParamChunkID_Tmp chunkID = CameraParamChunkID_Tmp();
    chunkID.createEventID(zoneID, pName);

    mChunkHolder->createChunk(chunkID, nullptr);
}

void CameraDirector::started() {
    mIsStartCameraActive = false;
    mCameraManGame->endStartPosCamera();
}

void CameraDirector::setTargetActor(const LiveActor* pActor) {
    mTargetHolder->set(pActor);
}

void CameraDirector::setTargetPlayer(const MarioActor* pActor) {
    mTargetHolder->set(pActor);
}

bool CameraDirector::isRotatingHard() const {
    return mRotChecker->mIsRotatingHard;
}

bool CameraDirector::isSubjectiveCamera() const {
    return mIsSubjectiveCamera;
}

bool CameraDirector::isEnableToControl() const {
    bool enable = false;

    CameraMan* curMan = getCurrentCameraMan();
    if (curMan && !curMan->isSubjectiveCameraOff()) {
        bool change = true;
        bool equals = (curMan == mCameraManEvent);

        if (equals && !isEventCameraActive(0, ::sSubjectiveCameraName)) {
            change = false;
        }

        if (change) {
            enable = true;
        }
    }

    return enable;
}

bool CameraDirector::isEnableToRoundLeft() const {
    return true; // Full 360 dual-stick unconstrained rotation
}

bool CameraDirector::isEnableToRoundRight() const {
    return true; // Full 360 dual-stick unconstrained rotation
}

bool CameraDirector::isEnableToReset() const {
    return true;
}

bool CameraDirector::isEventCameraActive(s32 zoneID, const char* pName) const {
    if (getCurrentCameraMan() == mCameraManEvent) {
        return mCameraManEvent->isEventActive(zoneID, pName);
    }

    return false;
}

bool CameraDirector::isEventCameraActive() const {
    return getCurrentCameraMan() == mCameraManEvent;
}

void CameraDirector::startStartPosCamera(bool interpolate) {
    mIsStartCameraActive = true;
    mCameraManGame->startStartPosCamera(interpolate);
}

bool CameraDirector::isInterpolatingNearlyEnd() const {
    if (getCurrentCameraMan() == mCameraManEvent && mCameraManEvent->doesNextChunkHaveInterpolation()) {
        return false;
    }

    return mViewInterpolator->isInterpolatingNearlyEnd();
}

bool CameraDirector::isForceCameraChange() const {
    return mViewInterpolator->mIsForceCameraChange;
}

f32 CameraDirector::getDefaultFovy() const {
    return ::sDefaultFovy;
}

void CameraDirector::startStartAnimCamera() {
    if (mStartCameraCreated) {
        ActorCameraInfo info = ActorCameraInfo();

        MR::startEventCamera(&info, ::sStartAnimCameraName, CameraTargetArg(mCameraTargetMtx), 0);
    }
}

bool CameraDirector::isStartAnimCameraEnd() const {
    if (mStartCameraCreated) {
        return isAnimCameraEnd(0, ::sStartAnimCameraName);
    }

    return true;
}

u32 CameraDirector::getStartAnimCameraFrame() const {
    if (mStartCameraCreated) {
        return getAnimCameraFrame(0, ::sStartAnimCameraName);
    }

    return 0;
}

void CameraDirector::endStartAnimCamera() {
    ActorCameraInfo info = ActorCameraInfo();
    MR::endEventCamera(&info, ::sStartAnimCameraName, true, 0);
}

void CameraDirector::startTalkCamera(const TVec3f& rPosition, const TVec3f& rUp, f32 axisX, f32 axisY, s32 frame) {
    CameraParamChunkEvent* chunk = getEventParameter(0, ::sTalkCameraName);

    if (chunk != nullptr) {
        chunk->mGeneralParam->mWPoint.set(rPosition);
        chunk->mGeneralParam->mUp.set(rUp);

        CameraGeneralParam* generalParam = chunk->mGeneralParam;
        generalParam->mAxis.x = axisX;
        generalParam->mAxis.y = axisY;
        generalParam->mAxis.z = 0.0f;

        CameraTargetArg targetArg;
        MR::setCameraTargetToPlayer(&targetArg);
        startEvent(0, ::sTalkCameraName, targetArg, frame);
    }
}

void CameraDirector::endTalkCamera(bool resetView, s32 frame) {
    endEvent(0, ::sTalkCameraName, resetView, frame);
}

void CameraDirector::startSubjectiveCamera(s32 camType) {
    mIsStartCameraActive = false;
    mCameraManGame->endStartPosCamera();
    mIsSubjectiveCamera = true;

    if (!mIsStartSubjectiveCamera) {
        mIsStartSubjectiveCamera = true;
        mSubjectiveFrame = 0;

        mCameraManSubjective->activate(this);
        mNearZ = MR::getNearZ();

        MR::setNearZ(10.0f);
        MR::turnOffDOFInSubjective();
    }

    if (mSubjectiveFrame < ::sSubjectiveInterpolateFrame) {
        MR::startCenterScreenBlur(::sSubjectiveInterpolateFrame, 15.0f, 80, 5, 10);
    }
}

void CameraDirector::endSubjectiveCamera(s32 camType) {
    bool forceEnd = camType == 0 || camType == 1;  // type ?? or ForceClose

    if (mIsSubjectiveCamera == true) {
        mIsSubjectiveCamera = false;

        if (!forceEnd) {
            MR::startCenterScreenBlur(mSubjectiveFrame, 15.0f, 80, 5, 10);
        }
    }

    if (mIsStartSubjectiveCamera && forceEnd) {
        mSubjectiveFrame = 0;
    }
}

bool CameraDirector::isAnimCameraEnd(s32 zoneID, const char* pName) const {
    if (getCurrentCameraMan() == mCameraManEvent) {
        return mCameraManEvent->isAnimCameraEnd(zoneID, pName);
    }

    return true;
}

u32 CameraDirector::getAnimCameraFrame(s32 zoneID, const char* pName) const {
    if (getCurrentCameraMan() == mCameraManEvent) {
        return mCameraManEvent->getAnimCameraFrame(zoneID, pName);
    }

    return 0;
}

// getAnimCameraCurrentFrame

void CameraDirector::pauseOnAnimCamera(s32 zoneID, const char* pName) {
    if (getCurrentCameraMan() == mCameraManEvent) {
        mCameraManEvent->pauseOnAnimCamera(zoneID, pName);
    }
}

void CameraDirector::pauseOffAnimCamera(s32 zoneID, const char* pName) {
    if (getCurrentCameraMan() == mCameraManEvent) {
        mCameraManEvent->pauseOffAnimCamera(zoneID, pName);
    }
}

void CameraDirector::zoomInGameCamera() {
    mCameraManGame->zoomIn();
}

void CameraDirector::zoomOutGameCamera() {
    mCameraManGame->zoomOut();
}

void CameraDirector::checkEndOfEventCamera() {
    if (mEventNum != 0 && mTargetHolder->isOnGround()) {
        for (u32 i = 0; i < mEventNum; i++) {
            endEvent(mEvents[i].mZoneID, mEvents[i].mName, true, mEvents[i].mFrame);
        }

        mEventNum = 0;
    }
}

void CameraDirector::createTalkCamera() {
    declareEvent(0, ::sTalkCameraName);

    CameraParamChunkEvent* chunk = getEventParameter(0, ::sTalkCameraName);
    if (chunk != nullptr) {
        chunk->setCameraType("CAM_TYPE_TALK", mHolder);
        chunk->_64 = true;
    }
}

void CameraDirector::controlCameraSE() {
    mIsCameraNG = false;

    if (MR::isPlayerDead()) {
        return;
    }

    if (mIsSubjectiveCamera) {
        if (MR::testCorePadTriggerLeft(WPAD_CHAN0) || MR::testCorePadTriggerRight(WPAD_CHAN0) || MR::testFpViewStartTrigger()) {
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_NG");
                mIsCameraNG = true;
            }
        }

        return;
    }

    if (CameraLocalUtil::testCameraPadTriggerRoundLeft()) {
        CameraMan* man = getCurrentCameraMan();
        if (man) {
            man->roundLeft();
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_MOVE");
            }
        }
    }

    if (CameraLocalUtil::testCameraPadTriggerRoundRight()) {
        CameraMan* man = getCurrentCameraMan();
        if (man) {
            man->roundRight();
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_MOVE");
            }
        }
    }

    if (CameraLocalUtil::testCameraPadTriggerReset()) {
        if (isEnableToReset()) {
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_RESET");
                MR::startSystemSE("SE_SY_CAMERA_MOVE");
            }
        } else {
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_NG");
                mIsCameraNG = true;
            }
        }
    }

    if (MR::isPlayerInBind()) {
        if (MR::testFpViewStartTrigger()) {
            if (isPlayableCameraSE(false)) {
                MR::startSystemSE("SE_SY_CAMERA_NG");
                mIsCameraNG = true;
            }
        }
    }

    if (MR::testCorePadTriggerDown(WPAD_CHAN0)) {
        if (isPlayableCameraSE(false)) {
            MR::startSystemSE("SE_SY_CAMERA_NG");
            mIsCameraNG = true;
        }
    }
}

void CameraDirector::removeEndEventAtLanding(s32 zoneID, const char* pName) {
    if (mEventNum == 0) {
        return;
    }

    for (s32 idx = 0; idx < mEventNum; idx++) {
        if (mEvents[idx].mZoneID == zoneID && strcmp(mEvents[idx].mName, pName) == 0) {
            if (mEventNum - 1 == idx) {
                mEventNum = 0;
                return;
            }

            mEvents[idx].mZoneID = mEvents[mEventNum - 1].mZoneID;
            strcpy(mEvents[idx].mName, mEvents[mEventNum - 1].mName);
            mEvents[idx].mFrame = mEvents[mEventNum - 1].mFrame;
            mEventNum--;
            return;
        }
    }
}

void CameraDirector::calcViewMtxFromPoseParam(TPos3f* pMtx, const CameraPoseParam* pParam) {
    TVec3f front = pParam->mWatchPos - pParam->mPos;
    MR::normalizeOrZero(&front);
    TVec3f side = pParam->mUpVec.cross(front);
    MR::normalizeOrZero(&side);
    TVec3f up = front.cross(side);
    MR::normalizeOrZero(&up);

    pMtx->setXDir(-side);
    pMtx->setYDir(up);
    pMtx->setZDir(-front);
    pMtx->setTrans(pParam->mPos);

    TPos3f rot;
    rot.makeRotate(TVec3f(0.0f, 0.0f, 1.0f), pParam->mRoll);
    pMtx->concat(*pMtx, rot);
}

bool CameraDirector::isPlayableCameraSE(bool checkSubjective) {
    if (MR::isDemoActive()) {
        return false;
    }

    if (MR::isPowerStarGetDemoActive()) {
        return false;
    }

    if (MR::isStageStateScenarioOpeningCamera()) {
        return false;
    }

    if (MR::isExecScenarioStarter()) {
        return false;
    }

    if (MR::isPlayerDead()) {
        return false;
    }

    if (MR::isEqualStageName("FileSelect")) {
        return false;
    }

    if (MR::isEqualStageName("EpilogueDemoStage")) {
        return false;
    }

    if (checkSubjective && mIsSubjectiveCamera) {
        return false;
    }

    return true;
}

void CameraDirector::resetCameraMan() {
    setInterpolation(0);

    CameraMan* man = getCurrentCameraMan();
    if (!man || !mTargetHolder || !mTargetHolder->get()) {
        return;
    }

    CameraTargetObj* target1 = mTargetHolder->get();
    CameraTargetObj* target2 = mTargetHolder->get();
    CameraTargetObj* target3 = mTargetHolder->get();

    TVec3f newPos = target3->getPosition() - target2->getFrontVec() * 800.0f + target1->getUpVec() * 300.0f;

    CameraLocalUtil::setPos(man, newPos);
    CameraLocalUtil::setWatchPos(man, mTargetHolder->get()->getPosition());
    CameraLocalUtil::setUpVec(man, mTargetHolder->get()->getUpVec());
    CameraLocalUtil::setWatchUpVec(man, mTargetHolder->get()->getUpVec());

    man->deactivate(this);
    man->activate(this);

    mOnlyCamera->mIsResetting = true;
}

void CameraDirector::createStartAnimCamera() {
    void* data = nullptr;
    s32 size = 0;
    MR::getCurrentScenarioStartAnimCameraData(&data, &size);

    if (size > 0) {
        ActorCameraInfo info = ActorCameraInfo();
        MR::declareEventCameraAnim(&info, ::sStartAnimCameraName, data);
        mStartCameraCreated = true;
    }
}

void CameraDirector::createSubjectiveCamera() {
    declareEvent(0, ::sSubjectiveCameraName);

    CameraParamChunkEvent* chunk = getEventParameter(0, ::sSubjectiveCameraName);
    if (chunk != nullptr) {
        chunk->setCameraType("CAM_TYPE_SUBJECTIVE", mHolder);
        chunk->_64 = true;
    }
}

CameraDirector* MR::getCameraDirector() {
    return MR::getSceneObj< CameraDirector >(SceneObj_CameraDirector);
}
