/*
 * MGO Budget Plugin: Linux/Proton fixes for Skyrim VR as an SKSE plugin.
 * Copyright (C) 2026 CrashTD. Licensed under GPL-3.0-or-later, see LICENSE.
 *
 * Everything below only runs under Wine/Proton (ntdll exports wine_get_version); on Windows
 * the plugin loads, logs one line and does nothing. Settings: MGOBudgetPlugin.ini next to the
 * DLL. Log: MGOBudgetPlugin.log next to the DLL (under MO2: overwrite/SKSE/Plugins/).
 *
 * 1. DXVK options ([DXVK] Config=): appended to DXVK_CONFIG before the game creates its first
 *    DXGI factory. SKSE calls SKSEPlugin_Load before the game's D3D11 device exists, and DXVK
 *    reads DXVK_CONFIG when the first factory creates its DxvkInstance (dxgi_factory.cpp:83,
 *    DXVK v3.0.2). Replaces the DXVK_CONFIG part of the launch options.
 *
 * 2. Memory budget ([Budget] MiB=): Community Shaders checks the DXGI memory budget before it
 *    switches to its scaled VR render targets. Under Proton, DXVK passes on
 *    VK_EXT_memory_budget; on RADV that excludes memory of other processes, while the usage
 *    also counts Proton's FSR4 (vkd3d) device. CSX then sees "Critical" and stays at native
 *    resolution. The plugin replaces IDXGIAdapter3::QueryVideoMemoryInfo in the DXVK adapter
 *    vtable (shared by all adapters) and raises only Budget for the local segment;
 *    CurrentUsage stays real and dxvk.maxMemoryBudget still caps DXVK's allocations.
 *
 * 3. Quest controllers ([Controller] QuestFixGenericProfile=, off by default): WiVRn reports
 *    a generic profile while Quest controllers idle or sleep; xrizer then answers with
 *    ControllerType "<unknown>" and VRIK sets up Vive Wands for the session. The plugin wraps
 *    VR_GetGenericInterface in SkyrimVR.exe's import table and, on IVRSystem_019, replaces
 *    GetStringTrackedDeviceProperty (vtable slot 28, openvr-1.0.17.h) to report the Quest 2
 *    Touch identity for such controllers. Quest owners enable it (stock xrizer does not
 *    identify the headset); a headset that clearly reports another vendor keeps it off.
 *
 * 4. Health check ([Check] Enabled=): logs known Proton pitfalls ([check] OK/WARN/INFO).
 *
 * 5. DevBench tools ([DevBench] Enabled=): if the DevBench SKSE plugin is installed, registers
 *    mgobudget.status (what the plugin set and saw) and mgobudget.set (budget live) with its
 *    local REST/MCP endpoint, e.g. curl -X POST 127.0.0.1:8921/api/tool/mgobudget.status.
 *    Interface fetched at kDataLoaded as in DevBench's MIT-licensed DevBenchAPI.h/.cpp
 *    (github.com/alandtse/devbench, include/), declared locally in C below.
 *
 * Old SKSE plugin interface (SKSEPlugin_Query/Load), structures declared locally, no
 * CommonLib. Build: see Makefile.
 */
#define COBJMACROS
#include <windows.h>
#include <dxgi1_4.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int UInt32;

typedef struct {
    UInt32 skseVersion;
    UInt32 runtimeVersion;
    UInt32 editorVersion;
    UInt32 isEditor;
    void *(*QueryInterface)(UInt32 id);
    UInt32 (*GetPluginHandle)(void);
    UInt32 (*GetReleaseIndex)(void);
} SKSEInterface;

typedef struct {
    UInt32 infoVersion;
    const char *name;
    UInt32 version;
} PluginInfo;

/* SKSE messaging (PluginAPI.h): kInterface_Messaging = 5, kMessage_DataLoaded = 8 */
typedef struct {
    const char *sender;
    UInt32 type;
    UInt32 dataLen;
    void *data;
} SKSEMessage;

typedef void (*SKSEEventCallback)(SKSEMessage *msg);

typedef struct {
    UInt32 interfaceVersion;
    bool (*RegisterListener)(UInt32 listener, const char *sender, SKSEEventCallback handler);
    bool (*Dispatch)(UInt32 sender, UInt32 messageType, void *data, UInt32 dataLen, const char *receiver);
} SKSEMessagingInterface;

enum { kInterfaceMessaging = 5, kMessageDataLoaded = 8 };

#define PLUGIN_NAME "MGOBudgetPlugin"
#define PLUGIN_VERSION 1

static HMODULE g_self;
static char g_dir[MAX_PATH];
static FILE *g_log;

/* What the plugin set and saw, for mgobudget.status */
static const char *g_wine;
static int g_budgetHooked;
static volatile LONG64 g_lastUsage, g_lastRealBudget;
static volatile LONG g_queryCount;
static int g_controllerArmed, g_controllerHooked;
static volatile LONG g_questRewrites;

static void logf_(const char *fmt, ...)
{
    if (!g_log)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static void init_paths(void)
{
    GetModuleFileNameA(g_self, g_dir, sizeof(g_dir));
    char *slash = strrchr(g_dir, '\\');
    if (slash)
        slash[1] = '\0';
}

static const char *wine_version(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll)
        return NULL;
    const char *(*fn)(void) = (const char *(*)(void))(void *)GetProcAddress(ntdll, "wine_get_version");
    return fn ? fn() : NULL;
}

static void apply_dxvk_config(void)
{
    char ini[MAX_PATH + 32], want[1024] = "";
    snprintf(ini, sizeof(ini), "%s%s.ini", g_dir, PLUGIN_NAME);
    GetPrivateProfileStringA("DXVK", "Config", "", want, sizeof(want), ini);
    if (!want[0]) {
        logf_("no [DXVK] Config= in %s, nothing to do", ini);
        return;
    }

    char have[2048] = "";
    DWORD n = GetEnvironmentVariableA("DXVK_CONFIG", have, sizeof(have));
    if (n >= sizeof(have)) {
        logf_("existing DXVK_CONFIG too long (%lu), left unchanged", (unsigned long)n);
        return;
    }

    char merged[3072];
    if (have[0])
        snprintf(merged, sizeof(merged), "%s;%s", have, want);
    else
        snprintf(merged, sizeof(merged), "%s", want);

    BOOL ok = SetEnvironmentVariableA("DXVK_CONFIG", merged);
    logf_("DXVK_CONFIG before: \"%s\"", have);
    logf_("DXVK_CONFIG after:  \"%s\" (%s)", merged, ok ? "set" : "FAILED");
    logf_("dxgi.dll loaded: %s, d3d11.dll loaded: %s (loaded is fine; DXVK reads the config at the first factory)",
          GetModuleHandleA("dxgi.dll") ? "yes" : "no", GetModuleHandleA("d3d11.dll") ? "yes" : "no");
}

static const IID kIID_IDXGIFactory1 = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
static const IID kIID_IDXGIAdapter3 = {0x645967a4, 0x1392, 0x4310, {0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd}};

typedef HRESULT(STDMETHODCALLTYPE *QueryVideoMemoryInfoFn)(IDXGIAdapter3 *, UINT, DXGI_MEMORY_SEGMENT_GROUP,
                                                           DXGI_QUERY_VIDEO_MEMORY_INFO *);
static QueryVideoMemoryInfoFn g_origQuery;
static volatile LONG64 g_budgetBytes; /* written live by mgobudget.set, 0 = pass the real budget through */
static volatile LONG g_queryLogs;

static HRESULT STDMETHODCALLTYPE hooked_query(IDXGIAdapter3 *self, UINT node, DXGI_MEMORY_SEGMENT_GROUP group,
                                              DXGI_QUERY_VIDEO_MEMORY_INFO *info)
{
    HRESULT hr = g_origQuery(self, node, group, info);
    if (FAILED(hr) || !info || group != DXGI_MEMORY_SEGMENT_GROUP_LOCAL)
        return hr;
    UINT64 real = info->Budget, want = (UINT64)g_budgetBytes;
    g_lastUsage = (LONG64)info->CurrentUsage;
    g_lastRealBudget = (LONG64)real;
    InterlockedIncrement(&g_queryCount);
    if (real < want) {
        info->Budget = want;
        if (InterlockedIncrement(&g_queryLogs) <= 5)
            logf_("QueryVideoMemoryInfo local: usage %llu MiB, budget %llu -> %llu MiB",
                  (unsigned long long)(info->CurrentUsage >> 20u), (unsigned long long)(real >> 20u),
                  (unsigned long long)(want >> 20u));
    }
    return hr;
}

static void install_budget_hook(void)
{
    char ini[MAX_PATH + 32];
    snprintf(ini, sizeof(ini), "%s%s.ini", g_dir, PLUGIN_NAME);
    UINT mib = GetPrivateProfileIntA("Budget", "MiB", 0, ini);
    if (!mib) {
        logf_("[Budget] MiB=0 or missing, budget hook off");
        return;
    }
    g_budgetBytes = (LONG64)((UINT64)mib << 20u);

    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    HRESULT(WINAPI * create)(REFIID, void **) =
        dxgi ? (HRESULT(WINAPI *)(REFIID, void **))(void *)GetProcAddress(dxgi, "CreateDXGIFactory1") : NULL;
    if (!create) {
        logf_("budget hook: CreateDXGIFactory1 not found");
        return;
    }
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    IDXGIAdapter3 *adapter3 = NULL;
    if (FAILED(create(&kIID_IDXGIFactory1, (void **)&factory)) ||
        FAILED(IDXGIFactory1_EnumAdapters1(factory, 0, &adapter)) ||
        FAILED(IDXGIAdapter1_QueryInterface(adapter, &kIID_IDXGIAdapter3, (void **)&adapter3))) {
        logf_("budget hook: no DXGI factory/adapter/IDXGIAdapter3");
        goto out;
    }

    void **slot = (void **)&adapter3->lpVtbl->QueryVideoMemoryInfo;
    DWORD old;
    if (!VirtualProtect((void *)slot, sizeof(void *), PAGE_EXECUTE_READWRITE, &old)) {
        logf_("budget hook: VirtualProtect failed (%lu)", (unsigned long)GetLastError());
        goto out;
    }
    g_origQuery = (QueryVideoMemoryInfoFn)*slot;
    *slot = (void *)hooked_query;
    VirtualProtect((void *)slot, sizeof(void *), old, &old);
    FlushInstructionCache(GetCurrentProcess(), (const void *)slot, sizeof(void *));
    g_budgetHooked = 1;
    logf_("budget hook installed: local budget at least %u MiB (vtable slot %p)", mib, (void *)slot);

out:
    if (adapter3)
        IDXGIAdapter3_Release(adapter3);
    if (adapter)
        IDXGIAdapter1_Release(adapter);
    if (factory)
        IDXGIFactory1_Release(factory);
}

/* ---- Controller identity (IVRSystem_019) ---- */

enum {
    kSlotGetControllerRole = 19,
    kSlotGetDeviceClass = 20,
    kSlotGetStringProperty = 28,
    kPropTrackingSystem = 1000,
    kPropModelNumber = 1001,
    kPropSerialNumber = 1002,
    kPropRenderModel = 1003,
    kPropManufacturer = 1005,
    kPropRegisteredType = 1036,
    kPropControllerType = 7000,
    kDeviceClassController = 2,
    kRoleLeft = 1,
    kPropErrSuccess = 0,
    kPropErrBufferTooSmall = 3,
};

typedef UInt32 (*GetStringPropFn)(void *self, UInt32 device, int prop, char *buf, UInt32 size, int *err);
typedef int (*GetIntFn)(void *self, UInt32 device);
typedef void *(*GetGenericInterfaceFn)(const char *version, int *err);

static GetStringPropFn g_origGetString;
static GetIntFn g_origGetRole, g_origGetClass;
static GetGenericInterfaceFn g_origGetInterface;
static volatile LONG g_ctlLogs;
static int g_questFixOn;
static volatile LONG g_genericWarned;
static int g_questHmd = -1; /* -1 unknown, 0 no, 1 yes */

static int contains_ci(const char *hay, const char *needle)
{
    for (; *hay; hay++)
        if (_strnicmp(hay, needle, strlen(needle)) == 0)
            return 1;
    return 0;
}

/* The user enables the fix for a Quest. Stock xrizer reports "<unknown>" as the headset
 * manufacturer, so a Quest cannot be detected; only a clearly different headset vetoes it. */
static int is_quest_hmd(void *self)
{
    if (g_questHmd >= 0)
        return g_questHmd;
    static const char *const kOther[] = {"htc", "valve", "pimax", "pico", "bigscreen", "samsung", "hp ", "varjo"};
    char manufacturer[128] = "", model[128] = "";
    int e = 0;
    g_origGetString(self, 0, kPropManufacturer, manufacturer, sizeof(manufacturer), &e);
    g_origGetString(self, 0, kPropModelNumber, model, sizeof(model), &e);
    g_questHmd = 1;
    for (size_t i = 0; i < sizeof(kOther) / sizeof(kOther[0]); i++)
        if (contains_ci(manufacturer, kOther[i]) || contains_ci(model, kOther[i]))
            g_questHmd = 0;
    logf_("headset: manufacturer \"%s\", model \"%s\" -> Quest fix %s", manufacturer, model,
          g_questHmd ? "active" : "inactive (another headset reported)");
    return g_questHmd;
}

static const char *touch_value(int prop, int left)
{
    switch (prop) {
    case kPropTrackingSystem: return "oculus";
    case kPropManufacturer: return "Oculus";
    case kPropControllerType: return "oculus_touch";
    case kPropModelNumber: return left ? "Oculus Quest2 (Left Controller)" : "Oculus Quest2 (Right Controller)";
    case kPropRenderModel: return left ? "oculus_quest2_controller_left" : "oculus_quest2_controller_right";
    case kPropSerialNumber: return left ? "WMHD315M3010GV_Controller_Left" : "WMHD315M3010GV_Controller_Right";
    case kPropRegisteredType:
        return left ? "oculus/WMHD315M3010GV_Controller_Left" : "oculus/WMHD315M3010GV_Controller_Right";
    default: return NULL;
    }
}

static UInt32 hooked_get_string(void *self, UInt32 device, int prop, char *buf, UInt32 size, int *err)
{
    UInt32 n = g_origGetString(self, device, prop, buf, size, err);
    if (!touch_value(prop, 1) || g_origGetClass(self, device) != kDeviceClassController)
        return n;

    char type[64] = "";
    int typeErr = 0;
    g_origGetString(self, device, kPropControllerType, type, sizeof(type), &typeErr);
    if (strcmp(type, "<unknown>") != 0)
        return n;
    if (!g_questFixOn) {
        if (InterlockedExchange(&g_genericWarned, 1) == 0)
            logf_("[check] WARN controller %u reports a generic profile (ControllerType \"<unknown>\"); VRIK will "
                  "likely set up Vive Wands. On a Quest set [Controller] QuestFixGenericProfile=1.",
                  device);
        return n;
    }
    if (!is_quest_hmd(self))
        return n;

    const char *value = touch_value(prop, g_origGetRole(self, device) == kRoleLeft);
    UInt32 need = (UInt32)strlen(value) + 1;
    if (buf && size >= need) {
        memcpy(buf, value, need);
        if (err)
            *err = kPropErrSuccess;
    } else if (err) {
        *err = kPropErrBufferTooSmall;
    }
    InterlockedIncrement(&g_questRewrites);
    if (InterlockedIncrement(&g_ctlLogs) <= 12)
        logf_("controller %u: generic profile, prop %d -> \"%s\"", device, prop, value);
    return need;
}

static int patch_slot(void **slot, void *fn, void **orig)
{
    DWORD old;
    if (!VirtualProtect((void *)slot, sizeof(void *), PAGE_EXECUTE_READWRITE, &old))
        return 0;
    *orig = *slot;
    *slot = fn;
    VirtualProtect((void *)slot, sizeof(void *), old, &old);
    return 1;
}

static void *hooked_get_interface(const char *version, int *err)
{
    void *iface = g_origGetInterface(version, err);
    if (iface && version && !g_origGetString && strncmp(version, "IVRSystem_019", 13) == 0) {
        void **vtbl = *(void ***)iface;
        g_origGetRole = (GetIntFn)vtbl[kSlotGetControllerRole];
        g_origGetClass = (GetIntFn)vtbl[kSlotGetDeviceClass];
        if (patch_slot(&vtbl[kSlotGetStringProperty], (void *)hooked_get_string, (void **)&g_origGetString)) {
            g_controllerHooked = 1;
            logf_("controller hook installed on %s (vtable %p)", version, (void *)vtbl);
        } else
            logf_("controller hook: VirtualProtect failed (%lu)", (unsigned long)GetLastError());
    }
    return iface;
}

/* Replaces one import of the main executable; returns the original function or NULL. */
static void *patch_import(const char *dll, const char *name, void *fn)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp((const char *)(base + d->Name), dll) != 0)
            continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA *funcs = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, funcs++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)ibn->Name, name) != 0)
                continue;
            void *orig;
            return patch_slot((void **)&funcs->u1.Function, fn, &orig) ? orig : NULL;
        }
    }
    return NULL;
}

static void install_controller_hook(void)
{
    char ini[MAX_PATH + 32];
    snprintf(ini, sizeof(ini), "%s%s.ini", g_dir, PLUGIN_NAME);
    g_questFixOn = GetPrivateProfileIntA("Controller", "QuestFixGenericProfile", 0, ini) != 0;
    logf_("[Controller] QuestFixGenericProfile=%d (hook also watches for the generic profile when off)", g_questFixOn);
    g_origGetInterface =
        (GetGenericInterfaceFn)patch_import("openvr_api.dll", "VR_GetGenericInterface", (void *)hooked_get_interface);
    g_controllerArmed = g_origGetInterface != NULL;
    logf_(g_origGetInterface ? "controller hook armed (VR_GetGenericInterface import wrapped)"
                             : "controller hook: VR_GetGenericInterface import not found");
}

/* ---- Health check: known Proton pitfalls of Skyrim VR modlists ---- */

static int g_checkWarnings;

static void check_engine_fixes(void)
{
    FILE *f = fopen("Data\\SKSE\\Plugins\\EngineFixes.toml", "r");
    if (!f) {
        logf_("[check] INFO EngineFixes.toml not found, skipped");
        return;
    }
    char line[512];
    int bad = 0, found = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (strncmp(p, "bCullingFreedObjectCrash", 24) != 0)
            continue;
        found = 1;
        bad = strstr(p, "true") != NULL;
    }
    fclose(f);
    if (bad) {
        g_checkWarnings++;
        logf_("[check] WARN EngineFixes.toml: bCullingFreedObjectCrash = true makes the picture black under Proton; "
              "set it to false (edit the copy MO2 puts in overwrite/)");
    } else {
        logf_("[check] OK   EngineFixes.toml: bCullingFreedObjectCrash %s", found ? "is false" : "not set");
    }
}

static void check_d3dcompiler(void)
{
    char path[MAX_PATH];
    GetSystemDirectoryA(path, sizeof(path));
    strncat(path, "\\d3dcompiler_47.dll", sizeof(path) - strlen(path) - 1);
    DWORD handle = 0, size = GetFileVersionInfoSizeA(path, &handle);
    BYTE *data = size ? (BYTE *)malloc(size) : NULL;
    VS_FIXEDFILEINFO *info = NULL;
    UINT len = 0;
    if (!data || !GetFileVersionInfoA(path, 0, size, data) || !VerQueryValueA(data, "\\", (void **)&info, &len) ||
        !info) {
        logf_("[check] INFO d3dcompiler_47.dll: no version info (%s), skipped", path);
        free(data);
        return;
    }
    unsigned major = HIWORD(info->dwFileVersionMS), minor = LOWORD(info->dwFileVersionMS);
    unsigned build = HIWORD(info->dwFileVersionLS), rev = LOWORD(info->dwFileVersionLS);
    free(data);
    if (major < 10) {
        g_checkWarnings++;
        logf_("[check] WARN d3dcompiler_47.dll is %u.%u.%u.%u (2013, winetricks); several CSX compute shaders fail "
              "with it. Use a current one (e.g. from mozilla/fxc2).",
              major, minor, build, rev);
    } else {
        logf_("[check] OK   d3dcompiler_47.dll %u.%u.%u.%u", major, minor, build, rev);
    }
}

static void check_data_data(void)
{
    DWORD a = GetFileAttributesA("Data\\data");
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) {
        logf_("[check] OK   Data\\data exists (fast audio path lookups)");
    } else {
        logf_("[check] INFO Data\\data does not exist: under Proton the engine's Data\\data\\... lookups can add "
              "minutes to startup. A mod with only data/placeholder.txt (plus .ciopfs markers) fixes it.");
    }
}

static void run_health_check(void)
{
    char ini[MAX_PATH + 32];
    snprintf(ini, sizeof(ini), "%s%s.ini", g_dir, PLUGIN_NAME);
    if (!GetPrivateProfileIntA("Check", "Enabled", 1, ini)) {
        logf_("[Check] Enabled=0, health check off");
        return;
    }
    check_engine_fixes();
    check_d3dcompiler();
    check_data_data();
    logf_("[check] done, %d warning(s); the controller check runs when the game asks for controllers", g_checkWarnings);
}

/* ---- DevBench tools ---- */

/* DevBenchAPI.h: IDevBenchInterface001 is a C++ object; its vtable lists the virtual functions in
 * declaration order, `this` comes first (MSVC x64). Only the first three slots are used here. */
typedef void (*DevBenchWriteFn)(void *sink, const char *resultJson);
typedef void (*DevBenchToolFn)(void *ctx, const char *argsJson, void *sink, DevBenchWriteFn write);

typedef struct DevBench DevBench;
typedef struct {
    unsigned (*GetBuildNumber)(DevBench *self);
    bool (*RegisterTool)(DevBench *self, const char *name, const char *descriptorJson, DevBenchToolFn handler,
                         void *ctx);
    void (*EmitEvent)(DevBench *self, const char *topic, const char *payloadJson);
} DevBenchVtbl;
struct DevBench {
    const DevBenchVtbl *vtbl;
};

typedef struct {
    void *(*GetApiFunction)(unsigned revision);
} DevBenchMessage;

enum { kDevBenchGetInterface = 0x9a3f1c08 };

static UInt32 g_pluginHandle;
static const SKSEMessagingInterface *g_messaging;

/* Copies s into out as JSON string content (without quotes). */
static void json_escape(const char *s, char *out, size_t size)
{
    size_t n = 0;
    for (; s && *s && n + 7 < size; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            out[n++] = '\\';
            out[n++] = (char)c;
        } else if (c < 0x20) {
            n += (size_t)snprintf(out + n, size - n, "\\u%04x", c);
        } else {
            out[n++] = (char)c;
        }
    }
    out[n] = '\0';
}

static void tool_status(void *ctx, const char *argsJson, void *sink, DevBenchWriteFn write)
{
    (void)ctx;
    (void)argsJson;
    char env[2048] = "", wine[64], dxvk[sizeof(env) * 2], out[8192];
    GetEnvironmentVariableA("DXVK_CONFIG", env, sizeof(env));
    json_escape(g_wine, wine, sizeof(wine));
    json_escape(env, dxvk, sizeof(dxvk));
    snprintf(out, sizeof(out),
             "{\"plugin\":\"%s\",\"version\":%d,\"wine\":\"%s\",\"dxvkConfig\":\"%s\","
             "\"budget\":{\"hooked\":%s,\"reportedMiB\":%llu,\"queries\":%ld,\"lastUsageMiB\":%llu,"
             "\"lastRealBudgetMiB\":%llu},"
             "\"controller\":{\"armed\":%s,\"hooked\":%s,\"questFix\":%s,\"questHmd\":%d,\"rewrites\":%ld},"
             "\"check\":{\"warnings\":%d}}",
             PLUGIN_NAME, PLUGIN_VERSION, wine, dxvk, g_budgetHooked ? "true" : "false",
             (unsigned long long)((UINT64)g_budgetBytes >> 20u), (long)g_queryCount,
             (unsigned long long)((UINT64)g_lastUsage >> 20u), (unsigned long long)((UINT64)g_lastRealBudget >> 20u),
             g_controllerArmed ? "true" : "false", g_controllerHooked ? "true" : "false",
             g_questFixOn ? "true" : "false", g_questHmd, (long)g_questRewrites, g_checkWarnings);
    write(sink, out);
}

static void tool_set(void *ctx, const char *argsJson, void *sink, DevBenchWriteFn write)
{
    (void)ctx;
    const char *p = argsJson ? strstr(argsJson, "\"mib\"") : NULL;
    if (p)
        p = strchr(p + 5, ':');
    char *end = NULL;
    long mib = p ? strtol(p + 1, &end, 10) : -1;
    if (!p || end == p + 1 || mib < 0 || mib > 65536) {
        write(sink, "{\"ok\":false,\"error\":\"expected {\\\"mib\\\": 0..65536}\"}");
        return;
    }
    if (!g_budgetHooked) {
        write(sink, "{\"ok\":false,\"error\":\"budget hook not installed ([Budget] MiB=0 at startup)\"}");
        return;
    }
    LONG64 before = InterlockedExchange64(&g_budgetBytes, (LONG64)((UINT64)mib << 20u));
    logf_("mgobudget.set: budget %llu -> %ld MiB", (unsigned long long)((UINT64)before >> 20u), mib);
    char out[160];
    snprintf(out, sizeof(out), "{\"ok\":true,\"previousMiB\":%llu,\"reportedMiB\":%ld}",
             (unsigned long long)((UINT64)before >> 20u), mib);
    write(sink, out);
}

static void register_devbench_tools(void)
{
    DevBenchMessage msg = {NULL};
    /* As in DevBenchAPI.cpp: dataLen is sizeof a pointer (never read by the host), and only the
     * filled-in function counts, not Dispatch's return value. */
    g_messaging->Dispatch(g_pluginHandle, kDevBenchGetInterface, &msg, sizeof(void *), "devbench");
    if (!msg.GetApiFunction) {
        logf_("DevBench not found, tools not registered");
        return;
    }
    DevBench *db = (DevBench *)msg.GetApiFunction(1);
    if (!db) {
        logf_("DevBench returned no interface");
        return;
    }
    db->vtbl->RegisterTool(db, "mgobudget.status",
                           "{\"description\":\"MGO Budget Plugin: what it set and saw (DXVK_CONFIG, memory budget "
                           "hook with the last real DXGI values, controller hook, health check)\","
                           "\"inputSchema\":{\"type\":\"object\",\"properties\":{}},\"readOnly\":true}",
                           tool_status, NULL);
    db->vtbl->RegisterTool(db, "mgobudget.set",
                           "{\"description\":\"MGO Budget Plugin: set the memory budget (MiB) Community Shaders sees, "
                           "live; 0 = the real budget. Needs [Budget] MiB > 0 at startup.\","
                           "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"mib\":{\"type\":\"integer\","
                           "\"minimum\":0,\"maximum\":65536}},\"required\":[\"mib\"]},\"readOnly\":false}",
                           tool_set, NULL);
    logf_("DevBench build %u: tools mgobudget.status and mgobudget.set registered", db->vtbl->GetBuildNumber(db));
}

static void on_skse_message(SKSEMessage *msg)
{
    if (msg && msg->type == kMessageDataLoaded)
        register_devbench_tools();
}

static void listen_for_devbench(const SKSEInterface *skse)
{
    char ini[MAX_PATH + 32];
    snprintf(ini, sizeof(ini), "%s%s.ini", g_dir, PLUGIN_NAME);
    if (!GetPrivateProfileIntA("DevBench", "Enabled", 1, ini)) {
        logf_("[DevBench] Enabled=0, no tools");
        return;
    }
    if (skse->QueryInterface)
        g_messaging = (const SKSEMessagingInterface *)skse->QueryInterface(kInterfaceMessaging);
    if (!g_messaging || !g_messaging->RegisterListener(g_pluginHandle, "SKSE", on_skse_message)) {
        logf_("DevBench: no SKSE messaging interface, tools not registered");
        g_messaging = NULL;
        return;
    }
    logf_("DevBench: waiting for kDataLoaded to register tools");
}

__declspec(dllexport) BOOL SKSEPlugin_Query(const SKSEInterface *skse, PluginInfo *info)
{
    info->infoVersion = 1;
    info->name = PLUGIN_NAME;
    info->version = PLUGIN_VERSION;
    return skse && !skse->isEditor;
}

__declspec(dllexport) BOOL SKSEPlugin_Load(const SKSEInterface *skse)
{
    init_paths();
    char logpath[MAX_PATH + 32];
    snprintf(logpath, sizeof(logpath), "%s%s.log", g_dir, PLUGIN_NAME);
    g_log = fopen(logpath, "w");

    logf_("%s %d loaded, SKSE 0x%08x, runtime 0x%08x", PLUGIN_NAME, PLUGIN_VERSION, skse ? skse->skseVersion : 0,
          skse ? skse->runtimeVersion : 0);
    g_wine = wine_version();
    if (!g_wine) {
        logf_("not running under Wine, doing nothing");
        return TRUE;
    }
    logf_("Wine %s", g_wine);
    apply_dxvk_config();
    install_budget_hook();
    install_controller_hook();
    run_health_check();
    if (skse) {
        g_pluginHandle = skse->GetPluginHandle ? skse->GetPluginHandle() : 0;
        listen_for_devbench(skse);
    }
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
