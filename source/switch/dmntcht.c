#define NX_SERVICE_ASSUME_NON_DOMAIN
#include "dmntcht.h"

static Service g_dmntchtSrv;
static int g_dmntchtRefCnt;

Result dmntchtInitialize(void) {
    if (g_dmntchtRefCnt > 0) {
        g_dmntchtRefCnt++;
        return 0;
    }
    Result rc = smGetService(&g_dmntchtSrv, "dmnt:cht");
    if (R_SUCCEEDED(rc)) g_dmntchtRefCnt = 1;
    return rc;
}

void dmntchtExit(void) {
    if (g_dmntchtRefCnt > 0 && --g_dmntchtRefCnt == 0) serviceClose(&g_dmntchtSrv);
}

Result dmntchtHasCheatProcess(bool* out) {
    u8 tmp = 0;
    Result rc = serviceDispatchOut(&g_dmntchtSrv, 65000, tmp);
    if (R_SUCCEEDED(rc) && out) *out = tmp & 1;
    return rc;
}

Result dmntchtGetCheatProcessMetadata(DmntCheatProcessMetadata* out_metadata) {
    return serviceDispatchOut(&g_dmntchtSrv, 65002, *out_metadata);
}

Result dmntchtForceOpenCheatProcess(void) { return serviceDispatch(&g_dmntchtSrv, 65003); }

Result dmntchtReadCheatProcessMemory(u64 address, void* buffer, size_t size) {
    const struct {
        u64 address;
        u64 size;
    } in = {address, size};
    return serviceDispatchIn(&g_dmntchtSrv, 65102, in, .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
                             .buffers = {{buffer, size}}, );
}

Result dmntchtWriteCheatProcessMemory(u64 address, const void* buffer, size_t size) {
    const struct {
        u64 address;
        u64 size;
    } in = {address, size};
    return serviceDispatchIn(&g_dmntchtSrv, 65103, in, .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
                             .buffers = {{buffer, size}}, );
}

Result dmntchtQueryCheatProcessMemory(MemoryInfo* mem_info, u64 address) {
    return serviceDispatchInOut(&g_dmntchtSrv, 65104, address, *mem_info);
}

Result dmntchtGetCheatCount(u64* out_count) { return serviceDispatchOut(&g_dmntchtSrv, 65200, *out_count); }

Result dmntchtGetCheats(DmntCheatEntry* buffer, u64 max_count, u64 offset, u64* out_count) {
    return serviceDispatchInOut(&g_dmntchtSrv, 65201, offset, *out_count,
                                .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
                                .buffers = {{buffer, sizeof(*buffer) * max_count}}, );
}

Result dmntchtToggleCheat(u32 cheat_id) { return serviceDispatchIn(&g_dmntchtSrv, 65203, cheat_id); }

Result dmntchtRemoveCheat(u32 cheat_id) { return serviceDispatchIn(&g_dmntchtSrv, 65205, cheat_id); }

Result dmntchtAddCheat(DmntCheatDefinition* cheat_def, bool enabled, u32* out_cheat_id) {
    const u8 in = enabled != 0;
    return serviceDispatchInOut(&g_dmntchtSrv, 65204, in, *out_cheat_id,
                                .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In | SfBufferAttr_FixedSize},
                                .buffers = {{cheat_def, sizeof(*cheat_def)}}, );
}

Result dmntchtGetFrozenAddressCount(u64* out_count) { return serviceDispatchOut(&g_dmntchtSrv, 65300, *out_count); }

Result dmntchtGetFrozenAddresses(DmntFrozenAddressEntry* buffer, u64 max_count, u64 offset, u64* out_count) {
    return serviceDispatchInOut(&g_dmntchtSrv, 65301, offset, *out_count,
                                .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
                                .buffers = {{buffer, sizeof(*buffer) * max_count}}, );
}

Result dmntchtEnableFrozenAddress(u64 address, u64 width, u64* out_value) {
    const struct {
        u64 address;
        u64 width;
    } in = {address, width};
    return serviceDispatchInOut(&g_dmntchtSrv, 65303, in, *out_value);
}

Result dmntchtDisableFrozenAddress(u64 address) { return serviceDispatchIn(&g_dmntchtSrv, 65304, address); }
