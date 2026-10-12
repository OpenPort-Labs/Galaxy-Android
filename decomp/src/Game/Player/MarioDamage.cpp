#include "Game/Player/MarioDamage.hpp"
#include "port/port.h"
#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/Map/CollisionCode.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioAbyssDamage.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioBlown.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioDarkDamage.hpp"
#include "Game/Player/MarioFaint.hpp"
#include "Game/Player/MarioFireDamage.hpp"
#include "Game/Player/MarioFireDance.hpp"
#include "Game/Player/MarioFireRun.hpp"
#include "Game/Player/MarioFreeze.hpp"
#include "Game/Player/MarioMapCode.hpp"
#include "Game/Player/MarioParalyze.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"

namespace {
    const char sDamageAirAnimation[] = "\x92\x86\x83\x5f\x83\x81\x81\x5b\x83\x57\x8b\xf3\x92\x86";
    const char sDamageLandAnimation[] = "\x92\x86\x83\x5f\x83\x81\x81\x5b\x83\x57\x92\x85\x92\x6e";
    const char sBackDamageAirAnimation[] = "\x92\x86\x8c\xe3\x83\x5f\x83\x81\x81\x5b\x83\x57\x8b\xf3\x92\x86";
    const char sBackDamageLandAnimation[] = "\x92\x86\x8c\xe3\x83\x5f\x83\x81\x81\x5b\x83\x57\x92\x85\x92\x6e";
}  // namespace

void MarioDamage_FORCE_MATCH_SDATA2() {
    (void)1.0f;
    (void)0.0f;
    (void)0.5f;
    (void)2.0f;
    (void)10.0f;
    (void)5.0f;
    (void)0.949999988f;
    (void)30.0f;
    (void)20.0f;
    (void)50.0f;
    (void)-10.0f;
    (void)0.699999988f;
    (void)0.100000001f;
    (void)0.25f;
}

bool Mario::isDamaging() const {
    if (isAnimationRun("\x90\x85\x8f\xe3\x83\x5f\x83\x81\x81\x5b\x83\x57\x92\x86")) {
        return true;
    }

    if (_41E) {
        return true;
    }

    if (mMovementStates._1B) {
        return true;
    }

    if (mMovementStates._27) {
        return true;
    }

    if (mMovementStates._2C) {
        return true;
    }

    if (_10.jumping) {
        return true;
    }

    if (_10._14) {
        return true;
    }

    if (_10._18) {
        return true;
    }

    switch (getCurrentStatus()) {
    case MarioStatus_FireDamage:
    case MarioStatus_FireDance:
    case MarioStatus_FireRun:
    case MarioStatus_Paralyze:
    case MarioStatus_AbyssDamage:
    case MarioStatus_Freeze:
    case MarioStatus_Crush:
        return true;
    case MarioStatus_Damage:
        return mDamage->_12;
    case MarioStatus_Faint:
        return mFaint->mTookDamage;
    default:
        return false;
    }
}

bool Mario::damageLarge(const TVec3f& rDirection) {
    if (damage(rDirection)) {
        if (isStatusActive(MarioStatus_Swim)) {
            mSwim->_AD = 1;
        } else {
            mDamage->setVecSize(mActor->getConst().getTable()->mJumpDistLargeDamage, mActor->getConst().getTable()->mJumpHeightLargeDamage);
            playSound("\x93\x8a\x82\xb0\x82\xe7\x82\xea");
        }

        return true;
    }

    return false;
}

void Mario::decDamageAfterTimer() {
    MarioDamage* damage = mDamage;
    if (damage->_16) {
        damage->_16--;
    }

    MarioFaint* faint = mFaint;
    if (faint->_14) {
        faint->_14--;
    }

    MarioParalyze* paralyze = mParalyze;
    if (paralyze->_16) {
        paralyze->_16--;
    }

    MarioFreeze* freeze = mFreeze;
    if (freeze->_1C) {
        freeze->_1C--;
    }

    mFireDamage->decAfterTimer();
    if (_41E) {
        _41E--;
    }
}

bool Mario::checkDamage() {
    decDamageAfterTimer();
    if (mMovementStates._1F) {
        return false;
    }

    if (mActor->_EA4) {
        return false;
    }

    if (MR::isDemoActive()) {
        return false;
    }

    if (isStatusActive(MarioStatus_Talk)) {
        return false;
    }

    if (isStatusActive(MarioStatus_Recovery) || isInvincible()) {
        mMovementStates._1B = false;
        mMovementStates._27 = false;
        mMovementStates._2C = false;
        _10.jumping = false;
        _10._14 = false;
        _10._18 = false;
        mFaint->mNoDamage = false;
        return false;
    }

    if (isStatusActive(MarioStatus_Swim)) {
        checkWaterDamage();
        return false;
    }

    if (mMovementStates._1B) {
        mMovementStates._1B = false;
        changeStatus(mDamage);
        mMovementStates._27 = false;
        mFaint->mNoDamage = false;
        mMovementStates._2C = false;
        return true;
    }

    if (mMovementStates._27) {
        mMovementStates._27 = false;
        mMovementStates._2C = false;
        changeStatus(mFaint);
        return true;
    }

    if (mMovementStates._2C) {
        mMovementStates._2C = false;
        changeStatus(mBlown);
        return true;
    }

    if (_10.jumping) {
        _10.jumping = false;
        doFireDanceWithInitialDamage(1);
        return true;
    }

    if (_10._14) {
        _10._14 = false;
        doParalyze();
        return true;
    }

    if (_10._18) {
        _10._18 = false;
        if (tryCrush()) {
            return true;
        }
    }

    checkKarikariDamage();
    return false;
}

u16 Mario::getDamageAfterTimer() const {
    u16 timer = mDamage->_16;
    if (timer < mFaint->_14) {
        timer = mFaint->_14;
    }

    if (timer < mParalyze->_16) {
        timer = mParalyze->_16;
    }

    if (timer < mFreeze->_1C) {
        timer = mFreeze->_1C;
    }

    if (timer < mFireDamage->_12) {
        timer = mFireDamage->_12;
    }

    if (timer < _41E) {
        timer = _41E;
    }

    return timer;
}

bool Mario::damageFloorCheck() {
    if (mMovementStates._1F) {
        return false;
    }

    if (_1C._16) {
        return false;
    }

    switch (_960) {
    case CollisionFloorCode_Needle:
        if (checkCurrentFloorCodeSevere(CollisionFloorCode_Needle) && doNeedleWithInitialDamage(mGroundPolygon)) {
            return true;
        }

        break;
    case 0x81:
        if (checkCurrentFloorCodeSevere(0x81) && doFireDanceWithInitialDamage(1)) {
            return true;
        }

        break;
    case CollisionFloorCode_Death:
        MarioAccess::forceKill(3, 0);
        return true;
    case CollisionFloorCode_DamageFire:
        if (checkCurrentFloorCodeSevere(CollisionFloorCode_DamageFire) && doFireDanceWithInitialDamage(1)) {
            return true;
        }

        break;
    case CollisionFloorCode_DamageNormal:
        if (isDamaging()) {
            return false;
        }

        if (damage(_368 * 10.0f)) {
            return true;
        }

        break;
    case CollisionFloorCode_DamageElectric:
        if (doParalyze()) {
            return true;
        }

        break;
    case CollisionFloorCode_PullBack:
        if (doRecovery()) {
            return true;
        }

        break;
    }

    return false;
}

bool Mario::damageWallCheck() {
    if (mMovementStates._1F) {
        return false;
    }

    TVec3f normal;
    if (checkWallCodeNorm(CollisionWallCode_Rebound, &normal, false)) {
        return doFlipJump(normal * 5.0f);
    }

    if (checkWallFloorCode(CollisionFloorCode_Death)) {
        mActor->forceKill(3);
        return true;
    }

    if (checkWallFloorCode(CollisionFloorCode_DamageFire) && doFireDanceWithInitialDamage(1)) {
        return true;
    }

    if (checkWallFloorCode(CollisionFloorCode_Needle) && doNeedleWithInitialDamage(1)) {
        return true;
    }

    if (checkWallFloorCode(CollisionFloorCode_DamageElectric) && doParalyze()) {
        return true;
    }

    if (checkWallFloorCode(CollisionFloorCode_PullBack) && doRecovery()) {
        return true;
    }

    if (checkWallFloorCode(CollisionFloorCode_DamageNormal) && damage(getWallNorm() * 10.0f)) {
        return true;
    }

    return false;
}

bool Mario::damagePolygonCheck(const Triangle* pTriangle) {
    if (mMovementStates._1F) {
        return false;
    }

    switch (_95C->getCode(pTriangle)) {
    case 0x81:
        if (doFireDanceWithInitialDamage(1)) {
            return true;
        }

        break;
    case CollisionFloorCode_Death:
        MarioAccess::forceKill(3, 0);
        return true;
    case CollisionFloorCode_DamageFire:
        if (doFireDanceWithInitialDamage(1)) {
            return true;
        }

        break;
    case CollisionFloorCode_DamageNormal:
        mSwim->addDamage(*MR::getNormal(pTriangle) * 10.0f);
        return true;
    case CollisionFloorCode_DamageElectric:
        if (doParalyze()) {
            return true;
        }

        break;
    case CollisionFloorCode_PullBack:
        if (doRecovery()) {
            return true;
        }

        break;
    case CollisionFloorCode_Needle:
        doNeedleWithInitialDamage(pTriangle);
        return true;
    case CollisionFloorCode_SinkDeath:
        return true;
    }

    return false;
}

bool Mario::flipLarge(const TVec3f& rDirection) {
    if (damage(rDirection)) {
        if (isStatusActive(MarioStatus_Swim)) {
            mSwim->_AD = 1;
            mSwim->mDamageType = 1;
        } else {
            mDamage->_11 = 1;
        }

        mDamage->setVecSize(rDirection.length(), 0.0f);
        return true;
    }

    return false;
}

bool Mario::isEnableAddDamage() const {
    if (getCurrentStatus() == MarioStatus_Talk) {
        return false;
    }

    if (isDamaging()) {
        return false;
    }

    if (mActor->_390) {
        return false;
    }

    if (isInvincible()) {
        return false;
    }

    return getDamageAfterTimer() == 0;
}

bool Mario::damage(const TVec3f& rDirection) {
    _7C4 = rDirection;
    if (!isEnableAddDamage()) {
        return false;
    }

    if (mFaint->_14 || mDamage->_16 || mMovementStates._1B) {
        return false;
    }

    if (mMovementStates._F) {
        forceStopTornado();
    }

    mDamage->setVec(rDirection);
    stopWalk();
    forceStopTornado();
    mActor->damageDropThrowMemoSensor();
    if (isStatusActive(MarioStatus_Damage)) {
        closeStatus(mDamage);
    }

    mMovementStates._1B = true;
    return true;
}

MarioDamage::MarioDamage(MarioActor* pActor) : MarioState(pActor, MarioStatus_Damage) {
    _11 = 0;
    _12 = 0;
    _14 = 0;
    _16 = 0;
    _18 = 0;
    _1C.zero();
    _28 = nullptr;
    _2C = nullptr;
}

bool MarioDamage::start() {
    _14 = 0;
    _18 = 0;
    if (_1C.dot(getPlayer()->mFrontVec) > 0.0f) {
        changeAnimationNonStop("\x92\x86\x8c\xe3\x83\x5f\x83\x81\x81\x5b\x83\x57");
        _28 = ::sBackDamageAirAnimation;
        _2C = ::sBackDamageLandAnimation;
        getPlayer()->setFrontVecKeepUp(_1C);
    } else {
        changeAnimationNonStop("\x92\x86\x83\x5f\x83\x81\x81\x5b\x83\x57");
        _28 = ::sDamageAirAnimation;
        _2C = ::sDamageLandAnimation;
        getPlayer()->setFrontVecKeepUp(-_1C);
    }

    if (!_11) {
        playEffect("\x83\x5f\x83\x81\x81\x5b\x83\x57");
    }

    startPadVib(3);
    getPlayer()->mMovementStates._1 = false;
    getPlayer()->mMovementStates.jumping = true;
    getPlayer()->mMovementStates._B = false;
    getPlayer()->mMovementStates._3E = 0;
    _1C += -mActor->_240 * mActor->getConst().getTable()->mJumpHeightDamage;
    getPlayer()->mJumpVec = _1C;
    addVelocity(_1C);

    _12 = !_11;
    if (_11) {
        playSound("\x90\xba\x93\x8a\x82\xb0\x82\xe7\x82\xea");
        playSound("\x93\x8a\x82\xb0\x82\xe7\x82\xea");
        _11 = 0;
        mActor->resetPlayerModeOnNoDamage();
    } else {
        playSound("\x90\xba\x8f\xac\x83\x5f\x83\x81\x81\x5b\x83\x57");
        playSound("\x83\x5f\x83\x81\x81\x5b\x83\x57");
        mActor->decLifeMiddle();
        mActor->resetPlayerModeOnDamage();
    }

    return true;
}

void MarioDamage::setVec(const TVec3f& rDirection) {
    MR::vecKillElement(rDirection, mActor->_240, &_1C);
    _1C.setLength(mActor->getConst().getTable()->mJumpDistDamage);
}

void MarioDamage::setVecSize(f32 horizontal, f32 vertical) {
    _1C.setLength(horizontal);
    _1C += -mActor->_240 * vertical;
}

void MarioDamage::stopHead(const TVec3f& rNormal) {
    if (!_18) {
        TVec3f horizontal;
        f32 verticalSpeed = MR::vecKillElement(_1C, mActor->_240, &horizontal);
        f32 towardWall = MR::vecKillElement(horizontal, rNormal, &_1C);
        _1C += mActor->_240 * verticalSpeed;
        if (towardWall < 0.0f) {
            _1C += rNormal * -towardWall * 0.5f;
        }
    } else {
        TVec3f horizontal;
        MR::vecKillElement(rNormal, getAirGravityVec(), &horizontal);
        if (!MR::normalizeOrZero(&horizontal)) {
            f32 towardWall = MR::vecKillElement(_1C, rNormal, &_1C);
            if (towardWall < 0.0f) {
                _1C += rNormal * -towardWall * 0.5f;
            }
        }
    }
}

bool MarioDamage::update() {
    _14++;
    if (mActor->_EA4) {
        return true;
    }

    switch (_18) {
    case 0:
        addVelocity(_1C);
        _1C += mActor->_240 * mActor->getConst().getTable()->mGravityDamage;
        if (_14 > 20) {
            if (_28) {
                changeAnimation(_28);
            }

            if (getPlayer()->_1C._0) {
                f32 vertical = MR::vecKillElement(_1C, getAirGravityVec(), &_1C);
                _1C.x *= 0.95f;
                _1C.y *= 0.95f;
                _1C.z *= 0.95f;
                _1C += getAirGravityVec() * vertical;
            }
        }

        if (getPlayer()->mMovementStates._1) {
            MarioActor* actor;
            if (getPlayer()->mVerticalSpeed > 30.0f && (actor = mActor, actor->selectDamagePop(getSensor(getGroundPolygon())))) {
                Mario* player = getPlayer();
                _1C += player->_368 * 20.0f;
                getPlayer()->mMovementStates._1 = false;
                getPlayer()->mMovementStates.jumping = true;
            } else {
                getPlayer()->mMovementStates.jumping = false;
                playSound("\x90\x81\x82\xc1\x94\xf2\x82\xd1\x93\x7c\x82\xea");
                changeAnimation(_2C);
                playEffect("\x8b\xa4\x92\xca\x83\x5f\x83\x81\x81\x5b\x83\x57\x92\x85\x92\x6e");
                MR::vecKillElement(_1C, mActor->_240, &_1C);
                _14 = 0;
                _18++;
                if (!mActor->mHealth) {
                    _18 = 2;
                }
            }
        } else if (!mActor->mHealth) {
            if (_14 > 240) {
                mActor->forceGameOverAbyss();
            }
        } else if (_14 > 360) {
            mActor->forceGameOverAbyss();
        }

        break;
    case 1:
        if (!getPlayer()->mMovementStates._1) {
            getPlayer()->mMovementStates.jumping = true;
            _18 = 0;
            break;
        }

        MR::vecKillElement(_1C, getAirGravityVec(), &_1C);
        addVelocity(_1C);
        _1C.x *= 0.95f;
        _1C.y *= 0.95f;
        _1C.z *= 0.95f;
        if (!isAnimationRun(_2C)) {
            return false;
        }

        if (_14 > 15 && checkTrgA()) {
            getPlayer()->tryJump();
            return false;
        }

        break;
    case 2:
        if (!getPlayer()->mMovementStates._1) {
            getPlayer()->mMovementStates.jumping = true;
            _18 = 0;
            break;
        }

        if (_14 == 40) {
            if (!mActor->mHealth) {
                mActor->forceGameOver();
            } else {
                return false;
            }
        }

        break;
    }

    getPlayer()->mJumpVec = _1C;
    return true;
}

bool MarioDamage::close() {
    stopAnimation("\x83\x5f\x83\x81\x81\x5b\x83\x57");
    stopAnimation("\x83\x5f\x83\x81\x81\x5b\x83\x57\x92\x85\x92\x6e", "\x8a\xee\x96\x7b");
    if (_12) {
        _16 = 120;
    }

    return true;
}

bool MarioDamage::notice() {
    if (!mActor->mHealth) {
        if (getNoticedStatus() == MarioStatus_Swim) {
            mActor->forceGameOver();
        }

        return true;
    }

    return false;
}

MarioFireDamage::MarioFireDamage(MarioActor* pActor) : MarioState(pActor, MarioStatus_FireDamage) {
    _12 = 0;
}

void MarioFireDamage::decAfterTimer() {
    if (_12 && !isStatusActiveID(MarioStatus_FireDance) && !isStatusActiveID(MarioStatus_FireRun)) {
        _12--;
    }
}

bool Mario::doAbyssDamage() {
    if (port_is_infinite_health()) {
        mActor->teleportToOriginalSpot();
        return false;
    }

    if (getCurrentStatus() == MarioStatus_AbyssDamage) {
        return false;
    }

    stopWalk();
    mActor->damageDropThrowMemoSensor();
    MR::removeAllClingingKarikari();
    mActor->_A6E = 0;
    changeStatus(mAbyssDamage);
    return true;
}

MarioAbyssDamage::MarioAbyssDamage(MarioActor* pActor) : MarioState(pActor, MarioStatus_AbyssDamage) {
    _12 = 0;
    _14 = 0;
    _18.zero();
}

bool MarioAbyssDamage::start() {
    if (port_is_infinite_health()) {
        mActor->teleportToOriginalSpot();
        return false;
    }
    _12 = 0;
    _14 = 0;
    mActor->forceGameOverAbyss();
    return false;
}

bool MarioAbyssDamage::update() {
    addTrans(_18, "Module");
    switch (_14) {
    case 0:
        _14++;
        _12 = 120;
        MR::requestStartGameOverDemo();
        break;
    case 1:
        if (_12) {
            _12--;
        }

        if (!_12) {
            mActor->forceGameOverAbyss();
            return false;
        }

        break;
    }

    return true;
}

bool MarioAbyssDamage::close() {
    return true;
}

const char* MarioDamage_FORCE_MATCH_DATA(u32 index) {
    switch (index) {
    case 0:
        return "\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57";
    case 1:
        return "\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57\x90\xc2\x89\x8c";
    default:
        return "\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57\x89\x8c";
    }
}

void Mario::connectToFireRun() {
    if (mActor->mHealth) {
        changeStatus(mFireRun);
        stopJump();
        mFireRun->_12 = 1;
    }
}

MarioFireRun::MarioFireRun(MarioActor* pActor) : MarioState(pActor, MarioStatus_FireRun) {
    _12 = 0;
    _14 = 0;
    _18 = 0.0f;
}

bool MarioFireRun::start() {
    _12 = mActor->getConst().getTable()->mFireRunTimer1;
    _14 = 0;
    stopAnimationUpper(nullptr);
    changeAnimation("\x83\x74\x83\x40\x83\x43\x83\x41\x83\x89\x83\x93\x91\x4f\x92\x9b");
    if (!getPlayer()->mMovementStates._1) {
        _18 = -mActor->getConst().getTable()->mFireRunFirstJump;
    } else {
        _18 = 0.0f;
    }

    return true;
}

bool MarioFireRun::move() {
    if (getStickX() != 0.0f || getStickY() != 0.0f) {
        const TVec3f& padDir = getWorldPadDir();
        getPlayer()->setFrontVecKeepUp(padDir, mActor->getConst().getTable()->mFireRunTurnRatio);
    }

    if (getPlayer()->checkTrgA() || mActor->isRequestJump()) {
        Mario* player = getPlayer();
        player->mWalkSpeed = 1.0f;
        getPlayer()->tryJump();
        return false;
    }

    return true;
}

bool MarioFireRun::update() {
    switch (_14) {
    case 0:
        if (!getPlayer()->mMovementStates._1) {
            getPlayer()->mJumpVec = getAirGravityVec() * _18;
            addVelocity(getAirGravityVec() * _18);
            _18 += mActor->getConst().getTable()->mFireRunGravity;
            if (_18 > 50.0f) {
                _18 = 50.0f;
            }

            const TVec3f& velocity = mActor->_288;
            if (velocity.dot(getAirGravityVec()) < -10.0f) {
                _18 = 0.0f;
                addVelocity(getFrontVec() * 5.0f);
            }
        } else if (_12) {
            _12--;
        } else {
            _14++;
            _12 = mActor->getConst().getTable()->mFireRunTimer2;
            if (!mActor->mHealth) {
                _12 >>= 1;
            }

            _18 = 0.0f;
            changeAnimation("\x89\x8a\x82\xcc\x83\x89\x83\x93\x83\x69\x81\x5b");
        }

        break;
    case 1:
        playSound("\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57\x89\x8a\x8f\xe3\x92\x86");
        if (!getPlayer()->mMovementStates._1) {
            _14 = 2;
            _12 += mActor->getConst().getTable()->mFireRunTimer3;
        }

        addVelocity(getFrontVec() * mActor->getConst().getTable()->mFireRunSpeed);
        if (_12) {
            _12--;
        }

        if (!_12) {
            _12 = mActor->getConst().getTable()->mFireRunTimer3;
            _14++;
        }

        return move();
    case 2:
        if (mActor->isEnableNerveChange() && getStickP() > 0.7f) {
            return false;
        }

        if (!getPlayer()->mMovementStates._1) {
            getPlayer()->mJumpVec = getAirGravityVec() * _18;
            addVelocity(getAirGravityVec() * _18);
            _18 += mActor->getConst().getTable()->mFireRunGravity;
            if (_18 > 50.0f) {
                _18 = 50.0f;
            }

            const TVec3f& velocity = mActor->_288;
            if (velocity.dot(getAirGravityVec()) < -10.0f) {
                _18 = 0.0f;
                addVelocity(getFrontVec() * 5.0f);
            }

            if (_12) {
                _12--;
            }

            break;
        }

        const MarioConstTable* table = mActor->getConst().getTable();
        if (_12 > table->mFireRunTimer3) {
            addVelocity(getFrontVec() * table->mFireRunSpeed);
        } else {
            addVelocity(getFrontVec() * table->mFireRunSpeed * _12 / table->mFireRunTimer3);
        }

        if (mActor->isEnableNerveChange()) {
            if (_12) {
                _12--;
            }

            if (!_12) {
                return false;
            }
        }

        return move();
    }

    return true;
}

bool MarioFireRun::close() {
    if (!mActor->mHealth) {
        mActor->forceGameOver();
    }

    if (getPlayer()->mMovementStates.jumping) {
        stopAnimation("\x89\x8a\x82\xcc\x83\x89\x83\x93\x83\x69\x81\x5b", "\x97\x8e\x89\xba");
    } else {
        stopAnimation("\x89\x8a\x82\xcc\x83\x89\x83\x93\x83\x69\x81\x5b", "\x8a\xee\x96\x7b");
        if (getStickP() < 0.1f) {
            playSound("\x90\xba\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57\x8f\x49\x97\xb9");
        }
    }

    stopEffect("\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57\x89\x8c");
    stopEffect("\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57\x90\xc2\x89\x8c");
    mActor->_1B4 = 0;
    return true;
}

bool MarioFireRun::notice() {
    return false;
}

bool Mario::doFireDanceWithInitialDamage(u8 amount) {
    if (mMovementStates._1F) {
        return false;
    }

    bool started = doFireDance();
    if (started) {
        for (u32 i = 0; i < amount; i++) {
            mActor->decLife(0);
        }

        if (!mActor->mHealth) {
            mActor->forceGameOverNonStop();
        }
    }

    return started;
}

bool Mario::doFireObjHitWithInitialDamage() {
    if (isEnableAddDamage() == false) {
        return false;
    }

    return doFireDanceWithInitialDamage(1);
}

bool Mario::doNeedleWithInitialDamage(u8 amount) {
    if (mMovementStates._1F) {
        return false;
    }

    if (getPlayerMode() == PlayerMode_Teresa) {
        doTeresaReflection(getWallNorm(), false);
        return false;
    }

    bool started = doNeedle(nullptr);
    if (started) {
        for (u32 i = 0; i < amount; i++) {
            mActor->decLife(0);
        }

        if (!mActor->mHealth) {
            mActor->forceGameOverNonStop();
        }
    }

    return started;
}

bool Mario::doNeedleWithInitialDamage(const Triangle* pTriangle) {
    if (mMovementStates._1F) {
        return false;
    }

    if (getPlayerMode() == PlayerMode_Teresa) {
        doTeresaReflection(*MR::getNormal(pTriangle), false);
        return false;
    }

    bool started = doNeedle(pTriangle);
    if (started) {
        mActor->decLife(0);
        if (!mActor->mHealth) {
            mActor->forceGameOverNonStop();
        }
    }

    return started;
}

bool Mario::doNeedle(const Triangle* pTriangle) {
    if (getCurrentStatus() == MarioStatus_FireDamage) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_FireRun) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_FireDance) {
        return false;
    }

    if (mMovementStates._1B) {
        return false;
    }

    if (getPlayerMode() == PlayerMode_Teresa) {
        if (pTriangle != nullptr) {
            doTeresaReflection(*MR::getNormal(pTriangle), false);
        }

        return false;
    }

    if (isInvincible()) {
        return false;
    }

    mActor->resetPlayerModeOnDamage();
    getPlayer()->mMovementStates._B = false;
    getPlayer()->mMovementStates._A = false;
    mActor->damageDropThrowMemoSensor();
    mFireDance->_29 = 1;
    changeStatus(mFireDance);
    return true;
}

bool Mario::doFireDance() {
    if (getCurrentStatus() == MarioStatus_Paralyze) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_FireDamage) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_FireRun) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_FireDance) {
        return false;
    }

    if (mMovementStates._1B) {
        return false;
    }

    if (isInvincible()) {
        return false;
    }

    if (getPlayerMode() == PlayerMode_Ice) {
        return false;
    }

    mActor->resetPlayerModeOnDamage();
    getPlayer()->mMovementStates._B = false;
    getPlayer()->mMovementStates._A = false;
    mActor->damageDropThrowMemoSensor();
    mFireDance->_29 = 0;
    changeStatus(mFireDance);
    mFireDamage->_12 = 120;
    return true;
}

MarioFireDance::MarioFireDance(MarioActor* pActor) : MarioState(pActor, MarioStatus_FireDance) {
    _14.zero();
    _24 = 0;
    _20 = 0.0f;
    _26 = 0;
    _28 = 0;
    _29 = 0;
}

bool MarioFireDance::start() {
    stopAnimationUpper(nullptr);
    _20 = -mActor->getConst().getTable()->mFireDanceFirstJump;
    Mario* player = getPlayer();
    MR::vecKillElement(player->mJumpVec, getAirGravityVec(), &_14);
    _24 = 0;
    _26 = 0;
    if (getPlayer()->mDrawStates._10) {
        _20 *= 0.25f;
        _14 = getPlayer()->getWallNorm() * mActor->getConst().getTable()->mFireDanceFirstJump * 0.5f;
        getPlayer()->setFrontVecKeepUp(getPlayer()->getWallNorm());
        _24 = 1;
        _26 = 60;
    }

    _28 = 0;
    impact();
    impactEffect();
    startPadVib(3);
    getPlayer()->mJumpVec = getAirGravityVec() * _20;
    return true;
}

void MarioFireDance::impact() {
    changeAnimation("\x83\x74\x83\x40\x83\x43\x83\x41\x83\x5f\x83\x93\x83\x58");
    if (!getPlayer()->mDrawStates._10) {
        if (_14.length() > 2.0f * mActor->getConst().getTable()->mFireDanceMoveSpeed) {
            _14.setLength(2.0f * mActor->getConst().getTable()->mFireDanceMoveSpeed);
        }

        _14.setLength(0.5f * _14.length());
    }

    getPlayer()->mMovementStates._1 = false;
    getPlayer()->mMovementStates.jumping = true;
}

void MarioFireDance::impactEffect() {
    playSound("\x83\x5f\x83\x81\x81\x5b\x83\x57");
    playEffect("\x83\x5f\x83\x81\x81\x5b\x83\x57");
    switch (_29) {
    case 0:
        playSound("\x90\xba\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57");
        playSound("\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57");
        if (mActor->_1B4) {
            playEffect("\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57\x90\xc2\x89\x8c");
        } else {
            playEffect("\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57\x89\x8c");
        }

        break;
    case 1:
        playSound("\x90\xba\x90\x6a\x83\x5f\x83\x81\x81\x5b\x83\x57");
        playSound("\x90\x6a\x83\x5f\x83\x81\x81\x5b\x83\x57");
        break;
    }
}

bool MarioFireDance::update() {
    if (getPlayer()->mMovementStates._1 && !getPlayer()->isRising()) {
        if (getPlayer()->_960 != 0x81 && getPlayer()->_960 != CollisionFloorCode_DamageFire && getPlayer()->_960 != CollisionFloorCode_Needle) {
            if (_28 == 1) {
                if (MR::getPlayerLeft() == 0 && !mActor->mHealth) {
                    mActor->changeGameOverAnimation();
                    return true;
                }

                getPlayer()->connectToFireRun();
                return false;
            }

            _28++;
            _20 = -mActor->getConst().getTable()->mFireDanceSecondJump;
            impact();
            startPadVib(0U);
            playSound("\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57\x95\x9c\x8b\x41\x83\x6f\x83\x45\x83\x93\x83\x68");
            if (_29 == 1) {
                playSound("\x90\xba\x90\x6a\x83\x5f\x83\x81\x81\x5b\x83\x57\x92\x86");
            } else {
                playSound("\x90\xba\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57\x92\x86");
            }

            changeAnimation("\x83\x74\x83\x40\x83\x43\x83\x41\x83\x5f\x83\x93\x83\x58");
        } else {
            if (!_29) {
                mActor->decLifeLarge();
            } else {
                mActor->decLifeMiddle();
            }

            if (!mActor->mHealth) {
                mActor->forceGameOverNonStop();
            }

            _20 = -mActor->getConst().getTable()->mFireDanceFirstJump;
            impact();
            impactEffect();
            startPadVib(3);
        }
    }

    getPlayer()->mJumpVec = getAirGravityVec() * _20;
    addVelocity(getAirGravityVec() * _20);
    if (_20 < 0.0f) {
        _20 += mActor->getConst().getTable()->mFireDanceGravityRise;
    } else {
        _20 += mActor->getConst().getTable()->mFireDanceGravityDrop;
    }

    if (_20 > 50.0f) {
        _20 = 50.0f;
    }

    if (getStickP() != 0.0f) {
        if (_26) {
            _26--;
        } else {
            const TVec3f& padDir = getWorldPadDir();
            getPlayer()->setFrontVecKeepUp(padDir, mActor->getConst().getTable()->mFireDanceTurnRatio);
            _14 += getFrontVec() * mActor->getConst().getTable()->mFireDanceMoveAcc;
        }
    }

    MR::vecKillElement(_14, getAirGravityVec(), &_14);
    if (!_24 && _14.length() > mActor->getConst().getTable()->mFireDanceMoveSpeed) {
        _14.setLength(mActor->getConst().getTable()->mFireDanceMoveSpeed);
    }

    addVelocity(_14);
    return true;
}

bool MarioFireDance::close() {
    stopAnimation("\x83\x74\x83\x40\x83\x43\x83\x41\x83\x5f\x83\x93\x83\x58");
    stopEffect("\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57\x89\x8c");
    stopEffect("\x89\x8a\x83\x5f\x83\x81\x81\x5b\x83\x57\x90\xc2\x89\x8c");
    return true;
}

void Mario::checkKarikariDamage() {
    if (!_1C._5 || isDamaging()) {
        _7D0 = 120;
        return;
    }

    if (mActor->_934) {
        return;
    }

    if (mActor->_EA4) {
        return;
    }

    if (isStatusActive(MarioStatus_Talk)) {
        return;
    }

    if (_1C._5 && _7D0) {
        _7D0--;
        if (!_7D0) {
            if (mActor->mHealth == 1) {
                faint(mHeadVec);
                _7D0 = 120;
                return;
            }

            startPadVib(2);
            playSound("\x90\xba\x8f\xac\x83\x5f\x83\x81\x81\x5b\x83\x57");
            playSound("\x83\x5f\x83\x81\x81\x5b\x83\x57");
            mActor->decLifeSmall();
            mActor->_BC4 = 16;
            _7D0 = 120;
            if (!mActor->mHealth) {
                mActor->forceGameOver();
            }
        }
    }
}

bool Mario::doDarkDamage() {
    if (getCurrentStatus() == MarioStatus_DarkDamage) {
        return false;
    }

    mActor->_3C0 = true;
    stopWalk();
    mActor->damageDropThrowMemoSensor();
    playSound("\x90\xba\x8f\xc0\x92\xbe\x82\xdd");
    playEffect("\x83\x5f\x81\x5b\x83\x4e\x83\x7d\x83\x5e\x81\x5b\x8e\x80\x96\x53");
    setSeVersion(1);
    changeStatus(mDarkDamage);
    return true;
}

MarioDarkDamage::MarioDarkDamage(MarioActor* pActor) : MarioState(pActor, MarioStatus_DarkDamage) {
    _12 = 0;
    _14 = 0;
}

bool MarioDarkDamage::start() {
    _12 = 0;
    _14 = 0;
    return true;
}

bool MarioDarkDamage::update() {
    switch (_14) {
    case 0:
        _14++;
        _12 = 150;
        MR::requestStartGameOverDemo();
        break;
    case 1:
        if (_12) {
            _12--;
        }

        if (!_12) {
            mActor->forceKill(3);
            MarioActor* actor = mActor;
            actor->_481 = 1;
            actor->updateHand();
            actor->updateFace();
        }

        break;
    }

    if (_12) {
        playSound("\x83\x5f\x81\x5b\x83\x4e\x83\x7d\x83\x5e\x81\x5b\x92\xbe\x82\xdd");
    }

    return true;
}

bool MarioState::close() {
    return true;
}

bool MarioState::update() {
    return true;
}

bool MarioState::start() {
    return true;
}

bool MarioDarkDamage::notice() {
    return true;
}

bool MarioAbyssDamage::notice() {
    return true;
}
