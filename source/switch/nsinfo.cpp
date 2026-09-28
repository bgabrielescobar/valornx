#include "nsinfo.hpp"

#include <switch.h>

#include <cstdlib>
#include <cstring>

namespace vnx {

bool loadGameName(uint64_t titleId, std::string& name, std::vector<uint8_t>* iconJpeg) {
    if (R_FAILED(nsInitialize())) return false;
    bool ok = false;
    NsApplicationControlData* data = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
    if (data) {
        u64 size = 0;
        const Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, titleId, data,
                                                      sizeof(NsApplicationControlData), &size);
        if (R_SUCCEEDED(rc) && size >= sizeof(data->nacp)) {
            NacpLanguageEntry* le = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&data->nacp, &le)) && le && le->name[0]) {
                name.assign(le->name, strnlen(le->name, sizeof(le->name)));
                ok = true;
            }
            if (iconJpeg && size > sizeof(data->nacp))
                iconJpeg->assign(data->icon, data->icon + (size_t)(size - sizeof(data->nacp)));
        }
        free(data);
    }
    nsExit();
    return ok;
}

}  // namespace vnx
