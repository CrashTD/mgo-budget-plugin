/* Loads the plugin like SKSE VR does (Query, then Load) and prints DXVK_CONFIG afterwards.
 * Also stands in for SKSE messaging and a DevBench host: sends kDataLoaded, then calls the
 * registered tools and prints their results. */
#define COBJMACROS
#include <windows.h>
#include <dxgi1_4.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *sender;
    unsigned type, dataLen;
    void *data;
} SKSEMessage;

typedef void (*EventCallback)(SKSEMessage *msg);

typedef struct {
    unsigned interfaceVersion;
    bool (*RegisterListener)(unsigned listener, const char *sender, EventCallback handler);
    bool (*Dispatch)(unsigned sender, unsigned messageType, void *data, unsigned dataLen, const char *receiver);
} MessagingInterface;

typedef struct {
    unsigned skseVersion, runtimeVersion, editorVersion, isEditor;
    void *(*queryInterface)(unsigned id);
    unsigned (*getPluginHandle)(void);
    void *getReleaseIndex;
} SKSEInterface;

typedef struct {
    unsigned infoVersion;
    const char *name;
    unsigned version;
} PluginInfo;

typedef BOOL (*QueryFn)(const SKSEInterface *, PluginInfo *);
typedef BOOL (*LoadFn)(const SKSEInterface *);

/* ---- DevBench host stand-in (vtable layout of IDevBenchInterface001) ---- */

typedef void (*WriteFn)(void *sink, const char *json);
typedef void (*ToolFn)(void *ctx, const char *args, void *sink, WriteFn write);

typedef struct Host Host;
typedef struct {
    unsigned (*GetBuildNumber)(Host *self);
    bool (*RegisterTool)(Host *self, const char *name, const char *descriptor, ToolFn handler, void *ctx);
    void (*EmitEvent)(Host *self, const char *topic, const char *payload);
} HostVtbl;
struct Host {
    const HostVtbl *vtbl;
};

static struct {
    char name[64];
    ToolFn fn;
    void *ctx;
} g_tools[8];
static int g_toolCount;

static unsigned host_build(Host *self)
{
    (void)self;
    return 12200;
}

static bool host_register(Host *self, const char *name, const char *descriptor, ToolFn fn, void *ctx)
{
    (void)self;
    printf("registered %s descriptor=%s\n", name, descriptor);
    if (g_toolCount < 8) {
        snprintf(g_tools[g_toolCount].name, sizeof(g_tools[0].name), "%s", name);
        g_tools[g_toolCount].fn = fn;
        g_tools[g_toolCount].ctx = ctx;
        g_toolCount++;
    }
    return true;
}

static void host_emit(Host *self, const char *topic, const char *payload)
{
    (void)self;
    printf("event %s %s\n", topic, payload);
}

static const HostVtbl g_hostVtbl = {host_build, host_register, host_emit};
static Host g_host = {&g_hostVtbl};

static void *host_get_api(unsigned revision)
{
    return revision == 1 ? &g_host : NULL;
}

static void write_result(void *sink, const char *json)
{
    snprintf((char *)sink, 4096, "%s", json);
}

static void call_tool(const char *name, const char *args)
{
    for (int i = 0; i < g_toolCount; i++) {
        if (strcmp(g_tools[i].name, name) != 0)
            continue;
        char result[4096] = "";
        g_tools[i].fn(g_tools[i].ctx, args, result, write_result);
        printf("tool %s %s -> %s\n", name, args, result);
        return;
    }
    printf("tool %s missing\n", name);
}

/* Asks DXGI for the local memory budget, as Community Shaders does (0 if DXGI is unavailable). */
static unsigned long long dxgi_budget_mib(void)
{
    static const IID iidFactory1 = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
    static const IID iidAdapter3 = {0x645967a4, 0x1392, 0x4310, {0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd}};
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    HRESULT(WINAPI * create)(REFIID, void **) =
        dxgi ? (HRESULT(WINAPI *)(REFIID, void **))(void *)GetProcAddress(dxgi, "CreateDXGIFactory1") : NULL;
    IDXGIFactory1 *factory = NULL;
    IDXGIAdapter1 *adapter = NULL;
    IDXGIAdapter3 *adapter3 = NULL;
    DXGI_QUERY_VIDEO_MEMORY_INFO info = {0};
    if (create && SUCCEEDED(create(&iidFactory1, (void **)&factory)) &&
        SUCCEEDED(IDXGIFactory1_EnumAdapters1(factory, 0, &adapter)) &&
        SUCCEEDED(IDXGIAdapter1_QueryInterface(adapter, &iidAdapter3, (void **)&adapter3)))
        IDXGIAdapter3_QueryVideoMemoryInfo(adapter3, 0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
    if (adapter3)
        IDXGIAdapter3_Release(adapter3);
    if (adapter)
        IDXGIAdapter1_Release(adapter);
    if (factory)
        IDXGIFactory1_Release(factory);
    return info.Budget >> 20u;
}

/* ---- SKSE messaging stand-in ---- */

static EventCallback g_listener;

static bool msg_register(unsigned listener, const char *sender, EventCallback handler)
{
    printf("listener %u for %s\n", listener, sender ? sender : "(any)");
    if (sender && strcmp(sender, "SKSE") == 0)
        g_listener = handler;
    return true;
}

static bool msg_dispatch(unsigned sender, unsigned type, void *data, unsigned len, const char *receiver)
{
    printf("dispatch from %u type 0x%08x len %u to %s\n", sender, type, len, receiver ? receiver : "(all)");
    if (receiver && strcmp(receiver, "devbench") == 0 && type == 0x9a3f1c08 && data) {
        *(void *(**)(unsigned))data = host_get_api;
        return true;
    }
    return false;
}

static MessagingInterface g_messaging = {2, msg_register, msg_dispatch};

static void *query_interface(unsigned id)
{
    return id == 5 ? &g_messaging : NULL;
}

static unsigned plugin_handle(void)
{
    return 7;
}

int main(void)
{
    SetEnvironmentVariableA("DXVK_CONFIG", "dxgi.syncInterval=1");
    HMODULE m = LoadLibraryA("MGOBudgetPlugin.dll");
    if (!m) {
        printf("FAIL LoadLibrary %lu\n", GetLastError());
        return 1;
    }
    QueryFn query = (QueryFn)(void *)GetProcAddress(m, "SKSEPlugin_Query");
    LoadFn load = (LoadFn)(void *)GetProcAddress(m, "SKSEPlugin_Load");
    if (!query || !load) {
        printf("FAIL exports missing\n");
        return 1;
    }
    SKSEInterface skse = {0x0200000C, 0x0104000F, 0, 0, query_interface, plugin_handle, NULL};
    PluginInfo info = {0};
    BOOL q = query(&skse, &info);
    printf("query=%d name=%s version=%u\n", q, info.name ? info.name : "(null)", info.version);
    printf("load=%d\n", load(&skse));
    char env[1024] = "";
    GetEnvironmentVariableA("DXVK_CONFIG", env, sizeof(env));
    printf("DXVK_CONFIG=%s\n", env);

    if (g_listener) {
        SKSEMessage dataLoaded = {"SKSE", 8, 0, NULL};
        g_listener(&dataLoaded);
    }
    call_tool("mgobudget.status", "{}");
    printf("dxgi budget before set: %llu MiB\n", dxgi_budget_mib());
    call_tool("mgobudget.set", "{\"mib\": 20000}");
    printf("dxgi budget after set: %llu MiB\n", dxgi_budget_mib());
    call_tool("mgobudget.set", "{\"mib\": -1}");
    call_tool("mgobudget.set", "{}");
    call_tool("mgobudget.status", "{}");
    return q ? 0 : 1;
}
