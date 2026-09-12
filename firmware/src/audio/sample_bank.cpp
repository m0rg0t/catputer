#include "lofi/sample_bank.h"

namespace lofi {
namespace {

#include "sample_bank_data.inc"

}  // namespace

const Sample* builtinSamples(std::size_t& count) {
    count = sizeof(kBuiltinSamples) / sizeof(kBuiltinSamples[0]);
    return kBuiltinSamples;
}

const char* builtinSampleBankId() {
    return kBuiltinSampleBankId;
}

}  // namespace lofi
