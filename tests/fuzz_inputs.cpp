#include "moenotes_score.h"
#include <cstddef>
#include <cstdint>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 65536)
        return 0;
    static constexpr char request[] = "{\"schema_version\":1,\"music_score_id\":1}";
    static constexpr char chart[] = "{\"events\":{},\"notes\":[]}";
    mns_prepared *output = nullptr;
    (void)mns_prepare(reinterpret_cast<const char *>(data), size, chart, sizeof(chart) - 1, "fuzz.json",
                      request, sizeof(request) - 1, &output);
    mns_prepared_free(output);
    (void)mns_prepare("{}", 2, data, size, "fuzz.json", reinterpret_cast<const char *>(data), size, &output);
    mns_prepared_free(output);
    return 0;
}
