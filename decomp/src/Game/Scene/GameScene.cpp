#include "Game/Scene/GameScene.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/AudioLib/AudSceneMgr.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Map/SleepControllerHolder.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/MapObj/StarPieceDirector.hpp"
#include "Game/NPC/EventDirector.hpp"
#include "Game/NPC/NPCFunction.hpp"
#include "Game/NPC/TalkDirector.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Scene/GameScenePauseControl.hpp"
#include "Game/Scene/GameSceneScenarioOpeningCameraState.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneNameObjMovementController.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/CometRetryButton.hpp"
#include "Game/Screen/GamePauseSequence.hpp"
#include "Game/Screen/GameStageClearSequence.hpp"
#include "Game/Screen/LensFlare.hpp"
#include "Game/Screen/MoviePlayingSequence.hpp"
#include "Game/Screen/OdhConverter.hpp"
#include "Game/Screen/ScreenAlphaCapture.hpp"
#include "Game/System/GalaxyMapController.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/Camera/CameraDirector.hpp"
#ifdef TARGET_PC
#include "Game/Camera/CameraHolder.hpp"
#include "Game/Camera/CameraManEvent.hpp"
#include "Game/Camera/CameraManGame.hpp"
#include "Game/Camera/CameraManPause.hpp"
#include "Game/Camera/CameraManSubjective.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraParamChunkID.hpp"
#endif
#include "Game/System/StarPointerOnOffController.hpp"
#include "Game/Camera/CameraPoseParam.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/JMapIdInfo.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <JSystem/J3DGraphBase/J3DSys.hpp>

namespace {
    CometRetryButton* getCometRetryButton() {
        return MR::getSceneObj< CometRetryButton >(SceneObj_CometRetryButton);
    }
};  // namespace

namespace NrvGameScene {
    NEW_NERVE(GameSceneScenarioOpeningCamera, GameScene, ScenarioOpeningCamera);
    NEW_NERVE(GameSceneScenarioStarter, GameScene, ScenarioStarter);
    NEW_NERVE(GameSceneAction, GameScene, SceneAction);
    NEW_NERVE(GameScenePauseMenu, GameScene, PauseMenu);
    NEW_NERVE(GameScenePowerStarGet, GameScene, PowerStarGet);
    NEW_NERVE(GameSceneGrandStarGet, GameScene, GrandStarGet);
    NEW_NERVE(GameSceneGameOver, GameScene, GameOver);
    NEW_NERVE(GameSceneCometRetryAfterMiss, GameScene, CometRetryAfterMiss);
    NEW_NERVE(GameSceneSaveAfterGameOver, GameScene, SaveAfterGameOver);
    NEW_NERVE(GameScenePlayMovie, GameScene, PlayMovie);
    NEW_NERVE(GameSceneTimeUp, GameScene, TimeUp);
    NEW_NERVE(GameSceneGalaxyMap, GameScene, GalaxyMap);
    NEW_NERVE(GameSceneStaffRoll, GameScene, StaffRoll);
};  // namespace NrvGameScene

GameScene::GameScene()
    : Scene("GameScene"), _14(0), mScenarioCamera(nullptr), mPauseCtrl(nullptr), mPauseSeq(nullptr), mStageClearSeq(nullptr), mDraw3D(true), _29(1) {
}

GameScene::~GameScene() {
    MR::destroySceneMessage();
    NPCFunction::deleteNPCData();
    MR::onStarPointerSceneOut();
}

void GameScene::init() {
    SceneFunction::createHioBasicNode(this);
    SceneFunction::startStageFileLoad();
    MR::requestChangeArchivePlayer(MR::isPlayerLuigi() == false);
    initNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera));
    SceneFunction::initForNameObj();
    SceneFunction::initForLiveActor();
    initEffect();
    MR::createSceneObj(SceneObj_CameraContext);
    MR::createSceneObj(SceneObj_NameObjGroup);
    MR::createSceneObj(SceneObj_PlanetGravityManager);
    MR::createSceneObj(SceneObj_MarioHolder);
    MR::createSceneObj(SceneObj_AudCameraWatcher);
    MR::createSceneObj(SceneObj_AudEffectDirector);
    MR::createSceneObj(SceneObj_AudBgmConductor);
    MR::createSceneObj(SceneObj_ResourceShare);
    MR::createSceneObj(SceneObj_EventSequencer);
    MR::createSceneObj(SceneObj_ScenePlayingResult);
    MR::createSceneObj(SceneObj_FurDrawManager);
    MR::createSceneObj(SceneObj_PlacementStateChecker);
    MR::createScreenAlphaSceneObj(0, 1.0f);
    MR::createSceneObj(SceneObj_GroupCheckManager);
    MR::createSceneObj(SceneObj_CinemaFrame);
    MR::createSceneObj(SceneObj_PlanetMapCreator);

    if (MR::isStageEpilogueDemo()) {
        MR::createSceneObj(SceneObj_StaffRoll);
    }

    if (MR::isStageEpilogueDemo() || GameSequenceFunction::isNeedMoviePlayerForStorySequenceEvent()) {
        MR::createMoviePlayingSequence();
    }

    SceneFunction::waitDoneStageFileLoad();
    MR::waitEndChangeArchivePlayer();
    SceneFunction::startActorFileLoadCommon();
    MR::suspendAsyncExecuteThread("\x83\x56\x81\x5b\x83\x93\x8f\x89\x8a\xfa\x89\xbb");

    if (!MR::isScenarioDecided()) {
        MR::receiveAllRequestedFile();
    } else {
        SceneFunction::initAfterScenarioSelected();
        NPCFunction::createNPCData();
        SceneFunction::startActorFileLoadScenario();
        MR::createSceneObj(SceneObj_EventDirector);
        MR::initSceneMessage();
        MR::createSceneObj(SceneObj_CameraDirector);
        MR::createSceneObj(SceneObj_GameSceneLayoutHolder);
        MR::createSceneObj(SceneObj_StarPieceDirector);
        MR::createSceneObj(SceneObj_SceneWipeHolder);
        MR::createSceneObj(SceneObj_NamePosHolder);
        MR::createSceneObj(SceneObj_InformationObserver);

        if (MR::isGalaxyAnyCometAppearInCurrentStage()) {
            MR::createSceneObj(SceneObj_CometRetryButton);
        }

        LightFunction::initLightData();
        initSequences();
        SceneFunction::startActorPlacement();
        GameSceneFunction::loadScenarioWaveData();
        MR::createStarPiece();
        MR::completeCameraParameters();
        MR::initStarPointerGameScene();
        MR::initEventSystemAfterPlacement();
        MR::endInitLiveActorSystemInfo();
        MR::setInitializeStateAfterPlacement();
        MR::callMethodAllSceneNameObj(&NameObj::initAfterPlacement);
        SleepControlFunc::initSyncSleepController();

        while (!GameSceneFunction::isLoadDoneScenarioWaveData()) {
#ifdef TARGET_PC
            // Let interrupts in (see MR::waitEndChangeArchivePlayer).
            port_host_sleep_ns(200000);
            port_irq_poll();
#endif
        }
    }
}

void GameScene::start() {
    AudWrap::getSceneMgr()->startScene();

    if (MR::isGlobalTimerEnd()) {
        MR::forceCloseWipeCircle();
    } else if (!MR::hasRetryGalaxySequence()) {
        startStagePlayFirst();
    } else {
        startStagePlayRetry();
    }
}

void GameScene::update() {
#ifdef TARGET_PC
    {
        // What holding A would skip now (see port_skip_context).
        s32 skipKind = PORT_SKIP_NONE;
        if (isPlayMovie()) {
            skipKind = PORT_SKIP_MOVIE;
        } else if (isNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera))) {
            skipKind = PORT_SKIP_OPENING;
        } else if (isNerve(GET_NERVE(GameScene, GameSceneScenarioStarter))) {
            skipKind = PORT_SKIP_STARTER;
        } else if (MR::isDemoActive()) {
            skipKind = PORT_SKIP_DEMO;
        } else if (MR::isSystemTalking()) {
            skipKind = PORT_SKIP_TALK;  // most people's talk is no cutscene
        }
        port_skip_context(skipKind);
    }
#endif
    mPauseCtrl->updateNerve();
    updateNerve();
#ifdef TARGET_PC
    if (port_is_plus_edition() && !MR::isStageFileSelect() && !MR::isDemoActive()) {
        char warpStage[64];
        int warpScenario = 1;
        if (port_take_warp_request(warpStage, sizeof(warpStage), &warpScenario)) {
            OSReport("port: warping to %s scenario %d\n", warpStage, (int)warpScenario);
            MR::closeSystemWipeFadeWithCaptureScreen(30);
            MR::requestChangeStageInGameMoving(warpStage, warpScenario);
        }
    }
    {
        // Debug: PETARI_STAGE="Galaxy[:scenario]" enters that galaxy once
        // ordinary gameplay has run for two seconds (for testing levels).
        static int sGameplayFrames = 0;
        static bool sJumped = false;
        const char* stage = getenv("PETARI_STAGE");
        if (!sJumped && stage && *stage && isNerve(GET_NERVE(GameScene, GameSceneAction)) && !MR::isStageFileSelect() && !MR::isDemoActive()) {
            if (++sGameplayFrames >= 120) {
                static char name[64];
                snprintf(name, sizeof(name), "%s", stage);
                s32 scenario = 1;
                if (char* colon = strchr(name, ':')) {
                    *colon = 0;
                    scenario = atoi(colon + 1);
                }
                sJumped = true;
                OSReport("port: jumping to %s scenario %d\n", name, (int)scenario);
                MR::requestChangeStageInGameMoving(name, scenario);
            }
        }
    }
    {
        // Debug: PETARI_WARP="<t>:<x>,<y>,<z>;..." puts the player at each
        // of those world positions once <t> seconds have passed since the
        // headless launcher booted the game (it sets PETARI_T0_MS).
        static int sWarpsDone = 0;
        static const char* warps = getenv("PETARI_WARP");
        static const char* t0 = getenv("PETARI_T0_MS");
        if (warps && *warps && t0 && isNerve(GET_NERVE(GameScene, GameSceneAction))) {
            f64 nowMs = port_host_time_ns() / 1000000.0 - atof(t0);
            s32 index = 0;
            for (const char* p = warps; p != nullptr; index++) {
                f32 t, x, y, z;
                if (sscanf(p, "%f:%f,%f,%f", &t, &x, &y, &z) != 4) {
                    break;
                }
                if (index >= sWarpsDone) {
                    if (nowMs < t * 1000.0) {
                        break;
                    }
                    OSReport("port: warping the player to %.0f %.0f %.0f\n", x, y, z);
                    MR::setPlayerPos(TVec3f(x, y, z));
                    sWarpsDone = index + 1;
                }
                p = strchr(p, ';');
                if (p != nullptr) {
                    p++;
                }
            }
        }
    }
    {
        // Debug: PETARI_SWITCH="<t>:<id>;..." turns on global stage switch
        // <id> (1000 and up, as placements number them) <t> seconds after
        // boot (PETARI_T0_MS), e.g. to open a cage without its puzzle;
        // "<t>:<zone name>/<id>" turns on switch <id> (0-127) of that zone.
        static s32 sSwitchesDone = 0;
        static const char* switches = getenv("PETARI_SWITCH");
        static const char* t0 = getenv("PETARI_T0_MS");
        if (switches && *switches && t0 && isNerve(GET_NERVE(GameScene, GameSceneAction)) &&
            MR::isExistSceneObj(SceneObj_StageSwitchContainer)) {
            f64 nowMs = port_host_time_ns() / 1000000.0 - atof(t0);
            s32 index = 0;
            for (const char* p = switches; p != nullptr; index++) {
                f32 t;
                s32 id;
                char zone[64];
                bool inZone = sscanf(p, "%f:%63[^/;]/%d", &t, zone, &id) == 3;
                if (!inZone && sscanf(p, "%f:%d", &t, &id) != 2) {
                    break;
                }
                if (index >= sSwitchesDone) {
                    if (nowMs < t * 1000.0) {
                        break;
                    }
                    StageSwitchContainer* container = MR::getSceneObj< StageSwitchContainer >(SceneObj_StageSwitchContainer);
                    if (inZone) {
                        for (s32 z = 0; z < MR::getZoneNum(); z++) {
                            ZoneSwitch* sw = MR::isEqualString(MR::getZoneNameFromZoneId(z), zone) ? container->getZoneSwitches(z) : nullptr;
                            if (sw != nullptr && id >= 0 && id < 128) {
                                OSReport("port: turning on switch %d of %s\n", (int)id, zone);
                                sw->set(id, true);
                            }
                        }
                    } else if (id >= 1000 && id < 1128) {
                        OSReport("port: turning on switch %d\n", (int)id);
                        container->getGlobalSwitches()->set(id - 1000, true);
                    }
                    sSwitchesDone = index + 1;
                }
                p = strchr(p, ';');
                if (p != nullptr) {
                    p++;
                }
            }
        }
    }
    {
        // Debug: PETARI_POSLOG=1 logs the player's position once a second.
        static const bool posLog = getenv("PETARI_POSLOG") != nullptr;
        static s32 sPosFrames = 0;
        if (posLog && MR::isExistMario() && isNerve(GET_NERVE(GameScene, GameSceneAction)) && ++sPosFrames % 60 == 0) {
            const TVec3f* p = MR::getPlayerPos();
            OSReport("port: player at %.1f %.1f %.1f%s\n", p->x, p->y, p->z, MR::isOnGroundPlayer() ? " (on the ground)" : "");
        }
    }
    {
        // Debug: PETARI_STARBITS="<t>:<n>" gives the player n star bits <t>
        // seconds after boot (PETARI_T0_MS), to test shooting them.
        static bool sGiven = false;
        static const char* starBits = getenv("PETARI_STARBITS");
        static const char* t0 = getenv("PETARI_T0_MS");
        f32 t;
        s32 n;
        if (!sGiven && starBits && t0 && sscanf(starBits, "%f:%d", &t, &n) == 2 && isNerve(GET_NERVE(GameScene, GameSceneAction)) &&
            port_host_time_ns() / 1000000.0 - atof(t0) >= t * 1000.0) {
            sGiven = true;
            OSReport("port: giving %d star bits\n", (int)n);
            MR::addStarPiece(n);
        }
    }
    {
        // Debug: PETARI_SETEST="<t0>-<t1>:<name>:<volume>:<fx send>;..." plays
        // that level sound on the player each frame from t0 to t1 seconds
        // after boot (PETARI_T0_MS), like an actor near the player would.
        static const char* tests = getenv("PETARI_SETEST");
        static const char* t0 = getenv("PETARI_T0_MS");
        if (tests && *tests && t0 && isNerve(GET_NERVE(GameScene, GameSceneAction))) {
            f64 nowMs = port_host_time_ns() / 1000000.0 - atof(t0);
            for (const char* p = tests; p != nullptr;) {
                f32 from, to;
                s32 volume, fxSend;
                char name[64];
                if (sscanf(p, "%f-%f:%63[^:]:%d:%d", &from, &to, name, &volume, &fxSend) != 5) {
                    break;
                }
                if (nowMs >= from * 1000.0 && nowMs < to * 1000.0) {
                    MR::startLevelSound(MarioAccess::getPlayerActor(), name, volume, fxSend);
                }
                p = strchr(p, ';');
                if (p != nullptr) {
                    p++;
                }
            }
        }
    }
    {
        // Debug: PETARI_MOVIE=<type> (0 = PrologueA .. 6 = EndingB) plays that
        // movie after two seconds of gameplay in a stage with a movie player,
        // such as the castle garden or the observatory.
        static int sMovieFrames = 0;
        static bool sMovieStarted = false;
        const char* movie = getenv("PETARI_MOVIE");
        if (!sMovieStarted && movie && *movie && isNerve(GET_NERVE(GameScene, GameSceneAction)) && !MR::isDemoActive() &&
            MR::isExistSceneObj(SceneObj_MoviePlayingSequenceHolder)) {
            if (++sMovieFrames >= 120) {
                sMovieStarted = true;
                OSReport("port: starting movie %d\n", atoi(movie));
                MR::startMovie(atoi(movie));
            }
        }
    }
#endif

    bool isTimeUp = MR::isGlobalTimerEnd() && !isNerve(GET_NERVE(GameScene, GameSceneTimeUp)) && MR::isGreaterEqualStep(this, 2);

    if (isTimeUp) {
        setNerve(GET_NERVE(GameScene, GameSceneTimeUp));
    }
}

void GameScene::draw() const {
    MR::drawInit();
    LightFunction::initLightRegisterAll();
    drawOdhCapture();
    CategoryList::execute(MR::DrawType_MiiFaceIcon);
    CategoryList::execute(MR::DrawType_MiiFaceNew);
    drawMirror();
    draw3D();
#ifdef TARGET_PC
    port_gx_marker(PORT_GX_MARK_HUD_BEGIN);
#endif
    draw2D();
#ifdef TARGET_PC
    port_gx_marker(PORT_GX_MARK_HUD_END);
#endif
    MR::reinitGX();
}

void GameScene::calcAnim() {
    if (isPlayMovie()) {
        SceneFunction::executeCalcAnimListOnPlayingMovie();
    } else {
        SceneFunction::executeCalcAnimList();
    }

    if (!isNerve(GET_NERVE(GameScene, GameSceneTimeUp))) {
        CategoryList::execute(MR::CalcAnimType_AnimParticleIgnorePause);
    }

    SceneFunction::executeCalcViewAndEntryList();
}

void GameScene::notifyEndScenarioStarter() {
    setNerve(GET_NERVE(GameScene, GameSceneAction));
}

void GameScene::requestPlayMovieDemo() {
    setNerve(GET_NERVE(GameScene, GameScenePlayMovie));
}

void GameScene::requestStartGameOverDemo() {
    if (!isNerve(GET_NERVE(GameScene, GameSceneGameOver))) {
        setNerve(GET_NERVE(GameScene, GameSceneGameOver));
    }
}

void GameScene::requestEndGameOverDemo() {
    setNerve(GET_NERVE(GameScene, GameSceneSaveAfterGameOver));
}

void GameScene::requestEndMissDemo() {
    if (MR::isExistSceneObj(SceneObj_CometRetryButton)) {
        setNerve(GET_NERVE(GameScene, GameSceneCometRetryAfterMiss));
    } else {
        MR::requestChangeStageAfterMiss();
    }
}

void GameScene::requestPowerStarGetDemo() {
    setNerve(GET_NERVE(GameScene, GameScenePowerStarGet));
}

void GameScene::requestGrandStarGetDemo() {
    setNerve(GET_NERVE(GameScene, GameSceneGrandStarGet));
}

void GameScene::setNerveAfterPauseMenu() {
    setNerve(GET_NERVE(GameScene, GameSceneAction));
}

bool GameScene::isExecScenarioOpeningCamera() const {
    return isNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera));
}

bool GameScene::isExecScenarioStarter() const {
    return isNerve(GET_NERVE(GameScene, GameSceneScenarioStarter));
}

bool GameScene::isExecStageClearDemo() const {
    return isNerve(GET_NERVE(GameScene, GameScenePowerStarGet)) || isNerve(GET_NERVE(GameScene, GameSceneGrandStarGet));
}

void GameScene::exeScenarioOpeningCamera() {
    mScenarioCamera->update();
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();

    if (mScenarioCamera->isDone()) {
        if (!MR::isBeginScenarioStarter()) {
            MR::activateDefaultGameLayout();
            MR::tryFrameToScreenCinemaFrame();
            MR::stopSubBGM(0);
            MR::stopStageBGM(0);
            MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
            setNerve(GET_NERVE(GameScene, GameSceneAction));
        } else {
            setNerve(GET_NERVE(GameScene, GameSceneScenarioStarter));
        }
    }
}

void GameScene::exeCometRetryAfterMiss() {
    CometRetryButton* pCometRetryButton = ::getCometRetryButton();

    if (MR::isFirstStep(this)) {
        ::getCometRetryButton()->appear();
        MR::forceOpenWipeCircle();
    }

    pCometRetryButton->movement();
}

void GameScene::exeSaveAfterGameOver() {
    if (MR::isFirstStep(this)) {
        MR::incPlayerGameOverNum();
        GameSequenceFunction::startGameDataSaveSequence(true, true);
        MR::startStarPointerModePauseMenu(this);
    }

    if (!GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        MR::endStarPointerMode(this);
        MR::requestChangeSceneAfterGameOver();
    }
}

void GameScene::exePlayMovie() {
    if (!MR::isActiveMoviePlayer() && !MR::isMoviePlayingOnSequence()) {
        setNerve(GET_NERVE(GameScene, GameSceneAction));
        SceneFunction::movementStopSceneController();
        SceneFunction::executeMovementList();
    } else {
        SceneFunction::executeMovementListOnPlayingMovie();
    }
}

void GameScene::exeGalaxyMap() {
    if (MR::isFirstStep(this)) {
        MR::startStarPointerModePauseMenu(this);
    }

    if (!MR::isActiveGalaxyMapLayout()) {
        MR::endStarPointerMode(this);
        setNerve(GET_NERVE(GameScene, GameSceneAction));
    } else {
        CategoryList::execute(MR::MovementType_LayoutOnPause);
    }
}

void GameScene::initSequences() {
    GameStageClearSequence* pStageClearSeq = new GameStageClearSequence();
    pStageClearSeq->initWithoutIter();
    mStageClearSeq = pStageClearSeq;

    GamePauseSequence* pPauseSeq = new GamePauseSequence();
    pPauseSeq->initWithoutIter();
    mPauseSeq = pPauseSeq;

    mScenarioCamera = new GameSceneScenarioOpeningCameraState();
    mPauseCtrl = new GameScenePauseControl(this);
    mPauseCtrl->registerNervePauseMenu(GET_NERVE(GameScene, GameScenePauseMenu));

    mPauseSeq->initWindowMenu(MR::Functor(mPauseCtrl, &GameScenePauseControl::requestPauseMenuOff));
}

void GameScene::initEffect() {
    if (MR::isEqualStageName("CosmosGardenGalaxy")) {
        SceneFunction::initEffectSystem(5120, 384);
    } else if (MR::isEqualStageName("KoopaBattleVs1Galaxy")) {
        SceneFunction::initEffectSystem(6144, 512);
    } else if (MR::isEqualStageName("AstroGalaxy")) {
        SceneFunction::initEffectSystem(3072, 512);
    } else {
        SceneFunction::initEffectSystem(3072, 256);
    }
}

void GameScene::drawMirror() const {
    if (isDrawMirror()) {
        PSMTXCopy(MR::getMirrorCameraViewMtx(), j3dSys.mViewMtx);
        MR::loadProjectionMtx();
        CategoryList::entryDrawBufferMirror();
        GXSetAlphaUpdate(GX_FALSE);
        GXSetColorUpdate(GX_TRUE);
        CategoryList::drawOpa(MR::DrawBufferType_MirrorMapObj);
        CategoryList::drawXlu(MR::DrawBufferType_MirrorMapObj);
        CategoryList::execute(MR::DrawType_CaptureScreenIndirect);
        MR::clearZBuffer();
        GXColor c = {0, 0, 255, 0};
        MR::fillScreen(c);
    }
}

#ifdef TARGET_PC
namespace {
    // Debug (PETARI_CAMLOG=1): which game camera is active, where it watches
    // and where the VR eye is, relative to the player and his gravity, twice
    // a second and whenever the camera changes.  "blocked" means the map lies
    // between the player and the eye: the eye is behind or inside geometry
    // (seen from inside, the map's faces are not hit, so the eye's own test
    // for the cutaway misses that).
    void logCameraForVr(const TVec3f& player, const TVec3f& up, const TVec3f& watch, const TVec3f* eye, u32 flags) {
        static s32 sFrames = 0;
        static const CameraParamChunk* sLastChunk = nullptr;
        CameraDirector* director = MR::getCameraDirector();
        CameraMan* man = director->getCurrentCameraMan();
        const char* kind = "other";
        const CameraParamChunk* chunk = nullptr;
        if (man == director->mCameraManGame) {
            kind = "game";
            chunk = director->mCameraManGame->mChunk;
        } else if (man == director->mCameraManEvent) {
            kind = "event";
            chunk = director->mCameraManEvent->mChunk;
        } else if (man == director->mCameraManPause) {
            kind = "pause";
        } else if (man == director->mCameraManSubjective) {
            kind = "subjective";
        }
        if (++sFrames % 30 != 0 && chunk == sLastChunk) {
            return;
        }
        sLastChunk = chunk;
        char id[96] = "-";
        if (chunk != nullptr && chunk->mParamChunkID != nullptr && chunk->mParamChunkID->mName != nullptr) {
            // Event chunk names are Shift-JIS: escape the bytes.
            char* out = id;
            for (const u8* p = (const u8*)chunk->mParamChunkID->mName; *p && out < id + sizeof(id) - 5; p++) {
                if (*p >= 0x20 && *p < 0x7F) {
                    *out++ = (char)*p;
                } else {
                    out += snprintf(out, 5, "\\x%02x", *p);
                }
            }
            *out = 0;
        }
        const char* type = chunk != nullptr ? director->mHolder->getNameStrOf(chunk->mCameraTypeIndex) : "-";
        TVec3f w = watch - player;
        f32 wUp = w.dot(up);
        TVec3f wSide = w - up * wUp;
        char eyeText[160] = "no VR eye";
        if (eye != nullptr) {
            TVec3f e = *eye - player;
            f32 eUp = e.dot(up);
            TVec3f eSide = e - up * eUp;
            TVec3f hit;
            bool blocked = MR::getFirstPolyOnLineToMap(&hit, nullptr, player, e);
            snprintf(eyeText, sizeof(eyeText), "eye %.0f %.0f %.0f: %.0f up %.0f out%s", eye->x, eye->y, eye->z, eUp, eSide.length(),
                     blocked ? "" : ", clear");
            if (blocked) {
                size_t n = strlen(eyeText);
                snprintf(eyeText + n, sizeof(eyeText) - n, ", blocked %.0f from the player", hit.distance(player));
            }
        }
        TVec3f cam = MR::getCamPos();
        OSReport("camlog: %s %s %s, watch %.0f up %.0f out, camera %.0f %.0f %.0f, player %.0f %.0f %.0f up %.2f %.2f %.2f%s, diorama on %s, %s\n", kind,
                 type, id, wUp, wSide.length(), cam.x, cam.y, cam.z, player.x, player.y, player.z, up.x, up.y, up.z,
                 (flags & PORT_GX_CAMERA_GROUNDED) ? " on the ground" : "", (flags & PORT_GX_CAMERA_CENTRE_PLAYER) ? "the player" : "the watched point",
                 eyeText);
    }
}  // namespace
#endif

// inline
bool GameScene::isPlayMovie() const {
    return MR::isActiveMoviePlayer() || MR::isMoviePlayingOnSequence() || isNerve(GET_NERVE(GameScene, GameScenePlayMovie));
}

void GameScene::draw3D() const {
    if (!mDraw3D) {
        return;
    }

    if (isPlayMovie()) {
        return;
    }

    CategoryList::execute(MR::DrawType_AstroDomeSkyClear);
    MR::loadViewMtx();
#ifdef TARGET_PC
    {
        // Hand the camera to the VR renderer.
        const CameraPoseParam* pose = MR::getCameraDirector()->mPoseParam1;
        float cam[PORT_GX_CAMERA_WORDS];
        const TPos3f& view = MR::getCameraViewMtx();
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 4; c++) {
                cam[r * 4 + c] = view.mMtx[r][c];
            }
        }
        const TVec3f* v[4] = {&pose->mPos, &pose->mWatchPos, &pose->mUpVec, &pose->mWatchUpVec};
        for (int i = 0; i < 4; i++) {
            cam[12 + i * 3] = v[i]->x;
            cam[13 + i * 3] = v[i]->y;
            cam[14 + i * 3] = v[i]->z;
        }
        cam[24] = pose->mFovy;
        u32 flags = 0;
        s32 pointerMode = MR::getStarPointerModeForPort();
        // Choosing a galaxy in an observatory dome means pointing at and
        // dragging the galaxy spheres: shown on the flat screen, where the
        // pointer lands exactly where the player aims.
        bool domeSelect = pointerMode == StarPointerMode_SphereSelectorOnReaction || pointerMode == StarPointerMode_SphereSelectorFinger;
        // The first-person view looks out through Mario's eyes: also flat.
        // So are scripted transits (port_vr_transit_begin) while Mario is
        // still bound to the object carrying him: in the diorama he would
        // leave the view mid-flight, while the world turned with the gravity
        // of every planet he passed, and a pipe through a planet would put
        // the viewer inside it (the game camera pulls back to show it whole).
        bool transit = false;
        if (MR::isExistMario()) {
            const HitSensor* rush = MR::isPlayerInRush() ? MarioAccess::getPlayerActor()->_924 : nullptr;
            if (rush == nullptr) {
                port_vr_transit_end(port_vr_transit_host());
            } else {
                transit = rush->mHost == port_vr_transit_host();
            }
        }
        // A galaxy's opening shots and Mario's flight into it are camera
        // work too: shown on the screen, the diorama flew the whole world
        // around the player.
        bool arriving = isNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera)) || isNerve(GET_NERVE(GameScene, GameSceneScenarioStarter));
        if (!MR::isStageFileSelect() && !MR::isDemoActive() && !domeSelect && !MR::isFirstPersonCamera() && !transit && !arriving) {
            flags |= PORT_GX_CAMERA_DIORAMA;
        }
        if (pointerMode <= StarPointerMode_PictureBook || domeSelect) {
            flags |= PORT_GX_CAMERA_POINTER_UI;
        }
        cam[27] = cam[28] = cam[29] = 0.0f;
        cam[30] = cam[31] = cam[32] = 0.0f;
        if (MR::isExistMario()) {
            const TVec3f* player = MR::getPlayerCenterPos();
            cam[27] = player->x;
            cam[28] = player->y;
            cam[29] = player->z;
            // The diorama keeps this up: the camera's own idea of up
            // (its watch-up vector) flips during some camera modes.
            const TVec3f* gravity = MR::getPlayerGravity();
            cam[30] = -gravity->x;
            cam[31] = -gravity->y;
            cam[32] = -gravity->z;
            flags |= PORT_GX_CAMERA_PLAYER;
            if (MR::isOnGroundPlayer()) {
                flags |= PORT_GX_CAMERA_GROUNDED;
            }
            // The diorama centres Mario, except while the game camera shows
            // something else: the galaxy's opening shots, and event cameras
            // watching a point far from him (a door opening elsewhere).  An
            // event camera near him, like the one on the Dino Piranha after
            // each hit, still centres him: the diorama shows that anyway.
            static bool sShowingElsewhere = false;
            CameraDirector* director = MR::getCameraDirector();
            bool eventCamera = director->getCurrentCameraMan() == director->mCameraManEvent;
            f32 watchDistance = pose->mWatchPos.distance(*player);
            sShowingElsewhere = eventCamera && watchDistance > (sShowingElsewhere ? 1500.0f : 2500.0f);
            if (!sShowingElsewhere && !isNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera))) {
                flags |= PORT_GX_CAMERA_CENTRE_PLAYER;
            }
            // Is level geometry between the headset and Mario?  The VR layer
            // then cuts it away.  The ray stops short of Mario's centre so the
            // ground he stands on does not count.
            f32 head[12] = {}, tanX, tanY;
            bool vrEye = port_vr_cull_view(head, &tanX, &tanY) != 0;
            TVec3f eye(head[3], head[7], head[11]);
            if (vrEye) {
                TVec3f toPlayer(player->x - eye.x, player->y - eye.y, player->z - eye.z);
                f32 dist = toPlayer.length();
                if (dist > 150.0f) {
                    toPlayer.scale((dist - 100.0f) / dist);
                    if (MR::getFirstPolyOnLineToMap(nullptr, nullptr, eye, toPlayer)) {
                        flags |= PORT_GX_CAMERA_OCCLUDED;
                    }
                }
            }
            static const bool sCamLog = getenv("PETARI_CAMLOG") != nullptr;
            if (sCamLog) {
                logCameraForVr(*player, TVec3f(-gravity->x, -gravity->y, -gravity->z), pose->mWatchPos, vrEye ? &eye : nullptr, flags);
            }
        }
        memcpy(&cam[25], &flags, 4);
        cam[26] = MR::getAspect();
        port_gx_camera(cam);
        // The presentation hints as they change, for the log.
        static s32 sLoggedHints = -1;
        s32 hints = (flags & (PORT_GX_CAMERA_DIORAMA | PORT_GX_CAMERA_POINTER_UI)) | (MR::isDemoActive() ? 0x10000 : 0);
        if (hints != sLoggedHints) {
            sLoggedHints = hints;
            port_log("vr hints: %s, pointer %s (mode %d)%s", (flags & PORT_GX_CAMERA_DIORAMA) ? "diorama" : "flat screen",
                     (flags & PORT_GX_CAMERA_POINTER_UI) ? "on menus" : "in the world", (int)pointerMode, MR::isDemoActive() ? ", cutscene" : "");
        }
    }
#endif
    MR::loadProjectionMtx();
    MR::setDefaultViewportAndScissor();
    GXSetColorUpdate(GX_TRUE);
    GXSetAlphaUpdate(GX_TRUE);
    GXSetDstAlpha(GX_TRUE, 0);
    CategoryList::drawOpa(MR::DrawBufferType_ClippedMapParts);
    CategoryList::execute(MR::DrawType_ClipArea);
    CategoryList::execute(MR::DrawType_FallOutFieldDraw);
    SceneFunction::executeDrawBufferListNormalOpaBeforeVolumeShadow();
    GXSetAlphaUpdate(GX_TRUE);
    CategoryList::execute(MR::DrawType_ShadowVolume);
    GXSetColorUpdate(GX_TRUE);
    GXSetDstAlpha(GX_TRUE, 0);
    SceneFunction::executeDrawBufferListNormalOpaBeforeSilhouette();
    CategoryList::execute(MR::DrawType_0x28);
    SceneFunction::executeDrawSilhouetteAndFillShadow();
    SceneFunction::executeDrawAlphaShadow();
    CategoryList::execute(MR::DrawType_BrightSun);
    MR::setLensFlareDrawSyncToken();
    GXSetAlphaUpdate(GX_FALSE);
    GXSetDstAlpha(GX_FALSE, 0);
    SceneFunction::executeDrawBufferListNormalOpa();
    SceneFunction::executeDrawListOpa();
    CategoryList::drawOpa(MR::DrawBufferType_UNK_0x18);
    SceneFunction::executeDrawBufferListNormalXlu();
    SceneFunction::executeDrawListXlu();
    CategoryList::drawXlu(MR::DrawBufferType_UNK_0x18);
    CategoryList::execute(MR::DrawType_ShadowSurface);
    CategoryList::execute(MR::DrawType_EffectDraw3D);
    CategoryList::execute(MR::DrawType_EffectDrawForBloomEffect);
    CategoryList::execute(MR::DrawType_CenterScreenBlur);
    CategoryList::execute(MR::DrawType_CaptureScreenIndirect);
    SceneFunction::executeDrawAfterIndirect();
    CategoryList::execute(MR::DrawType_0x33);
    MR::setStarPointerDrawSyncToken();
    MR::setTalkDirectorDrawSyncToken();
#ifdef TARGET_PC
    // The scene's depth, before the image effects: the VR renderer keeps it
    // for SpaceWarp (the frame ends with a Z clear).
    port_gx_marker(PORT_GX_MARK_SCENE_DEPTH);
#endif
    SceneFunction::executeDrawImageEffect();
    GXSetDither(GX_TRUE);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    CategoryList::execute(MR::DrawType_EffectDrawAfterImageEffect);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    MR::loadLightPlayer();
    CategoryList::execute(MR::DrawType_0x24);
    CategoryList::execute(MR::DrawType_0x3B);
    MR::clearZBuffer();
}

void GameScene::draw2D() const {
    if (isPlayMovie()) {
        SceneFunction::executeDrawList2DMovie();
    } else {
        SceneFunction::executeDrawList2DNormal();
    }

    CategoryList::execute(MR::DrawType_CameraCover);
    CategoryList::execute(MR::DrawType_LayoutOnPause);
    mPauseSeq->draw();
    MR::drawInitFor2DModel();
    CategoryList::drawOpa(MR::DrawBufferType_0x26);
    CategoryList::drawXlu(MR::DrawBufferType_0x26);
    CategoryList::execute(MR::DrawType_WipeLayout);
}

bool GameScene::isValidScenarioOpeningCamera() const {
    return MR::hasStartAnimCamera() != false;
}

void GameScene::drawOdhCapture() const {
    if (!_29) {
        return;
    }

    if (!MR::isRequestedCaptureOdhImage()) {
        return;
    }

    MR::setPortCaptureOdhImage();
    CategoryList::execute(MR::DrawType_MessageBoardCapture);
    MR::captureOdhImage();
    MR::reinitGX();
}

void GameScene::startStagePlayFirst() {
    if (isValidScenarioOpeningCamera()) {
        setNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera));

        if (!MR::isBeginScenarioStarter()) {
            MR::stopSubBGM(0);
            MR::stopStageBGM(0);
            MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
        }
    } else if (MR::isBeginScenarioStarter()) {
        MR::forceToBlankCinemaFrame();
        MR::forceOpenWipeFade();
        setNerve(GET_NERVE(GameScene, GameSceneScenarioStarter));
    } else {
        if (MR::isStageBeginFadeWipe()) {
            MR::openWipeFade(180);
        } else if (MR::isStageBeginTitleWipe()) {
            MR::openWipeFade(30);
        } else if (MR::isStageBeginWithoutWipe()) {
            MR::forceOpenWipeFade();
        } else {
            MR::openWipeCircle();
        }

        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
        MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
        MR::executeOnWelcomeAndRetry();
        setNerve(GET_NERVE(GameScene, GameSceneAction));
    }
}

void GameScene::startStagePlayRetry() {
    MR::openWipeCircle();

    if (MR::isEqualStageName("SurfingLv1Galaxy") && MR::getPlayerRestartIdInfo()->_0 == 1) {
        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
    } else if (MR::isEqualStageName("SurfingLv2Galaxy") && MR::getPlayerRestartIdInfo()->_0 == 1) {
        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
    } else {
        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
        MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
    }

    MR::executeOnWelcomeAndRetry();
    setNerve(GET_NERVE(GameScene, GameSceneAction));
}

bool GameScene::isPermitToPauseMenu() const {
    return !MR::isStageDisablePauseMenu() && isNerve(GET_NERVE(GameScene, GameSceneAction)) && !MR::isDemoActive() && !MR::isWipeActive() &&
           !MR::isWipeBlank() && !MR::isPlayerDead() && !MR::isPlayerDamaging();
}

void GameScene::requestShowGalaxyMap() {
    setNerve(GET_NERVE(GameScene, GameSceneGalaxyMap));
}

void GameScene::requestStaffRoll() {
    setNerve(GET_NERVE(GameScene, GameSceneStaffRoll));
}

bool GameScene::isDrawMirror() const {
    if (!MR::isExistMirrorCamera()) {
        return false;
    }

    if (isPlayMovie()) {
        return false;
    }

    return MR::isPlayerInAreaObj("MirrorArea");
}

void GameScene::stageClear() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(GameScene, GameScenePowerStarGet))) {
            mStageClearSeq->startPowerStarGetDemo();
        } else if (isNerve(GET_NERVE(GameScene, GameSceneGrandStarGet))) {
            mStageClearSeq->startGrandStarGetDemo();
        }
    }

    mStageClearSeq->movement();
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}

void GameScene::exeStaffRoll() {
    MR::getSceneNameObjMovementController()->movement();
    CategoryList::execute(MR::MovementType_Layout);
    CategoryList::execute(MR::DrawType_LayoutDecoration);
    CategoryList::execute(MR::MovementType_WipeLayout);
}

void GameScene::exeTimeUp() {
    if (MR::isFirstStep(this)) {
        MR::startGlobalTimerTimeUp();
    }
}

void GameScene::exeGameOver() {
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}

void GameScene::exeGrandStarGet() {
    stageClear();
}

void GameScene::exePowerStarGet() {
    stageClear();
}

void GameScene::exePauseMenu() {
    if (MR::isFirstStep(this)) {
    }

    mPauseSeq->movement();
    CategoryList::execute(MR::MovementType_LayoutOnPause);
}

void GameScene::exeSceneAction() {
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}

void GameScene::exeScenarioStarter() {
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}
