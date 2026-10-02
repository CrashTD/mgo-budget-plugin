/* Loads the plugin like SKSE VR does (Query, then Load) and prints DXVK_CONFIG afterwards. */
#include <windows.h>
#include <stdio.h>

typedef struct {
    unsigned skseVersion, runtimeVersion, editorVersion, isEditor;
    void *queryInterface, *getPluginHandle, *getReleaseIndex;
} SKSEInterface;

typedef struct {
    unsigned infoVersion;
    const char *name;
    unsigned version;
} PluginInfo;

typedef BOOL (*QueryFn)(const SKSEInterface *, PluginInfo *);
typedef BOOL (*LoadFn)(const SKSEInterface *);

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
    SKSEInterface skse = {0x0200000C, 0x0104000F, 0, 0, NULL, NULL, NULL};
    PluginInfo info = {0};
    BOOL q = query(&skse, &info);
    printf("query=%d name=%s version=%u\n", q, info.name ? info.name : "(null)", info.version);
    printf("load=%d\n", load(&skse));
    char env[1024] = "";
    GetEnvironmentVariableA("DXVK_CONFIG", env, sizeof(env));
    printf("DXVK_CONFIG=%s\n", env);
    return q ? 0 : 1;
}
