#include "Game/System/GameSystem.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NameObj/NameObjRegister.hpp"
#include "Game/Screen/HomeButtonLayout.hpp"
#include "Game/Screen/SystemWipeHolder.hpp"
#include "Game/System/AudSystemWrapper.hpp"
#include "Game/System/DrawSyncManager.hpp"
#include "Game/System/FileRipper.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystemDimmingWatcher.hpp"
#include "Game/System/GameSystemErrorWatcher.hpp"
#include "Game/System/GameSystemException.hpp"
#include "Game/System/GameSystemFontHolder.hpp"
#include "Game/System/GameSystemFrameControl.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/System/GameSystemResetAndPowerProcess.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/GameSystemStationedArchiveLoader.hpp"
#include "Game/System/HeapMemoryWatcher.hpp"
#include "Game/System/HomeButtonStateNotifier.hpp"
#include "Game/System/MainLoopFramework.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MutexHolder.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <JSystem/JKernel/JKRAram.hpp>
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include <nw4r/lyt/init.h>
#include <revolution.h>

#define GX_FIFO_SIZE 0x80000

#define INIT_AUDIO_KEY "\x83\x49\x81\x5b\x83\x66\x83\x42\x83\x49\x8f\x89\x8a\xfa\x89\xbb"  // "Audio Initialization"

namespace NrvGameSystem {
    NEW_NERVE(GameSystemInitializeAudio, GameSystem, InitializeAudio);
    NEW_NERVE(GameSystemInitializeLogoScene, GameSystem, InitializeLogoScene);
    NEW_NERVE(GameSystemLoadStationedArchive, GameSystem, LoadStationedArchive);
    NEW_NERVE(GameSystemWaitForReboot, GameSystem, WaitForReboot);
    NEW_NERVE(GameSystemNormal, GameSystem, Normal);
};  // namespace NrvGameSystem

#ifdef TARGET_PC
void port_game_main(void) {
#else
#ifdef TARGET_PC
void port_game_main(void) {
#else
void main(void) {
#endif
#endif
    OSInitFastCast();
    DVDInit();
    VIInit();
    HeapMemoryWatcher::createRootHeap();
    OSInitMutex(&MR::MutexHolder< 0 >::sMutex);
    OSInitMutex(&MR::MutexHolder< 1 >::sMutex);
    OSInitMutex(&MR::MutexHolder< 2 >::sMutex);
    nw4r::lyt::LytInit();
    MR::setLayoutDefaultAllocator();
    SingletonHolder< HeapMemoryWatcher >::init();
    SingletonHolder< HeapMemoryWatcher >::get()->setCurrentHeapToStationedHeap();
    FileRipper::setup(0x20000, MR::getStationedHeapNapa());
    GameSystemException::init();
    MR::initAcosTable();
    SingletonHolder< GameSystem >::init();
    SingletonHolder< GameSystem >::get()->init();

    GameSystem* pGameSystem = SingletonHolder< GameSystem >::get();

    while (true) {
        pGameSystem->frameLoop();
    }
}

GameSystem::GameSystem()
    : NerveExecutor("GameSystem"), mFifoBase(nullptr), mSequenceDirector(nullptr), mErrorWatcher(nullptr), mFontHolder(nullptr),
      mFrameControl(nullptr), mObjHolder(nullptr), mSceneController(nullptr), mStationedArchiveLoader(nullptr), mHomeButtonLayout(nullptr),
      mSystemWipeHolder(nullptr), mHomeButtonStateNotifier(nullptr), mIsExecuteLoadSystemArchive(false) {
}

void GameSystem::init() {
    JKRAram::create(0xE00000, 0xFFFFFFFF, 8, 7, 3);
    mObjHolder = new GameSystemObjHolder();
    mFontHolder = new GameSystemFontHolder();
    mFontHolder->createFontFromEmbeddedData();
    initNerve(GET_NERVE(GameSystem, GameSystemInitializeAudio));
    mSequenceDirector = new GameSequenceDirector();
    initGX();
    DrawSyncManager::start(0x300, 15);
    mSceneController = new GameSystemSceneController();
    mObjHolder->init();
    mErrorWatcher = new GameSystemErrorWatcher();
    mFrameControl = new GameSystemFrameControl();
    SingletonHolder< GameSystemResetAndPowerProcess >::init();
    SingletonHolder< GameSystemResetAndPowerProcess >::get()->initWithoutIter();
    mStationedArchiveLoader = new GameSystemStationedArchiveLoader();
    mHomeButtonLayout = new HomeButtonLayout();
    mHomeButtonStateNotifier = new HomeButtonStateNotifier();
    mDimmingWatcher = new GameSystemDimmingWatcher();
    setNerve(GET_NERVE(GameSystem, GameSystemInitializeAudio));
}

bool GameSystem::isExecuteLoadSystemArchive() const {
    return mIsExecuteLoadSystemArchive;
}

bool GameSystem::isDoneLoadSystemArchive() const {
    return isNerve(GET_NERVE(GameSystem, GameSystemNormal));
}

void GameSystem::startToLoadSystemArchive() {
    mIsExecuteLoadSystemArchive = true;

    SingletonHolder< HeapMemoryWatcher >::get()->setCurrentHeapToStationedHeap();
    SingletonHolder< NameObjRegister >::get()->setCurrentHolder(mObjHolder->mObjHolder);
    setNerve(GET_NERVE(GameSystem, GameSystemLoadStationedArchive));
}

void GameSystem::exeInitializeAudio() {
    if (MR::isFirstStep(this)) {
        MR::startFunctionAsyncExecute(MR::Functor(mObjHolder, &GameSystemObjHolder::createAudioSystem), 14, INIT_AUDIO_KEY);
    }

    updateSceneController();

    if (MR::isEndFunctionAsyncExecute(INIT_AUDIO_KEY) && mObjHolder->mAudioSystem->isLoadDoneWaveDataAtSystemInit()) {
        MR::waitForEndFunctionAsyncExecute(INIT_AUDIO_KEY);
        setNerve(GET_NERVE(GameSystem, GameSystemInitializeLogoScene));
    }
}

void GameSystem::exeInitializeLogoScene() {
    if (GameSystemFunction::isResetProcessing()) {
        setNerve(GET_NERVE(GameSystem, GameSystemWaitForReboot));
    } else {
        if (MR::isFirstStep(this)) {
            MR::requestChangeScene("Logo");
        }

        updateSceneController();
    }
}

void GameSystem::exeLoadStationedArchive() {
    mStationedArchiveLoader->update();
    updateSceneController();

    if (mStationedArchiveLoader->isDone()) {
        setNerve(GET_NERVE(GameSystem, GameSystemNormal));
    }
}

void GameSystem::exeWaitForReboot() {
}

void GameSystem::exeNormal() {
    updateSceneController();
    mStationedArchiveLoader->update();
}

void GameSystem::initGX() {
    if (mFifoBase == nullptr) {
        mFifoBase = new (32) u8[GX_FIFO_SIZE];
    }

    GXInit(mFifoBase, GX_FIFO_SIZE);
}

void GameSystem::initAfterStationedResourceLoaded() {
    mFontHolder->createFontFromFile();
    mObjHolder->initAfterStationedResourceLoaded();
    mHomeButtonLayout->initWithoutIter();
    mErrorWatcher->initAfterResourceLoaded();
    mSystemWipeHolder = MR::createSystemWipeHolder();
    mSceneController->initAfterStationedResourceLoaded();
    mSequenceDirector->initAfterResourceLoaded();
}

void GameSystem::prepareReset() {
    mStationedArchiveLoader->prepareReset();
}

inline bool isSystemWaitForReboot(const GameSystem* pGameSystem) {
    return pGameSystem->isNerve(GET_NERVE(GameSystem, GameSystemWaitForReboot));
}

inline bool isSystemNormal(const GameSystem* pGameSystem) {
    return pGameSystem->isNerve(GET_NERVE(GameSystem, GameSystemNormal));
}

bool GameSystem::isPreparedReset() const {
    return isSystemWaitForReboot(this) || isSystemNormal(this) || mStationedArchiveLoader->isPreparedReset();
}

void GameSystem::frameLoop() {
#ifdef TARGET_PC
    port_perf_frame_begin();
#ifndef TARGET_MOBILE
    if (port_skip_fast_forwarding()) {
        // A cutscene being skipped: nothing is drawn (the headset view is
        // dimmed), and the game logic runs as often as fits in a frame.  The
        // retrace wait still gives the other threads (audio, loading) their
        // turn.
        int64_t start = port_host_time_ns();
        do {
            update();
            calcAnim();
            if (mSceneController->isChangingScene()) {
                // The skipped cutscene ends in another stage (Mario jumping
                // out of a dome into the star select): the scene change runs
                // at the normal pace, drawn, with its loading threads timed
                // as when nothing is skipped.  Fast-forwarded through, the
                // game stopped for good there.
                port_skip_scene_change();
                break;
            }
        } while (port_skip_fast_forwarding() && port_host_time_ns() - start < 12000000);
        port_perf_frame_work_done();
        MainLoopFramework::sManager->waitForRetrace();
        return;
    }
#else
    if (port_skip_fast_forwarding()) {
        int64_t start = port_host_time_ns();
        do {
            update();
            calcAnim();
            if (mSceneController->isChangingScene()) {
                port_skip_scene_change();
                break;
            }
        } while (port_skip_fast_forwarding() && port_host_time_ns() - start < 12000000);
    }
#endif
#endif
    MainLoopFramework::sManager->beginRender();
    draw();
    MainLoopFramework::sManager->endRender();
    update();
    calcAnim();
    mObjHolder->captureIfAllowForScreenPreserver();
    MainLoopFramework::sManager->endFrame();
#ifdef TARGET_PC
    port_perf_frame_work_done();
#endif
    MainLoopFramework::sManager->waitForRetrace();
}

void GameSystem::draw() {
    mSceneController->drawScene();
#ifdef TARGET_PC
    // Everything drawn over the scene is 2D too: the save and error windows,
    // the pointer's cursor, the system wipe.  In VR it goes on the HUD panel
    // with the scene's own 2D, where the pointer aims (drawn into the eyes'
    // images instead, it covered the whole view, out of the pointer's reach).
    port_gx_marker(PORT_GX_MARK_HUD_BEGIN);
#endif
    mSequenceDirector->draw();
#ifdef TARGET_PC
    port_gx_marker(PORT_GX_MARK_POINTER_BEGIN);
#endif
    mObjHolder->drawStarPointer();
#ifdef TARGET_PC
    port_gx_marker(PORT_GX_MARK_POINTER_END);
#endif
    mObjHolder->drawBeforeEndRender();

    if (mSystemWipeHolder != nullptr) {
        mSystemWipeHolder->draw();
    }

    mErrorWatcher->draw();
    mHomeButtonLayout->draw();
    SingletonHolder< GameSystemResetAndPowerProcess >::get()->draw();
#ifdef TARGET_PC
    port_gx_marker(PORT_GX_MARK_HUD_END);
#endif
}

void GameSystem::update() {
    SingletonHolder< GameSystemResetAndPowerProcess >::get()->movement();
    mSceneController->checkRequestAndChangeScene();
    mObjHolder->update();
    mHomeButtonLayout->movement();

    if (!mHomeButtonLayout->isActive()) {
        mErrorWatcher->movement();
    }

    mDimmingWatcher->_5 = mErrorWatcher->isWarning() || mHomeButtonLayout->isActive() || GameSequenceFunction::isActiveSaveDataHandleSequence();
    mDimmingWatcher->update();
    updateNerve();
}

void GameSystem::updateSceneController() {
    bool isSceneUpdate = true;
    bool isResetProcessing = SingletonHolder< GameSystemResetAndPowerProcess >::get()->isActive();

    mObjHolder->updateAudioSystem();

    if (isResetProcessing) {
        isSceneUpdate = false;
    }

    if (mHomeButtonLayout->isActive()) {
        isSceneUpdate = false;
    }

    if (GameSystemFunction::isOccurredSystemWarning()) {
        isSceneUpdate = false;
    }

    mHomeButtonStateNotifier->update(mHomeButtonLayout->isActive() || GameSystemFunction::isOccurredSystemWarning());

    if (isSceneUpdate || isResetProcessing) {
        mSequenceDirector->update();
    }

    if (isSceneUpdate || mSceneController->isFirstUpdateSceneNerveNormal()) {
        if (mSystemWipeHolder != nullptr) {
            mSystemWipeHolder->movement();
        }

        mSceneController->updateScene();
    }

    if (isResetProcessing) {
        mSceneController->updateSceneDuringResetProcessing();
    }
}

void GameSystem::calcAnim() {
    mSceneController->calcAnimScene();

    if (mSystemWipeHolder != nullptr) {
        mSystemWipeHolder->calcAnim();
    }
}
