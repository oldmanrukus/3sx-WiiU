#include "main.h"
#include "arcade/arcade_balance.h"
#include "args.h"
#include "common.h"
#include "configuration.h"
#include "netplay/netplay.h"
#if defined(__WIIU__)
#include "port/wiiu/wiiu_app.h"
#else
#include "port/sdl/sdl_app.h"
#endif
#include "sf33rd/AcrSDK/common/mlPAD.h"
#include "sf33rd/AcrSDK/ps2/flps2debug.h"
#include "sf33rd/AcrSDK/ps2/flps2etc.h"
#include "sf33rd/AcrSDK/ps2/flps2render.h"
#include "sf33rd/AcrSDK/ps2/foundaps2.h"
#include "sf33rd/Source/Common/MemMan.h"
#include "sf33rd/Source/Common/PPGFile.h"
#include "sf33rd/Source/Common/PPGWork.h"
#include "sf33rd/Source/Compress/zlibApp.h"
#include "sf33rd/Source/Game/debug/Debug.h"
#include "sf33rd/Source/Game/effect/effect.h"
#include "sf33rd/Source/Game/engine/plcnt.h"
#include "sf33rd/Source/Game/engine/workuser.h"
#include "sf33rd/Source/Game/init3rd.h"
#include "sf33rd/Source/Game/io/gd3rd.h"
#include "sf33rd/Source/Game/io/ioconv.h"
#include "sf33rd/Source/Game/menu/menu.h"
#include "sf33rd/Source/Game/rendering/color3rd.h"
#include "sf33rd/Source/Game/rendering/dc_ghost.h"
#include "sf33rd/Source/Game/rendering/mtrans.h"
#include "sf33rd/Source/Game/rendering/texcash.h"
#include "sf33rd/Source/Game/sound/sound3rd.h"
#include "sf33rd/Source/Game/stage/bg.h"
#include "sf33rd/Source/Game/system/ramcnt.h"
#include "sf33rd/Source/Game/system/sys_sub.h"
#include "sf33rd/Source/Game/system/sys_sub2.h"
#include "sf33rd/Source/Game/system/work_sys.h"
#include "sf33rd/Source/PS2/mc/knjsub.h"
#include "sf33rd/Source/PS2/mc/mcsub.h"
#include "structs.h"
#include "test/test_runner.h"

#if DEBUG
#include "sf33rd/Source/Game/debug/debug_config.h"
#endif

#include "port/io/afs.h"
#include "port/resources.h"

#include <SDL3/SDL.h>

#if _WIN32 && DEBUG
#include <windef.h>
#include <ConsoleApi.h>
#endif

#include <memory.h>
#include <stdbool.h>
#include <stdio.h>

/* Wii U OSScreen debug */
#if defined(__WIIU__)
#include <coreinit/time.h>
#include <coreinit/thread.h>
#include <coreinit/debug.h>
#include <coreinit/screen.h>
#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <whb/proc.h>
#include <whb/sdcard.h>

static void* screen_buf_tv = NULL;
static void* screen_buf_drc = NULL;
static bool osscreen_ready = false;
static int dbg_line = 0;

static void dbg_init(void) {
    OSScreenInit();
    uint32_t tv_size = OSScreenGetBufferSizeEx(SCREEN_TV);
    uint32_t drc_size = OSScreenGetBufferSizeEx(SCREEN_DRC);
    screen_buf_tv = MEMAllocFromDefaultHeapEx(tv_size, 0x100);
    screen_buf_drc = MEMAllocFromDefaultHeapEx(drc_size, 0x100);
    if (!screen_buf_tv || !screen_buf_drc) return;
    OSScreenSetBufferEx(SCREEN_TV, screen_buf_tv);
    OSScreenSetBufferEx(SCREEN_DRC, screen_buf_drc);
    OSScreenEnableEx(SCREEN_TV, TRUE);
    OSScreenEnableEx(SCREEN_DRC, TRUE);
    osscreen_ready = true;
}

static void dbg_clear(void) {
    if (!osscreen_ready) return;
    OSScreenClearBufferEx(SCREEN_TV, 0x000000FF);
    OSScreenClearBufferEx(SCREEN_DRC, 0x000000FF);
    dbg_line = 0;
}

static void dbg_print(const char* text) {
    if (!osscreen_ready) return;
    OSScreenPutFontEx(SCREEN_TV, 0, dbg_line, text);
    OSScreenPutFontEx(SCREEN_DRC, 0, dbg_line, text);
    dbg_line++;
}

static void dbg_flip(void) {
    if (!osscreen_ready) return;
    DCFlushRange(screen_buf_tv, OSScreenGetBufferSizeEx(SCREEN_TV));
    DCFlushRange(screen_buf_drc, OSScreenGetBufferSizeEx(SCREEN_DRC));
    OSScreenFlipBuffersEx(SCREEN_TV);
    OSScreenFlipBuffersEx(SCREEN_DRC);
}

static void dbg_msg(const char* msg, int secs) {
    dbg_clear();
    dbg_print("=== 3SX Wii U ===");
    dbg_print(msg);
    dbg_flip();
    OSSleepTicks(OSSecondsToTicks(secs));
}

#else
#define dbg_init() do {} while(0)
#define dbg_clear() do {} while(0)
#define dbg_print(t) do {} while(0)
#define dbg_flip() do {} while(0)
#define dbg_msg(m, s) do {} while(0)
#endif

typedef enum MainPhase {
    MAIN_PHASE_INIT,
    MAIN_PHASE_COPYING_RESOURCES,
    MAIN_PHASE_INITIALIZED,
} MainPhase;

s32 system_init_level;
MPP mpp_w;
Configuration configuration = { 0 };

static u8 dctex_linear_mem[0x800];
static u8 texcash_melt_buffer_mem[0x1000];
static u8 tpu_free_mem[0x2000];
static MainPhase phase = MAIN_PHASE_INIT;

static u8* mppMalloc(u32 size) {
    return flAllocMemory(size);
}

static void set_netplay_params() {}

static void cpInitTask() {
    memset(&task, 0, sizeof(task));
}

static void njUserInit() {
    s32 i;
    u32 size;

    sysFF = 1;
    mpp_w.sysStop = false;
    mpp_w.inGame = false;
    mpp_w.language = 0;
    mmSystemInitialize();
    flGetFrame(&mpp_w.fmsFrame);
    seqsInitialize(mppMalloc(seqsGetUseMemorySize()));
    ppg_Initialize(mppMalloc(0x60000), 0x60000);
    zlib_Initialize(mppMalloc(0x10000), 0x10000);
    size = flGetSpace();
    mpp_w.ramcntBuff = mppMalloc(size);
    Init_ram_control_work(mpp_w.ramcntBuff, size);

    for (i = 0; i < 0x14; i++) {
        mpp_w.useChar[i] = 0;
    }

    Interrupt_Timer = 0;
    Disp_Size_H = 100;
    Disp_Size_V = 100;
    Country = 4;

    if (Country == 0) {
        while (1) {}
    }

    Init_sound_system();
    Init_bgm_work();
    sndInitialLoad();
    cpInitTask();
    cpReadyTask(TASK_INIT, Init_Task);
}

static void distributeScratchPadAddress() {
    dctex_linear = (s16*)dctex_linear_mem;
    texcash_melt_buffer = (u8*)texcash_melt_buffer_mem;
    tpu_free = (TexturePoolUsed*)tpu_free_mem;
}

static void sf3_init() {
#if DEBUG
    DebugConfig_Init();
#endif

    flInitialize();
    flSetRenderState(FLRENDER_BACKCOLOR, 0);
    system_init_level = 0;
    ppgWorkInitializeApprication();
    distributeScratchPadAddress();
    njdp2d_init();
    njUserInit();
    palCreateGhost();
    ppgMakeConvTableTexDC();
    appSetupBasePriority();
    MemcardInit();
}

#if _WIN32 && DEBUG
static void init_windows_console() {
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0) {
        AllocConsole();
    }
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
}
#endif

static void initialize_game() {
    dbg_msg("SDLApp_FullInit...", 1);
    SDLApp_FullInit();

#if _WIN32 && DEBUG
    init_windows_console();
#endif

    set_netplay_params();
    ArcadeBalance_Init();
    dbg_msg("AFS_Init...", 1);
    AFS_Init(Resources_GetAFSPath());

    {
        char buf[128];
        snprintf(buf, sizeof(buf), "AFS files: %u", AFS_GetFileCount());
        dbg_msg(buf, 2);
    }

    sf3_init();
    dbg_msg("Init complete!", 2);
}

static void cleanup() {
    AFS_Finish();
    SDLApp_Quit();
}

// Iteration

static void cpLoopTask() {
    for (int i = 0; i < 11; i++) {
        struct _TASK* task_ptr = &task[i];

        switch (task_ptr->condition) {
        case 1:
            if (task_ptr->func_adrs) {
#if defined(TARGET_WIIU)
                if (i != TASK_GAME) {
                    task_ptr->func_adrs(task_ptr);
                }
#else
                task_ptr->func_adrs(task_ptr);
#endif
            }
            break;

        case 2:
            task_ptr->condition = 1;
            break;

        case 3:
            break;
        }
    }
}

static void appCopyKeyData() {
    PLsw[0][1] = PLsw[0][0];
    PLsw[1][1] = PLsw[1][0];
    PLsw[0][0] = p1sw_buff;
    PLsw[1][0] = p2sw_buff;
}

void njUserMain() {
    CPU_Time_Lag[0] = 0;
    CPU_Time_Lag[1] = 0;
    CPU_Rec[0] = 0;
    CPU_Rec[1] = 0;

    Check_Replay_Status(0, Replay_Status[0]);
    Check_Replay_Status(1, Replay_Status[1]);

    cpLoopTask();

    if ((Game_pause != 0x81) && (Mode_Type == MODE_VERSUS) && (Play_Mode == 1)) {
        if ((plw[0].wu.operator == 0) && (CPU_Rec[0] == 0) && (Replay_Status[0] == 1)) {
            p1sw_0 = 0;

            Check_Replay_Status(0, 1);

            if (Debug_w[0x21]) {
                flPrintColor(0xFFFFFFFF);
                flPrintL(0x10, 0xA, "FAKE REC! PL1");
            }
        }

        if ((plw[1].wu.operator == 0) && (CPU_Rec[1] == 0) && (Replay_Status[1] == 1)) {
            p2sw_0 = 0;

            Check_Replay_Status(1, 1);

            if (Debug_w[0x21]) {
                flPrintColor(0xFFFFFFFF);
                flPrintL(0x10, 0xA, "FAKE REC!     PL2");
            }
        }
    }
}

#if DEBUG
static void configure_slow_timer() {
    if (test_flag) { return; }
    if (mpp_w.sysStop) {
        sysSLOW = 1;
        switch (io_w.data[1].sw_new) {
        case SWK_LEFT_STICK: mpp_w.sysStop = false;
        case SWK_LEFT_SHOULDER: Slow_Timer = 1; break;
        default:
            switch (io_w.data[1].sw & (SWK_LEFT_SHOULDER | SWK_LEFT_TRIGGER)) {
            case SWK_LEFT_SHOULDER | SWK_LEFT_TRIGGER:
                if ((sysFF = Debug_w[1]) == 0) sysFF = 1;
                sysSLOW = 1; Slow_Timer = 1; break;
            case SWK_LEFT_TRIGGER:
                if (Slow_Timer == 0) { if ((Slow_Timer = Debug_w[0]) == 0) Slow_Timer = 1; sysFF = 1; } break;
            default: Slow_Timer = 2; break;
            } break;
        }
    } else if (io_w.data[1].sw_new & SWK_LEFT_STICK) { mpp_w.sysStop = true; }
}
#endif

static void game_step_0() {
    AFS_RunServer();

    flSetRenderState(FLRENDER_BACKCOLOR, 0xFF000000);

#if DEBUG
    if (Debug_w[0x43]) {
        flSetRenderState(FLRENDER_BACKCOLOR, 0xFF0000FF);
    }
#endif

    appSetupTempPriority();
    flPADGetALL();
    keyConvert();

#if DEBUG
    if (configuration.test.enabled) {
        TestRunner_Prologue();
    }

    configure_slow_timer();
#endif

    if ((Play_Mode != 3 && Play_Mode != 1) || (Game_pause != 0x81)) {
        p1sw_1 = p1sw_0;
        p2sw_1 = p2sw_0;
        p3sw_1 = p3sw_0;
        p4sw_1 = p4sw_0;
        p1sw_0 = p1sw_buff;
        p2sw_0 = p2sw_buff;
        p3sw_0 = p3sw_buff;
        p4sw_0 = p4sw_buff;

        if ((task[TASK_MENU].condition == 1) && (Mode_Type == MODE_PARRY_TRAINING) && (Play_Mode == 1)) {
            const u16 sw_buff = p2sw_0;
            p2sw_0 = p1sw_0;
            p1sw_0 = sw_buff;
        }
    }

    appCopyKeyData();

    mpp_w.inGame = false;

    njUserMain();
    seqsBeforeProcess();
    njdp2d_draw();
    seqsAfterProcess();

    KnjFlush();
    disp_effect_work();
    flFlip(0);
}

static void game_step_1() {
    Interrupt_Timer += 1;
    Record_Timer += 1;

    Scrn_Renew();
    Irl_Family();
    Irl_Scrn();
    BGM_Server();

#if DEBUG
    if (configuration.test.enabled) {
        TestRunner_Epilogue();
    }
#endif
}

static bool sdl_poll_helper() {
#if defined(__WIIU__)
    return SDLApp_PollEvents();
#else
    SDL_Event event;
    bool continue_running = true;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            continue_running = false;
        }
    }

    return continue_running;
#endif
}

static int loop_frame = 0;

static int loop() {
    bool is_running = true;

    while (is_running) {
        switch (phase) {
        case MAIN_PHASE_INIT:
            SDLApp_PreInit();

#if defined(__WIIU__)
            dbg_init();
            dbg_msg("PreInit complete", 2);
#endif

            if (Resources_Check()) {
                dbg_msg("Resources found! Initializing...", 2);
                initialize_game();
                phase = MAIN_PHASE_INITIALIZED;
            } else {
                dbg_msg("ERROR: SF33RD.AFS not found!", 5);
                phase = MAIN_PHASE_COPYING_RESOURCES;
            }

            break;

        case MAIN_PHASE_COPYING_RESOURCES:
            is_running = sdl_poll_helper();
            if (!is_running) break;
            SDL_Delay(16);
            const bool resource_flow_ended = Resources_RunResourceCopyingFlow();
            if (resource_flow_ended) {
                initialize_game();
                phase = MAIN_PHASE_INITIALIZED;
            }
            break;

        case MAIN_PHASE_INITIALIZED:
            is_running = SDLApp_PollEvents();
            if (!is_running) break;

            loop_frame++;

#if defined(__WIIU__)
            if (loop_frame % 60 == 1) {
                char buf[80];
                dbg_clear();
                dbg_print("=== 3SX RUNNING ===");
                snprintf(buf, sizeof(buf), "Frame: %d", loop_frame);
                dbg_print(buf);

                for (int t = 0; t < 11; t++) {
                    if (task[t].condition != 0 || task[t].func_adrs != NULL) {
                        snprintf(buf, sizeof(buf), "Task[%d] cond:%d r:[%d,%d]",
                                 t, task[t].condition,
                                 task[t].r_no[0], task[t].r_no[1]);
                        dbg_print(buf);
                    }
                }

                dbg_flip();
            }
#endif

            SDLApp_BeginFrame();
            game_step_0();
            SDLApp_EndFrame();
            game_step_1();
            break;
        }
    }

    cleanup();
    return 0;
}

int main(int argc, const char* argv[]) {
    read_args(argc, argv, &configuration);
    return loop();
}

s32 mppGetFavoritePlayerNumber() {
    s32 i;
    s32 max = 1;
    s32 num = 0;

#if DEBUG
    if (Debug_w[0x2D]) {
        return Debug_w[0x2D] - 1;
    }
#endif

    for (i = 0; i < 0x14; i++) {
        if (max <= mpp_w.useChar[i]) {
            max = mpp_w.useChar[i];
            num = i + 1;
        }
    }

    return num;
}

// Tasks

void cpReadyTask(TaskID num, void* func_adrs) {
    struct _TASK* task_ptr = &task[num];

    memset(task_ptr, 0, sizeof(struct _TASK));

    task_ptr->func_adrs = func_adrs;
    task_ptr->condition = 2;
}

void cpExitTask(TaskID num) {
    SDL_zero(task[num]);
}
