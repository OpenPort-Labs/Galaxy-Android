#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GalaxyMoveArgument.hpp"
#include "Game/NWC24/NWC24Messenger.hpp"
#include "Game/System/FindingLuigiEventScheduler.hpp"
#include "Game/System/GalaxyCometScheduler.hpp"
#include "Game/System/GameDataConst.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataGalaxyStorage.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/GameDataTemporaryInGalaxy.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/GameSequenceProgress.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/System/StorySequenceExecutor.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include "port/port.h"

namespace {
    static s32 sCreditedQuickSaveStarBits = 0;

    void resetQuickSaveStarBits() {
        sCreditedQuickSaveStarBits = 0;
    }

    GameSequenceProgress* getGameSequenceProgress() NO_INLINE {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mGameSequenceProgress;
    }

    SaveDataHandleSequence* getSaveDataHandleSequence() NO_INLINE {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mSaveDataHandleSequence;
    }

    GameDataTemporaryInGalaxy* getGameDataTemporaryInGalaxy() {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mGameDataTemporaryInGalaxy;
    }

    NWC24Messenger* getNWC24Messenger() NO_INLINE {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mNWC24Messenger;
    }
};  // namespace

namespace GameSequenceFunction {
    void activateGalaxyCometScheduler() {
        ::getGameSequenceProgress()->getGalaxyCometScheduler()->activate();
    }

    void deactivateGalaxyCometScheduler() {
        ::getGameSequenceProgress()->getGalaxyCometScheduler()->deactivate();
    }

    void forceToNextStateGalaxyCometScheduler() {
        ::getGameSequenceProgress()->getGalaxyCometScheduler()->forceToNextState();
    }

    bool isGalaxyCometLand(const char* pGalaxyName) {
        return ::getGameSequenceProgress()->getGalaxyCometScheduler()->isCometLand(pGalaxyName);
    }

    s32 getEncounterGalaxyCometPowerStarId(const char* pGalaxyName) {
        return ::getGameSequenceProgress()->getGalaxyCometScheduler()->getEncounterCometPowerStarId(pGalaxyName);
    }

    const char* getEncounterGalaxyCometName(const char* pGalaxyName) {
        return ::getGameSequenceProgress()->getGalaxyCometScheduler()->getEncounterCometName(pGalaxyName);
    }

    s32 getGalaxyCometState(const char* pGalaxyName) {
        return ::getGameSequenceProgress()->getGalaxyCometScheduler()->getStateValueIncluded(pGalaxyName);
    }

    bool isNeedMoviePlayerForStorySequenceEvent() {
        return ::getGameSequenceProgress()->mStorySequenceExecutor->isNeedMoviePlayerExecutingEventEnum();
    }

    bool hasNextDemoForStorySequenceEvent() {
        return ::getGameSequenceProgress()->mStorySequenceExecutor->hasNextDemo();
    }

    s32 getExecutingStorySequenceEventNum() {
        return ::getGameSequenceProgress()->mStorySequenceExecutor->getExecutingEventEnum();
    }

    bool isLuigiDisappearFromAstroGalaxy() {
        return ::getGameSequenceProgress()->mFindingLuigiEventScheduler->isDisappear();
    }

    bool isLuigiHidingAnyGalaxy() {
        return ::getGameSequenceProgress()->mFindingLuigiEventScheduler->isHiding();
    }

    bool isActiveLuigiHideAndSeekEvent() {
        return ::getGameSequenceProgress()->mFindingLuigiEventScheduler->isActive();
    }

    bool isEndLuigiHideAndSeekEvent() {
        return ::getGameSequenceProgress()->mFindingLuigiEventScheduler->isEnd();
    }

    void getLuigiHidingGalaxyNameAndStarId(const char** ppGalaxyName, s32* pStarId) {
        return ::getGameSequenceProgress()->mFindingLuigiEventScheduler->getHidingGalaxyNameAndStarId(ppGalaxyName, pStarId);
    }

    void onGameEventFlagGetLuigiLetter() {
        ::getGameSequenceProgress()->mFindingLuigiEventScheduler->setStateHiding();
    }

    bool isReadyToStartScene() {
        return SingletonHolder< GameSystem >::get()->mSceneController->isReadyToStartScene();
    }

    void startScene() {
        SingletonHolder< GameSystem >::get()->mSceneController->startScene();
    }

    void notifyToGameSequenceProgressToEndScene() {
        ::getGameSequenceProgress()->endScene();
    }

    void requestChangeScene(const char* pSceneName) {
        port_perf_scene_transition(pSceneName);
        resetQuickSaveStarBits();
        ::getGameSequenceProgress()->requestChangeScene(pSceneName);
    }

    void requestGalaxyMove(const GalaxyMoveArgument& rArgument) {
        if (rArgument.mStageName) {
            port_perf_scene_transition(rArgument.mStageName);
        }
        resetQuickSaveStarBits();
        ::getGameSequenceProgress()->requestGalaxyMove(rArgument);
    }

    void requestCancelScenarioSelect() {
        ::getGameSequenceProgress()->requestCancelScenarioSelect();
    }

    void sendStageResultSequenceParam(const char* pStageName, s32 powerStarId, int starPieceNum, int coinNum) {
        int uncreditedPieces = starPieceNum;
        if (sCreditedQuickSaveStarBits > 0) {
            uncreditedPieces = (starPieceNum > sCreditedQuickSaveStarBits) ? (starPieceNum - sCreditedQuickSaveStarBits) : 0;
        }
        ::getGameDataTemporaryInGalaxy()->receiveStageResultParam(pStageName, powerStarId, uncreditedPieces, coinNum);
    }

    bool hasStageResultSequence() {
        return ::getGameDataTemporaryInGalaxy()->isStageCleared();
    }

    const char* getClearedStageName() {
        return ::getGameDataTemporaryInGalaxy()->mStageName;
    }

    s32 getClearedPowerStarId() {
        return ::getGameDataTemporaryInGalaxy()->mPowerStarId;
    }

    int getClearedStarPieceNum() {
        return ::getGameDataTemporaryInGalaxy()->mStarPieceNum;
    }

    int getClearedCoinNum() {
        return ::getGameDataTemporaryInGalaxy()->mCoinNum;
    }

    bool hasPowerStarYetAtResultSequence() {
        if (hasStageResultSequence() == false) {
            return false;
        }

        GameDataHolder* pGameDataHolder = GameDataFunction::getSceneStartGameDataHolder();

        return pGameDataHolder->hasPowerStar(getClearedStageName(), getClearedPowerStarId());
    }

    bool isPowerStarAtResultSequence(const char* pClearedStageName, s32 clearedPowerStarId) {
        if (hasStageResultSequence() == false) {
            return false;
        }

        return MR::isEqualString(getClearedStageName(), pClearedStageName) && getClearedPowerStarId() == clearedPowerStarId;
    }

    bool isPowerStarGreenAtResultSequence() {
        return GameDataConst::isPowerStarGreen(getClearedStageName(), getClearedPowerStarId());
    }

    bool isPowerStarRedAtResultSequence() {
        return GameDataConst::isPowerStarRed(getClearedStageName(), getClearedPowerStarId());
    }

    bool isGrandStarAtResultSequence() {
        return GameDataConst::isGrandStar(getClearedStageName(), getClearedPowerStarId());
    }

    bool hasRetryGalaxySequence() {
        return ::getGameDataTemporaryInGalaxy()->_4;
    }

    bool isNeedToReflectStageResultSequenceStarPiece() {
        return getClearedStarPieceNum() != 0;
    }

    void reflectStageResultSequenceCoin() {
        u32 clearedCoinNum = getClearedCoinNum();
        GameDataHolder* pGameDataHolder = SingletonHolder< GameSystem >::get()->mSequenceDirector->getGameDataHolder();

        GameDataSomeScenarioAccessor accessor = pGameDataHolder->makeGalaxyScenarioAccessor(getClearedStageName(), getClearedPowerStarId());
        accessor.updateMaxCoinNum(clearedCoinNum);
    }

    void resetStageResultSequenceParam() {
        ::getGameDataTemporaryInGalaxy()->resetStageResultParam();
    }

    void startPreLoadSaveDataSequence() {
        ::getSaveDataHandleSequence()->startPreLoad();
    }

    void startTotalMailSizeSaveSequence() {
        ::getSaveDataHandleSequence()->startSaveTotalMailSize();
    }

    void startGameDataLoadSequence(int userFileIndex, bool isPlayerMario) {
        ::getSaveDataHandleSequence()->startLoad(userFileIndex, isPlayerMario);
    }

    void startCreateUserFileSequence(int userFileIndex) {
        ::getSaveDataHandleSequence()->startCreateUserFile(userFileIndex);
    }

    void startDeleteUserFileSequence(int userFileIndex) {
        ::getSaveDataHandleSequence()->startDeleteUserFile(userFileIndex);
    }

    void startCopyUserFileSequence(int userFileIndexDst, int userFileIndexSrc) {
        storeCopyUserFileSequence(userFileIndexDst, userFileIndexSrc);
        startSaveAllUserFileSequence();
    }

    void startSetMiiOrIconIdUserFileSequence(int userFileIndex, const void* pMiiId, const u32* pIconId) {
        storeMiiOrIconIdUserFileSequence(userFileIndex, pMiiId, pIconId);
        startSaveAllUserFileSequence();
    }

    void storeMiiOrIconIdUserFileSequence(int userFileIndex, const void* pMiiId, const u32* pIconId) {
        ::getSaveDataHandleSequence()->storeMiiOrIconId(userFileIndex, pMiiId, pIconId);
    }

    void storeCopyUserFileSequence(int userFileIndexDst, int userFileIndexSrc) {
        ::getSaveDataHandleSequence()->storeCopyUserFile(userFileIndexDst, userFileIndexSrc);
    }

    void startSaveAllUserFileSequence() {
        ::getSaveDataHandleSequence()->startSaveAll();
    }

    bool isActiveSaveDataHandleSequence() {
        return ::getSaveDataHandleSequence()->isActive();
    }

    bool isSuccessSaveDataHandleSequence() {
        return ::getSaveDataHandleSequence()->_24 == 2;
    }

    void restoreUserFile(UserFile* pUserFile, int userFileIndex) {
        ::getSaveDataHandleSequence()->restoreUserFile(pUserFile, userFileIndex);
    }

    void restoreUserFile(UserFile* pUserFile, int userFileIndex, bool isPlayerMario) {
        ::getSaveDataHandleSequence()->restoreUserFile(pUserFile, userFileIndex, isPlayerMario);
    }

    void tryNANDErrorSequence(s32 code) {
        ::getSaveDataHandleSequence()->tryNANDErrorSequence(code);
    }

    void reserveUserName(const wchar_t* pUserName) {
        ::getSaveDataHandleSequence()->getCurrentUserFile()->setUserName(pUserName);
    }

    void requestPrepareResetNWC24() {
        ::getNWC24Messenger()->prepareReset();
    }

    bool isEnableToResetNWC24() {
        return !::getNWC24Messenger()->isRunning();
    }

    void resetNWC24() {
        ::getNWC24Messenger()->reset();
    }

    void storeSceneStartGameDataHolder() {
        if (!GameDataFunction::isPassedStoryEvent("\x83\x60\x83\x52\x83\x4b\x83\x43\x83\x68\x83\x66\x83\x82\x8f\x49\x97\xb9") || GameDataFunction::hasGrandStar(1)) {
            ::getSaveDataHandleSequence()->backupCurrentUserFile();
        }
    }

    void updateGameDataAndSequenceAfterStageResultSequence() {
        if (!isPowerStarAtResultSequence("KoopaBattleVs3Galaxy", 1)) {
            GameDataFunction::setGameFlagPowerStarSuccess(getClearedStageName(), getClearedPowerStarId(), true);
        }

        resetQuickSaveStarBits();
        ::getGameSequenceProgress()->getGalaxyCometScheduler()->syncWithFlags();
        ::getGameSequenceProgress()->getGalaxyCometScheduler()->forceToNextState();
    }

    void startGameDataSaveSequence(bool isConfirmRemind, bool isSaveAndQuitMsg) {
        ::getGameSequenceProgress()->getGalaxyCometScheduler()->updateStateToGameData();

        if (GameDataFunction::isPassedStoryEvent("\x83\x73\x81\x5b\x83\x60\x8f\xe9\x95\x82\x8f\xe3\x8c\xe3") && !GameDataFunction::hasGrandStar(1)) {
            ::getSaveDataHandleSequence()->startSaveBackup(isConfirmRemind, isSaveAndQuitMsg);
        } else {
            ::getSaveDataHandleSequence()->startSave(isConfirmRemind, isSaveAndQuitMsg);
        }
    }

    void quickSaveAnywhere() {
        SaveDataHandleSequence* seq = ::getSaveDataHandleSequence();
        if (seq == nullptr || seq->isActive()) {
            return;
        }

        GameSequenceProgress* progress = ::getGameSequenceProgress();
        if (progress != nullptr && progress->getGalaxyCometScheduler() != nullptr) {
            progress->getGalaxyCometScheduler()->updateStateToGameData();
        }

        if (!MR::isStageAstroLocation()) {
            s32 stageBits = MR::getStarPieceNum();
            if (stageBits > sCreditedQuickSaveStarBits) {
                s32 delta = stageBits - sCreditedQuickSaveStarBits;
                GameDataFunction::addStockedStarPiece(delta);
                sCreditedQuickSaveStarBits = stageBits;
            }
        }

        seq->startQuickSaveAnywhere();
    }

    void resetQuickSaveStarBits() {
        ::resetQuickSaveStarBits();
    }
};  // namespace GameSequenceFunction
