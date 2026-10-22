# camd: return success after expunge cleanup

CAMD registers the void `UninitCamd()` function as an expunge callback,
but the generated expunge path tests callbacks' integer return values.
A zero result can leave an already cleaned library resident, allowing
cleanup to run again or a subsequent open to use cleared state.

Register the existing `Expunge()` wrapper on AROS too. It performs cleanup
and returns `TRUE`. This changes no public interface.

Validation on hosted x86-64 UP AROS: with corrected netinfo held constant,
the original CAMD blocks at the second open after a flush; the corrected
CAMD completes two open/close/flush cycles. The network MIDI lifecycle test
also unloads and reloads the actual driver successfully under the service.
No native trap or allocator-corruption report remains with both cleanup
fixes installed.

Local branch: `review/camd-expunge-result`, one commit on upstream
`ecdc52080ac8dfc593a27888695b8fba8c836eb2`. No endpoint API dependency.
