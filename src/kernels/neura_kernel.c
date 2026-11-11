/* Status texts of the kernel ABI (neura_kernel.h). The messages are the
 * exact texts of the Neura implementations the kernels replace. */
#include "neura_kernel.h"

const char* neura_kernel_status_message(neura_status status) {
    switch (status) {
    case NEURA_OK: return "ok";
    case NEURA_NEED_CAPACITY: return "output buffer too small";
    case NEURA_ERR_INVALID_START_BYTE: return "invalid start byte";
    case NEURA_ERR_INVALID_CONTINUATION_BYTE: return "invalid continuation byte";
    case NEURA_ERR_UNEXPECTED_END_OF_DATA: return "unexpected end of data";
    case NEURA_ERR_INVALID_CHARACTER: return "invalid character";
    case NEURA_ERR_INVALID_PADDING: return "invalid padding";
    case NEURA_ERR_UNEXPECTED_PADDING: return "unexpected padding";
    case NEURA_ERR_NONZERO_TRAILING_BITS: return "nonzero trailing bits";
    case NEURA_ERR_INVALID_HEX_DIGIT: return "invalid hex digit";
    case NEURA_ERR_ODD_HEX_DIGITS: return "odd number of hex digits";
    case NEURA_ERR_PARSE_EMPTY: return "empty input";
    case NEURA_ERR_PARSE_EXPECTED_DIGIT: return "expected a digit";
    case NEURA_ERR_PARSE_INVALID_CHARACTER: return "invalid character";
    case NEURA_ERR_PARSE_OUT_OF_RANGE: return "value out of range for ";
    case NEURA_TRAP_OFFSET_OUT_OF_RANGE: return "invalid argument: offset out of range";
    case NEURA_TRAP_NOT_CHARACTER_BOUNDARY: return "invalid argument: offset is not a character boundary";
    case NEURA_TRAP_EMPTY_SEPARATOR: return "invalid argument: empty separator";
    case NEURA_TRAP_EMPTY_TARGET: return "invalid argument: empty target";
    case NEURA_TRAP_RADIX_OUT_OF_RANGE: return "invalid argument: radix outside 2 through 36";
    case NEURA_TRAP_DIGEST_FINISHED: return "invalid state: digest finished";
    case NEURA_TRAP_INVALID_STATE: return "invalid state: kernel state";
    case NEURA_TRAP_SCRATCH_TOO_SMALL: return "invalid argument: scratch buffer too small";
    case NEURA_TRAP_WORK_EXHAUSTED: return "step limit exceeded";
    }
    return "unknown kernel status";
}

const char* neura_kernel_status_error_kind(neura_status status) {
    switch (status) {
    case NEURA_ERR_INVALID_START_BYTE:
    case NEURA_ERR_INVALID_CONTINUATION_BYTE:
    case NEURA_ERR_UNEXPECTED_END_OF_DATA:
    case NEURA_ERR_INVALID_CHARACTER:
    case NEURA_ERR_INVALID_PADDING:
    case NEURA_ERR_UNEXPECTED_PADDING:
    case NEURA_ERR_NONZERO_TRAILING_BITS:
    case NEURA_ERR_INVALID_HEX_DIGIT:
    case NEURA_ERR_ODD_HEX_DIGITS:
        return "Encoding";
    case NEURA_ERR_PARSE_EMPTY: return "Parse.Empty";
    case NEURA_ERR_PARSE_EXPECTED_DIGIT: return "Parse.UnexpectedEnd";
    case NEURA_ERR_PARSE_INVALID_CHARACTER: return "Parse.InvalidCharacter";
    case NEURA_ERR_PARSE_OUT_OF_RANGE: return "Parse.OutOfRange";
    default: return 0;
    }
}
