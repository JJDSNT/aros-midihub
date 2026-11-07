/* Compile-only canary for the additive CAMD 43 Function Block record.
   Build this against the proposal's installed public headers. */
#include <midi/camd.h>
#include <midi/camdendpoint.h>

#include <stddef.h>

typedef char function_block_public_size_must_be_112[
    sizeof(struct CAMDFunctionBlockInfoV1) == 112 ? 1 : -1];
typedef char function_block_midi_ci_offset_must_be_104[
    offsetof(struct CAMDFunctionBlockInfoV1, MIDICIVersion) == 104 ? 1 : -1];
typedef char function_block_sysex8_offset_must_be_108[
    offsetof(struct CAMDFunctionBlockInfoV1, MaxSysEx8Streams) == 108 ? 1 : -1];

void camd_function_block_public_canary(struct CAMDFunctionBlockInfoV1 *block)
{
    block->MIDICIVersion = 2;
    block->MaxSysEx8Streams = 5;
}
