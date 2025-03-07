

#include "Python.h"
#include "pycore_call.h"
#include "pycore_frame.h"
#include "pycore_interp.h"
#include "pycore_long.h"
#include "pycore_namespace.h"
#include "pycore_object.h"
#include "pycore_opcode.h"
#include "pycore_pyerrors.h"
#include "pycore_pystate.h"

/* Uncomment this to dump debugging output when assertions fail */
// #define INSTRUMENT_DEBUG 1

static PyObject DISABLE =
{
    _PyObject_IMMORTAL_REFCNT,
    &PyBaseObject_Type
};

PyObject _PyInstrumentation_MISSING =
{
    _PyObject_IMMORTAL_REFCNT,
    &PyBaseObject_Type
};

static const int8_t EVENT_FOR_OPCODE[256] = {
    [RETURN_CONST] = PY_MONITORING_EVENT_PY_RETURN,
    [INSTRUMENTED_RETURN_CONST] = PY_MONITORING_EVENT_PY_RETURN,
    [RETURN_VALUE] = PY_MONITORING_EVENT_PY_RETURN,
    [INSTRUMENTED_RETURN_VALUE] = PY_MONITORING_EVENT_PY_RETURN,
    [CALL] = PY_MONITORING_EVENT_CALL,
    [INSTRUMENTED_CALL] = PY_MONITORING_EVENT_CALL,
    [CALL_FUNCTION_EX] = PY_MONITORING_EVENT_CALL,
    [INSTRUMENTED_CALL_FUNCTION_EX] = PY_MONITORING_EVENT_CALL,
    [RESUME] = -1,
    [YIELD_VALUE] = PY_MONITORING_EVENT_PY_YIELD,
    [INSTRUMENTED_YIELD_VALUE] = PY_MONITORING_EVENT_PY_YIELD,
    [JUMP_FORWARD] = PY_MONITORING_EVENT_JUMP,
    [JUMP_BACKWARD] = PY_MONITORING_EVENT_JUMP,
    [POP_JUMP_IF_FALSE] = PY_MONITORING_EVENT_BRANCH,
    [POP_JUMP_IF_TRUE] = PY_MONITORING_EVENT_BRANCH,
    [POP_JUMP_IF_NONE] = PY_MONITORING_EVENT_BRANCH,
    [POP_JUMP_IF_NOT_NONE] = PY_MONITORING_EVENT_BRANCH,
    [INSTRUMENTED_JUMP_FORWARD] = PY_MONITORING_EVENT_JUMP,
    [INSTRUMENTED_JUMP_BACKWARD] = PY_MONITORING_EVENT_JUMP,
    [INSTRUMENTED_POP_JUMP_IF_FALSE] = PY_MONITORING_EVENT_BRANCH,
    [INSTRUMENTED_POP_JUMP_IF_TRUE] = PY_MONITORING_EVENT_BRANCH,
    [INSTRUMENTED_POP_JUMP_IF_NONE] = PY_MONITORING_EVENT_BRANCH,
    [INSTRUMENTED_POP_JUMP_IF_NOT_NONE] = PY_MONITORING_EVENT_BRANCH,
    [FOR_ITER] = PY_MONITORING_EVENT_BRANCH,
    [INSTRUMENTED_FOR_ITER] = PY_MONITORING_EVENT_BRANCH,
    [END_FOR] = PY_MONITORING_EVENT_STOP_ITERATION,
    [INSTRUMENTED_END_FOR] = PY_MONITORING_EVENT_STOP_ITERATION,
    [END_SEND] = PY_MONITORING_EVENT_STOP_ITERATION,
    [INSTRUMENTED_END_SEND] = PY_MONITORING_EVENT_STOP_ITERATION,
};

static const uint8_t DE_INSTRUMENT[256] = {
    [INSTRUMENTED_RESUME] = RESUME,
    [INSTRUMENTED_RETURN_VALUE] = RETURN_VALUE,
    [INSTRUMENTED_RETURN_CONST] = RETURN_CONST,
    [INSTRUMENTED_CALL] = CALL,
    [INSTRUMENTED_CALL_FUNCTION_EX] = CALL_FUNCTION_EX,
    [INSTRUMENTED_YIELD_VALUE] = YIELD_VALUE,
    [INSTRUMENTED_JUMP_FORWARD] = JUMP_FORWARD,
    [INSTRUMENTED_JUMP_BACKWARD] = JUMP_BACKWARD,
    [INSTRUMENTED_POP_JUMP_IF_FALSE] = POP_JUMP_IF_FALSE,
    [INSTRUMENTED_POP_JUMP_IF_TRUE] = POP_JUMP_IF_TRUE,
    [INSTRUMENTED_POP_JUMP_IF_NONE] = POP_JUMP_IF_NONE,
    [INSTRUMENTED_POP_JUMP_IF_NOT_NONE] = POP_JUMP_IF_NOT_NONE,
    [INSTRUMENTED_FOR_ITER] = FOR_ITER,
    [INSTRUMENTED_END_FOR] = END_FOR,
    [INSTRUMENTED_END_SEND] = END_SEND,
};

static const uint8_t INSTRUMENTED_OPCODES[256] = {
    [RETURN_CONST] = INSTRUMENTED_RETURN_CONST,
    [INSTRUMENTED_RETURN_CONST] = INSTRUMENTED_RETURN_CONST,
    [RETURN_VALUE] = INSTRUMENTED_RETURN_VALUE,
    [INSTRUMENTED_RETURN_VALUE] = INSTRUMENTED_RETURN_VALUE,
    [CALL] = INSTRUMENTED_CALL,
    [INSTRUMENTED_CALL] = INSTRUMENTED_CALL,
    [CALL_FUNCTION_EX] = INSTRUMENTED_CALL_FUNCTION_EX,
    [INSTRUMENTED_CALL_FUNCTION_EX] = INSTRUMENTED_CALL_FUNCTION_EX,
    [YIELD_VALUE] = INSTRUMENTED_YIELD_VALUE,
    [INSTRUMENTED_YIELD_VALUE] = INSTRUMENTED_YIELD_VALUE,
    [RESUME] = INSTRUMENTED_RESUME,
    [INSTRUMENTED_RESUME] = INSTRUMENTED_RESUME,
    [JUMP_FORWARD] = INSTRUMENTED_JUMP_FORWARD,
    [INSTRUMENTED_JUMP_FORWARD] = INSTRUMENTED_JUMP_FORWARD,
    [JUMP_BACKWARD] = INSTRUMENTED_JUMP_BACKWARD,
    [INSTRUMENTED_JUMP_BACKWARD] = INSTRUMENTED_JUMP_BACKWARD,
    [POP_JUMP_IF_FALSE] = INSTRUMENTED_POP_JUMP_IF_FALSE,
    [INSTRUMENTED_POP_JUMP_IF_FALSE] = INSTRUMENTED_POP_JUMP_IF_FALSE,
    [POP_JUMP_IF_TRUE] = INSTRUMENTED_POP_JUMP_IF_TRUE,
    [INSTRUMENTED_POP_JUMP_IF_TRUE] = INSTRUMENTED_POP_JUMP_IF_TRUE,
    [POP_JUMP_IF_NONE] = INSTRUMENTED_POP_JUMP_IF_NONE,
    [INSTRUMENTED_POP_JUMP_IF_NONE] = INSTRUMENTED_POP_JUMP_IF_NONE,
    [POP_JUMP_IF_NOT_NONE] = INSTRUMENTED_POP_JUMP_IF_NOT_NONE,
    [INSTRUMENTED_POP_JUMP_IF_NOT_NONE] = INSTRUMENTED_POP_JUMP_IF_NOT_NONE,
    [END_FOR] = INSTRUMENTED_END_FOR,
    [INSTRUMENTED_END_FOR] = INSTRUMENTED_END_FOR,
    [END_SEND] = INSTRUMENTED_END_SEND,
    [INSTRUMENTED_END_SEND] = INSTRUMENTED_END_SEND,

    [INSTRUMENTED_LINE] = INSTRUMENTED_LINE,
    [INSTRUMENTED_INSTRUCTION] = INSTRUMENTED_INSTRUCTION,
};

static inline bool
opcode_has_event(int opcode) {
    return opcode < INSTRUMENTED_LINE &&
        INSTRUMENTED_OPCODES[opcode] > 0;
}

static inline bool
is_instrumented(int opcode) {
    assert(opcode != 0);
    assert(opcode != RESERVED);
    return opcode >= MIN_INSTRUMENTED_OPCODE;
}

static inline bool
monitors_equals(_Py_Monitors a, _Py_Monitors b)
{
    for (int i = 0; i < PY_MONITORING_UNGROUPED_EVENTS; i++) {
        if (a.tools[i] != b.tools[i]) {
            return false;
        }
    }
    return true;
}

static inline _Py_Monitors
monitors_sub(_Py_Monitors a, _Py_Monitors b)
{
    _Py_Monitors res;
    for (int i = 0; i < PY_MONITORING_UNGROUPED_EVENTS; i++) {
        res.tools[i] = a.tools[i] & ~b.tools[i];
    }
    return res;
}

static inline _Py_Monitors
monitors_and(_Py_Monitors a, _Py_Monitors b)
{
    _Py_Monitors res;
    for (int i = 0; i < PY_MONITORING_UNGROUPED_EVENTS; i++) {
        res.tools[i] = a.tools[i] & b.tools[i];
    }
    return res;
}

static inline _Py_Monitors
monitors_or(_Py_Monitors a, _Py_Monitors b)
{
    _Py_Monitors res;
    for (int i = 0; i < PY_MONITORING_UNGROUPED_EVENTS; i++) {
        res.tools[i] = a.tools[i] | b.tools[i];
    }
    return res;
}

static inline bool
monitors_are_empty(_Py_Monitors m)
{
    for (int i = 0; i < PY_MONITORING_UNGROUPED_EVENTS; i++) {
        if (m.tools[i]) {
            return false;
        }
    }
    return true;
}

static inline bool
multiple_tools(_Py_Monitors *m)
{
    for (int i = 0; i < PY_MONITORING_UNGROUPED_EVENTS; i++) {
        if (_Py_popcount32(m->tools[i]) > 1) {
            return true;
        }
    }
    return false;
}

static inline _PyMonitoringEventSet
get_events(_Py_Monitors *m, int tool_id)
{
    _PyMonitoringEventSet result = 0;
    for (int e = 0; e < PY_MONITORING_UNGROUPED_EVENTS; e++) {
        if ((m->tools[e] >> tool_id) & 1) {
            result |= (1 << e);
        }
    }
    return result;
}

/* Line delta.
 * 8 bit value.
 * if line_delta == -128:
 *     line = None # represented as -1
 * elif line_delta == -127:
 *     line = PyCode_Addr2Line(code, offset * sizeof(_Py_CODEUNIT));
 * else:
 *     line = first_line  + (offset >> OFFSET_SHIFT) + line_delta;
 */

#define NO_LINE -128
#define COMPUTED_LINE -127

#define OFFSET_SHIFT 4

static int8_t
compute_line_delta(PyCodeObject *code, int offset, int line)
{
    if (line < 0) {
        return NO_LINE;
    }
    int delta = line - code->co_firstlineno - (offset >> OFFSET_SHIFT);
    if (delta <= INT8_MAX && delta > COMPUTED_LINE) {
        return delta;
    }
    return COMPUTED_LINE;
}

static int
compute_line(PyCodeObject *code, int offset, int8_t line_delta)
{
    if (line_delta > COMPUTED_LINE) {
        return code->co_firstlineno + (offset >> OFFSET_SHIFT) + line_delta;
    }
    if (line_delta == NO_LINE) {

        return -1;
    }
    assert(line_delta == COMPUTED_LINE);
    /* Look it up */
    return PyCode_Addr2Line(code, offset * sizeof(_Py_CODEUNIT));
}

static int
instruction_length(PyCodeObject *code, int offset)
{
    int opcode = _PyCode_CODE(code)[offset].op.code;
    assert(opcode != 0);
    assert(opcode != RESERVED);
    if (opcode == INSTRUMENTED_LINE) {
        opcode = code->_co_monitoring->lines[offset].original_opcode;
    }
    if (opcode == INSTRUMENTED_INSTRUCTION) {
        opcode = code->_co_monitoring->per_instruction_opcodes[offset];
    }
    int deinstrumented = DE_INSTRUMENT[opcode];
    if (deinstrumented) {
        opcode = deinstrumented;
    }
    else {
        opcode = _PyOpcode_Deopt[opcode];
    }
    assert(opcode != 0);
    assert(!is_instrumented(opcode));
    assert(opcode == _PyOpcode_Deopt[opcode]);
    return 1 + _PyOpcode_Caches[opcode];
}

#ifdef INSTRUMENT_DEBUG

static void
dump_instrumentation_data_tools(PyCodeObject *code, uint8_t *tools, int i, FILE*out)
{
    if (tools == NULL) {
        fprintf(out, "tools = NULL");
    }
    else {
        fprintf(out, "tools = %d", tools[i]);
    }
}

static void
dump_instrumentation_data_lines(PyCodeObject *code, _PyCoLineInstrumentationData *lines, int i, FILE*out)
{
    if (lines == NULL) {
        fprintf(out, ", lines = NULL");
    }
    else if (lines[i].original_opcode == 0) {
        fprintf(out, ", lines = {original_opcode = No LINE (0), line_delta = %d)", lines[i].line_delta);
    }
    else {
        fprintf(out, ", lines = {original_opcode = %s, line_delta = %d)", _PyOpcode_OpName[lines[i].original_opcode], lines[i].line_delta);
    }
}

static void
dump_instrumentation_data_line_tools(PyCodeObject *code, uint8_t *line_tools, int i, FILE*out)
{
    if (line_tools == NULL) {
        fprintf(out, ", line_tools = NULL");
    }
    else {
        fprintf(out, ", line_tools = %d", line_tools[i]);
    }
}

static void
dump_instrumentation_data_per_instruction(PyCodeObject *code, _PyCoMonitoringData *data, int i, FILE*out)
{
    if (data->per_instruction_opcodes == NULL) {
        fprintf(out, ", per-inst opcode = NULL");
    }
    else {
        fprintf(out, ", per-inst opcode = %s", _PyOpcode_OpName[data->per_instruction_opcodes[i]]);
    }
    if (data->per_instruction_tools == NULL) {
        fprintf(out, ", per-inst tools = NULL");
    }
    else {
        fprintf(out, ", per-inst tools = %d", data->per_instruction_tools[i]);
    }
}

static void
dump_monitors(const char *prefix, _Py_Monitors monitors, FILE*out)
{
    fprintf(out, "%s monitors:\n", prefix);
    for (int event = 0; event < PY_MONITORING_UNGROUPED_EVENTS; event++) {
        fprintf(out, "    Event %d: Tools %x\n", event, monitors.tools[event]);
    }
}

/* Like _Py_GetBaseOpcode but without asserts.
 * Does its best to give the right answer, but won't abort
 * if something is wrong */
int get_base_opcode_best_attempt(PyCodeObject *code, int offset)
{
    int opcode = _Py_OPCODE(_PyCode_CODE(code)[offset]);
    if (INSTRUMENTED_OPCODES[opcode] != opcode) {
        /* Not instrumented */
        return _PyOpcode_Deopt[opcode] == 0 ? opcode : _PyOpcode_Deopt[opcode];
    }
    if (opcode == INSTRUMENTED_INSTRUCTION) {
        if (code->_co_monitoring->per_instruction_opcodes[offset] == 0) {
            return opcode;
        }
        opcode = code->_co_monitoring->per_instruction_opcodes[offset];
    }
    if (opcode == INSTRUMENTED_LINE) {
        if (code->_co_monitoring->lines[offset].original_opcode == 0) {
            return opcode;
        }
        opcode = code->_co_monitoring->lines[offset].original_opcode;
    }
    int deinstrumented = DE_INSTRUMENT[opcode];
    if (deinstrumented) {
        return deinstrumented;
    }
    if (_PyOpcode_Deopt[opcode] == 0) {
        return opcode;
    }
    return _PyOpcode_Deopt[opcode];
}

/* No error checking -- Don't use this for anything but experimental debugging */
static void
dump_instrumentation_data(PyCodeObject *code, int star, FILE*out)
{
    _PyCoMonitoringData *data = code->_co_monitoring;
    fprintf(out, "\n");
    PyObject_Print(code->co_name, out, Py_PRINT_RAW);
    fprintf(out, "\n");
    if (data == NULL) {
        fprintf(out, "NULL\n");
        return;
    }
    dump_monitors("Global", PyInterpreterState_Get()->monitors, out);
    dump_monitors("Code", data->local_monitors, out);
    dump_monitors("Active", data->active_monitors, out);
    int code_len = (int)Py_SIZE(code);
    bool starred = false;
    for (int i = 0; i < code_len; i += instruction_length(code, i)) {
        _Py_CODEUNIT *instr = &_PyCode_CODE(code)[i];
        int opcode = instr->op.code;
        if (i == star) {
            fprintf(out, "**  ");
            starred = true;
        }
        fprintf(out, "Offset: %d, line: %d %s: ", i, PyCode_Addr2Line(code, i*2), _PyOpcode_OpName[opcode]);
        dump_instrumentation_data_tools(code, data->tools, i, out);
        dump_instrumentation_data_lines(code, data->lines, i, out);
        dump_instrumentation_data_line_tools(code, data->line_tools, i, out);
        dump_instrumentation_data_per_instruction(code, data, i, out);
        fprintf(out, "\n");
        ;
    }
    if (!starred && star >= 0) {
        fprintf(out, "Error offset not at valid instruction offset: %d\n", star);
        fprintf(out, "    ");
        dump_instrumentation_data_tools(code, data->tools, star, out);
        dump_instrumentation_data_lines(code, data->lines, star, out);
        dump_instrumentation_data_line_tools(code, data->line_tools, star, out);
        dump_instrumentation_data_per_instruction(code, data, star, out);
        fprintf(out, "\n");
    }
}

#define CHECK(test) do { \
    if (!(test)) { \
        dump_instrumentation_data(code, i, stderr); \
    } \
    assert(test); \
} while (0)

bool valid_opcode(int opcode) {
    if (opcode > 0 &&
        opcode != RESERVED &&
        opcode < 255 &&
        _PyOpcode_OpName[opcode] &&
        _PyOpcode_OpName[opcode][0] != '<'
    ) {
       return true;
    }
    return false;
}

static void
sanity_check_instrumentation(PyCodeObject *code)
{
    _PyCoMonitoringData *data = code->_co_monitoring;
    if (data == NULL) {
        return;
    }
    _Py_Monitors active_monitors = PyInterpreterState_Get()->monitors;
    if (code->_co_monitoring) {
        _Py_Monitors local_monitors = code->_co_monitoring->local_monitors;
        active_monitors = monitors_or(active_monitors, local_monitors);
    }
    assert(monitors_equals(
        code->_co_monitoring->active_monitors,
        active_monitors)
    );
    int code_len = (int)Py_SIZE(code);
    for (int i = 0; i < code_len;) {
        int opcode = _PyCode_CODE(code)[i].op.code;
        int base_opcode = _Py_GetBaseOpcode(code, i);
        CHECK(valid_opcode(opcode));
        CHECK(valid_opcode(base_opcode));
        if (opcode == INSTRUMENTED_INSTRUCTION) {
            opcode = data->per_instruction_opcodes[i];
            if (!is_instrumented(opcode)) {
                CHECK(_PyOpcode_Deopt[opcode] == opcode);
            }
            if (data->per_instruction_tools) {
                uint8_t tools = active_monitors.tools[PY_MONITORING_EVENT_INSTRUCTION];
                CHECK((tools & data->per_instruction_tools[i]) == data->per_instruction_tools[i]);
            }
        }
        if (opcode == INSTRUMENTED_LINE) {
            CHECK(data->lines);
            CHECK(valid_opcode(data->lines[i].original_opcode));
            opcode = data->lines[i].original_opcode;
            CHECK(opcode != END_FOR);
            CHECK(opcode != RESUME);
            CHECK(opcode != INSTRUMENTED_RESUME);
            if (!is_instrumented(opcode)) {
                CHECK(_PyOpcode_Deopt[opcode] == opcode);
            }
            CHECK(opcode != INSTRUMENTED_LINE);
        }
        else if (data->lines && !is_instrumented(opcode)) {
            CHECK(data->lines[i].original_opcode == 0 ||
                  data->lines[i].original_opcode == base_opcode ||
                  DE_INSTRUMENT[data->lines[i].original_opcode] == base_opcode);
        }
        if (is_instrumented(opcode)) {
            CHECK(DE_INSTRUMENT[opcode] == base_opcode);
            int event = EVENT_FOR_OPCODE[DE_INSTRUMENT[opcode]];
            if (event < 0) {
                /* RESUME fixup */
                event = _PyCode_CODE(code)[i].op.arg;
            }
            CHECK(active_monitors.tools[event] != 0);
        }
        if (data->lines && base_opcode != END_FOR) {
            int line1 = compute_line(code, i, data->lines[i].line_delta);
            int line2 = PyCode_Addr2Line(code, i*sizeof(_Py_CODEUNIT));
            CHECK(line1 == line2);
        }
        CHECK(valid_opcode(opcode));
        if (data->tools) {
            uint8_t local_tools = data->tools[i];
            if (opcode_has_event(base_opcode)) {
                int event = EVENT_FOR_OPCODE[base_opcode];
                if (event == -1) {
                    /* RESUME fixup */
                    event = _PyCode_CODE(code)[i].op.arg;
                }
                CHECK((active_monitors.tools[event] & local_tools) == local_tools);
            }
            else {
                CHECK(local_tools == 0xff);
            }
        }
        i += instruction_length(code, i);
        assert(i <= code_len);
    }
}
#else

#define CHECK(test) assert(test)

#endif

/* Get the underlying opcode, stripping instrumentation */
int _Py_GetBaseOpcode(PyCodeObject *code, int i)
{
    int opcode = _PyCode_CODE(code)[i].op.code;
    if (opcode == INSTRUMENTED_LINE) {
        opcode = code->_co_monitoring->lines[i].original_opcode;
    }
    if (opcode == INSTRUMENTED_INSTRUCTION) {
        opcode = code->_co_monitoring->per_instruction_opcodes[i];
    }
    CHECK(opcode != INSTRUMENTED_INSTRUCTION);
    CHECK(opcode != INSTRUMENTED_LINE);
    int deinstrumented = DE_INSTRUMENT[opcode];
    if (deinstrumented) {
        return deinstrumented;
    }
    return _PyOpcode_Deopt[opcode];
}

static void
de_instrument(PyCodeObject *code, int i, int event)
{
    assert(event != PY_MONITORING_EVENT_INSTRUCTION);
    assert(event != PY_MONITORING_EVENT_LINE);

    _Py_CODEUNIT *instr = &_PyCode_CODE(code)[i];
    uint8_t *opcode_ptr = &instr->op.code;
    int opcode = *opcode_ptr;
    if (opcode == INSTRUMENTED_LINE) {
        opcode_ptr = &code->_co_monitoring->lines[i].original_opcode;
        opcode = *opcode_ptr;
    }
     if (opcode == INSTRUMENTED_INSTRUCTION) {
        opcode_ptr = &code->_co_monitoring->per_instruction_opcodes[i];
        opcode = *opcode_ptr;
    }
   int deinstrumented = DE_INSTRUMENT[opcode];
    if (deinstrumented == 0) {
        return;
    }
    CHECK(_PyOpcode_Deopt[deinstrumented] == deinstrumented);
    *opcode_ptr = deinstrumented;
    if (_PyOpcode_Caches[deinstrumented]) {
        instr[1].cache = adaptive_counter_warmup();
    }
}

static void
de_instrument_line(PyCodeObject *code, int i)
{
    _Py_CODEUNIT *instr = &_PyCode_CODE(code)[i];
    uint8_t *opcode_ptr = &instr->op.code;
    int opcode =*opcode_ptr;
    if (opcode != INSTRUMENTED_LINE) {
        return;
    }
    _PyCoLineInstrumentationData *lines = &code->_co_monitoring->lines[i];
    int original_opcode = lines->original_opcode;
    CHECK(original_opcode != 0);
    CHECK(original_opcode == _PyOpcode_Deopt[original_opcode]);
    *opcode_ptr = instr->op.code = original_opcode;
    if (_PyOpcode_Caches[original_opcode]) {
        instr[1].cache = adaptive_counter_warmup();
    }
    assert(*opcode_ptr != INSTRUMENTED_LINE);
    assert(instr->op.code != INSTRUMENTED_LINE);
}


static void
de_instrument_per_instruction(PyCodeObject *code, int i)
{
    _Py_CODEUNIT *instr = &_PyCode_CODE(code)[i];
    uint8_t *opcode_ptr = &instr->op.code;
    int opcode =*opcode_ptr;
    if (opcode == INSTRUMENTED_LINE) {
        opcode_ptr = &code->_co_monitoring->lines[i].original_opcode;
        opcode = *opcode_ptr;
    }
    if (opcode != INSTRUMENTED_INSTRUCTION) {
        return;
    }
    int original_opcode = code->_co_monitoring->per_instruction_opcodes[i];
    CHECK(original_opcode != 0);
    CHECK(original_opcode == _PyOpcode_Deopt[original_opcode]);
    instr->op.code = original_opcode;
    if (_PyOpcode_Caches[original_opcode]) {
        instr[1].cache = adaptive_counter_warmup();
    }
    assert(instr->op.code != INSTRUMENTED_INSTRUCTION);
    /* Keep things clean for sanity check */
    code->_co_monitoring->per_instruction_opcodes[i] = 0;
}


static void
instrument(PyCodeObject *code, int i)
{
    _Py_CODEUNIT *instr = &_PyCode_CODE(code)[i];
    uint8_t *opcode_ptr = &instr->op.code;
    int opcode =*opcode_ptr;
    if (opcode == INSTRUMENTED_LINE) {
        _PyCoLineInstrumentationData *lines = &code->_co_monitoring->lines[i];
        opcode_ptr = &lines->original_opcode;
        opcode = *opcode_ptr;
    }
    if (opcode == INSTRUMENTED_INSTRUCTION) {
        opcode_ptr = &code->_co_monitoring->per_instruction_opcodes[i];
        opcode = *opcode_ptr;
        CHECK(!is_instrumented(opcode));
        CHECK(opcode == _PyOpcode_Deopt[opcode]);
    }
    CHECK(opcode != 0);
    if (!is_instrumented(opcode)) {
        int deopt = _PyOpcode_Deopt[opcode];
        int instrumented = INSTRUMENTED_OPCODES[deopt];
        assert(instrumented);
        *opcode_ptr = instrumented;
        if (_PyOpcode_Caches[deopt]) {
            instr[1].cache = adaptive_counter_warmup();
        }
    }
}

static void
instrument_line(PyCodeObject *code, int i)
{
    uint8_t *opcode_ptr = &_PyCode_CODE(code)[i].op.code;
    int opcode =*opcode_ptr;
    if (opcode == INSTRUMENTED_LINE) {
        return;
    }
    _PyCoLineInstrumentationData *lines = &code->_co_monitoring->lines[i];
    lines->original_opcode = _PyOpcode_Deopt[opcode];
    CHECK(lines->original_opcode > 0);
    *opcode_ptr = INSTRUMENTED_LINE;
}

static void
instrument_per_instruction(PyCodeObject *code, int i)
{
    _Py_CODEUNIT *instr = &_PyCode_CODE(code)[i];
    uint8_t *opcode_ptr = &instr->op.code;
    int opcode =*opcode_ptr;
    if (opcode == INSTRUMENTED_LINE) {
        _PyCoLineInstrumentationData *lines = &code->_co_monitoring->lines[i];
        opcode_ptr = &lines->original_opcode;
        opcode = *opcode_ptr;
    }
    if (opcode == INSTRUMENTED_INSTRUCTION) {
        return;
    }
    CHECK(opcode != 0);
    if (is_instrumented(opcode)) {
        code->_co_monitoring->per_instruction_opcodes[i] = opcode;
    }
    else {
        assert(opcode != 0);
        assert(_PyOpcode_Deopt[opcode] != 0);
        assert(_PyOpcode_Deopt[opcode] != RESUME);
        code->_co_monitoring->per_instruction_opcodes[i] = _PyOpcode_Deopt[opcode];
    }
    assert(code->_co_monitoring->per_instruction_opcodes[i] > 0);
    *opcode_ptr = INSTRUMENTED_INSTRUCTION;
}

#ifndef NDEBUG
static bool
instruction_has_event(PyCodeObject *code, int offset)
{
    _Py_CODEUNIT instr = _PyCode_CODE(code)[offset];
    int opcode = instr.op.code;
    if (opcode == INSTRUMENTED_LINE) {
        opcode = code->_co_monitoring->lines[offset].original_opcode;
    }
    if (opcode == INSTRUMENTED_INSTRUCTION) {
        opcode = code->_co_monitoring->per_instruction_opcodes[offset];
    }
    return opcode_has_event(opcode);
}
#endif

static void
remove_tools(PyCodeObject * code, int offset, int event, int tools)
{
    assert(event != PY_MONITORING_EVENT_LINE);
    assert(event != PY_MONITORING_EVENT_INSTRUCTION);
    assert(event < PY_MONITORING_INSTRUMENTED_EVENTS);
    assert(instruction_has_event(code, offset));
    _PyCoMonitoringData *monitoring = code->_co_monitoring;
    if (monitoring && monitoring->tools) {
        monitoring->tools[offset] &= ~tools;
        if (monitoring->tools[offset] == 0) {
            de_instrument(code, offset, event);
        }
    }
    else {
        /* Single tool */
        uint8_t single_tool = code->_co_monitoring->active_monitors.tools[event];
        assert(_Py_popcount32(single_tool) <= 1);
        if (((single_tool & tools) == single_tool)) {
            de_instrument(code, offset, event);
        }
    }
}

#ifndef NDEBUG
static bool
tools_is_subset_for_event(PyCodeObject * code, int event, int tools)
{
    int global_tools = PyInterpreterState_Get()->monitors.tools[event];
    int local_tools = code->_co_monitoring->local_monitors.tools[event];
    return tools == ((global_tools | local_tools) & tools);
}
#endif

static void
remove_line_tools(PyCodeObject * code, int offset, int tools)
{
    assert(code->_co_monitoring);
    if (code->_co_monitoring->line_tools)
    {
        uint8_t *toolsptr = &code->_co_monitoring->line_tools[offset];
        *toolsptr &= ~tools;
        if (*toolsptr == 0 ) {
            de_instrument_line(code, offset);
        }
    }
    else {
        /* Single tool */
        uint8_t single_tool = code->_co_monitoring->active_monitors.tools[PY_MONITORING_EVENT_LINE];
        assert(_Py_popcount32(single_tool) <= 1);
        if (((single_tool & tools) == single_tool)) {
            de_instrument_line(code, offset);
        }
    }
}

static void
add_tools(PyCodeObject * code, int offset, int event, int tools)
{
    assert(event != PY_MONITORING_EVENT_LINE);
    assert(event != PY_MONITORING_EVENT_INSTRUCTION);
    assert(event < PY_MONITORING_INSTRUMENTED_EVENTS);
    assert(code->_co_monitoring);
    if (code->_co_monitoring &&
        code->_co_monitoring->tools
    ) {
        code->_co_monitoring->tools[offset] |= tools;
    }
    else {
        /* Single tool */
        assert(_Py_popcount32(tools) == 1);
        assert(tools_is_subset_for_event(code, event, tools));
    }
    instrument(code, offset);
}

static void
add_line_tools(PyCodeObject * code, int offset, int tools)
{
    assert(tools_is_subset_for_event(code, PY_MONITORING_EVENT_LINE, tools));
    assert(code->_co_monitoring);
    if (code->_co_monitoring->line_tools
    ) {
        code->_co_monitoring->line_tools[offset] |= tools;
    }
    else {
        /* Single tool */
        assert(_Py_popcount32(tools) == 1);
    }
    instrument_line(code, offset);
}


static void
add_per_instruction_tools(PyCodeObject * code, int offset, int tools)
{
    assert(tools_is_subset_for_event(code, PY_MONITORING_EVENT_INSTRUCTION, tools));
    assert(code->_co_monitoring);
    if (code->_co_monitoring->per_instruction_tools
    ) {
        code->_co_monitoring->per_instruction_tools[offset] |= tools;
    }
    else {
        /* Single tool */
        assert(_Py_popcount32(tools) == 1);
    }
    instrument_per_instruction(code, offset);
}


static void
remove_per_instruction_tools(PyCodeObject * code, int offset, int tools)
{
    assert(code->_co_monitoring);
    if (code->_co_monitoring->per_instruction_tools)
    {
        uint8_t *toolsptr = &code->_co_monitoring->per_instruction_tools[offset];
        *toolsptr &= ~tools;
        if (*toolsptr == 0 ) {
            de_instrument_per_instruction(code, offset);
        }
    }
    else {
        /* Single tool */
        uint8_t single_tool = code->_co_monitoring->active_monitors.tools[PY_MONITORING_EVENT_INSTRUCTION];
        assert(_Py_popcount32(single_tool) <= 1);
        if (((single_tool & tools) == single_tool)) {
            de_instrument_per_instruction(code, offset);
        }
    }
}


/* Return 1 if DISABLE returned, -1 if error, 0 otherwise */
static int
call_one_instrument(
    PyInterpreterState *interp, PyThreadState *tstate, PyObject **args,
    Py_ssize_t nargsf, int8_t tool, int event)
{
    assert(0 <= tool && tool < 8);
    assert(tstate->tracing == 0);
    PyObject *instrument = interp->monitoring_callables[tool][event];
    if (instrument == NULL) {
       return 0;
    }
    int old_what = tstate->what_event;
    tstate->what_event = event;
    tstate->tracing++;
    PyObject *res = _PyObject_VectorcallTstate(tstate, instrument, args, nargsf, NULL);
    tstate->tracing--;
    tstate->what_event = old_what;
    if (res == NULL) {
        return -1;
    }
    Py_DECREF(res);
    return (res == &DISABLE);
}

static const int8_t MOST_SIGNIFICANT_BITS[16] = {
    -1, 0, 1, 1,
    2, 2, 2, 2,
    3, 3, 3, 3,
    3, 3, 3, 3,
};

/* We could use _Py_bit_length here, but that is designed for larger (32/64) bit ints,
  and can perform relatively poorly on platforms without the necessary intrinsics. */
static inline int most_significant_bit(uint8_t bits) {
    assert(bits != 0);
    if (bits > 15) {
        return MOST_SIGNIFICANT_BITS[bits>>4]+4;
    }
    else {
        return MOST_SIGNIFICANT_BITS[bits];
    }
}

static bool
is_version_up_to_date(PyCodeObject *code, PyInterpreterState *interp)
{
    return interp->monitoring_version == code->_co_instrumentation_version;
}

#ifndef NDEBUG
static bool
instrumentation_cross_checks(PyInterpreterState *interp, PyCodeObject *code)
{
    _Py_Monitors expected = monitors_or(
        interp->monitors,
        code->_co_monitoring->local_monitors);
    return monitors_equals(code->_co_monitoring->active_monitors, expected);
}
#endif

static inline uint8_t
get_tools_for_instruction(PyCodeObject * code, int i, int event)
{
    uint8_t tools;
    assert(event != PY_MONITORING_EVENT_LINE);
    assert(event != PY_MONITORING_EVENT_INSTRUCTION);
    assert(instrumentation_cross_checks(PyThreadState_GET()->interp, code));
    _PyCoMonitoringData *monitoring = code->_co_monitoring;
    if (event >= PY_MONITORING_UNGROUPED_EVENTS) {
        assert(event == PY_MONITORING_EVENT_C_RAISE ||
                event == PY_MONITORING_EVENT_C_RETURN);
        event = PY_MONITORING_EVENT_CALL;
    }
    if (event < PY_MONITORING_INSTRUMENTED_EVENTS && monitoring->tools) {
        tools = monitoring->tools[i];
    }
    else {
        tools = code->_co_monitoring->active_monitors.tools[event];
    }
    CHECK(tools_is_subset_for_event(code, event, tools));
    CHECK((tools & code->_co_monitoring->active_monitors.tools[event]) == tools);
    return tools;
}

static int
call_instrumentation_vector(
    PyThreadState *tstate, int event,
    _PyInterpreterFrame *frame, _Py_CODEUNIT *instr, Py_ssize_t nargs, PyObject *args[])
{
    if (tstate->tracing) {
        return 0;
    }
    assert(!_PyErr_Occurred(tstate));
    assert(args[0] == NULL);
    PyCodeObject *code = frame->f_code;
    assert(code->_co_instrumentation_version == tstate->interp->monitoring_version);
    assert(is_version_up_to_date(code, tstate->interp));
    assert(instrumentation_cross_checks(tstate->interp, code));
    assert(args[1] == NULL);
    args[1] = (PyObject *)code;
    int offset = (int)(instr - _PyCode_CODE(code));
    /* Offset visible to user should be the offset in bytes, as that is the
     * convention for APIs involving code offsets. */
    int bytes_offset = offset * (int)sizeof(_Py_CODEUNIT);
    PyObject *offset_obj = PyLong_FromSsize_t(bytes_offset);
    if (offset_obj == NULL) {
        return -1;
    }
    assert(args[2] == NULL);
    args[2] = offset_obj;
    uint8_t tools = get_tools_for_instruction(code, offset, event);
    Py_ssize_t nargsf = nargs | PY_VECTORCALL_ARGUMENTS_OFFSET;
    PyObject **callargs = &args[1];
    int err = 0;
    PyInterpreterState *interp = tstate->interp;
    while (tools) {
        int tool = most_significant_bit(tools);
        assert(tool >= 0 && tool < 8);
        assert(tools & (1 << tool));
        tools ^= (1 << tool);
        int res = call_one_instrument(interp, tstate, callargs, nargsf, tool, event);
        if (res == 0) {
            /* Nothing to do */
        }
        else if (res < 0) {
            /* error */
            err = -1;
            break;
        }
        else {
            /* DISABLE */
            remove_tools(code, offset, event, 1 << tool);
        }
    }
    Py_DECREF(offset_obj);
    return err;
}

int
_Py_call_instrumentation(
    PyThreadState *tstate, int event,
    _PyInterpreterFrame *frame, _Py_CODEUNIT *instr)
{
    PyObject *args[3] = { NULL, NULL, NULL };
    return call_instrumentation_vector(tstate, event, frame, instr, 2, args);
}

int
_Py_call_instrumentation_arg(
    PyThreadState *tstate, int event,
    _PyInterpreterFrame *frame, _Py_CODEUNIT *instr, PyObject *arg)
{
    PyObject *args[4] = { NULL, NULL, NULL, arg };
    return call_instrumentation_vector(tstate, event, frame, instr, 3, args);
}

int
_Py_call_instrumentation_2args(
    PyThreadState *tstate, int event,
    _PyInterpreterFrame *frame, _Py_CODEUNIT *instr, PyObject *arg0, PyObject *arg1)
{
    PyObject *args[5] = { NULL, NULL, NULL, arg0, arg1 };
    return call_instrumentation_vector(tstate, event, frame, instr, 4, args);
}

int
_Py_call_instrumentation_jump(
    PyThreadState *tstate, int event,
    _PyInterpreterFrame *frame, _Py_CODEUNIT *instr, _Py_CODEUNIT *target
) {
    assert(event == PY_MONITORING_EVENT_JUMP ||
           event == PY_MONITORING_EVENT_BRANCH);
    assert(frame->prev_instr == instr);
    frame->prev_instr = target;
    PyCodeObject *code = frame->f_code;
    int to = (int)(target - _PyCode_CODE(code));
    PyObject *to_obj = PyLong_FromLong(to * (int)sizeof(_Py_CODEUNIT));
    if (to_obj == NULL) {
        return -1;
    }
    PyObject *args[4] = { NULL, NULL, NULL, to_obj };
    int err = call_instrumentation_vector(tstate, event, frame, instr, 3, args);
    Py_DECREF(to_obj);
    return err;
}

static void
call_instrumentation_vector_protected(
    PyThreadState *tstate, int event,
    _PyInterpreterFrame *frame, _Py_CODEUNIT *instr, Py_ssize_t nargs, PyObject *args[])
{
    assert(_PyErr_Occurred(tstate));
    PyObject *exc = _PyErr_GetRaisedException(tstate);
    int err = call_instrumentation_vector(tstate, event, frame, instr, nargs, args);
    if (err) {
        Py_XDECREF(exc);
    }
    else {
        _PyErr_SetRaisedException(tstate, exc);
    }
    assert(_PyErr_Occurred(tstate));
}

void
_Py_call_instrumentation_exc0(
    PyThreadState *tstate, int event,
    _PyInterpreterFrame *frame, _Py_CODEUNIT *instr)
{
    assert(_PyErr_Occurred(tstate));
    PyObject *args[3] = { NULL, NULL, NULL };
    call_instrumentation_vector_protected(tstate, event, frame, instr, 2, args);
}

void
_Py_call_instrumentation_exc2(
    PyThreadState *tstate, int event,
    _PyInterpreterFrame *frame, _Py_CODEUNIT *instr, PyObject *arg0, PyObject *arg1)
{
    assert(_PyErr_Occurred(tstate));
    PyObject *args[5] = { NULL, NULL, NULL, arg0, arg1 };
    call_instrumentation_vector_protected(tstate, event, frame, instr, 4, args);
}


int
_Py_Instrumentation_GetLine(PyCodeObject *code, int index)
{
    _PyCoMonitoringData *monitoring = code->_co_monitoring;
    assert(monitoring != NULL);
    assert(monitoring->lines != NULL);
    assert(index >= code->_co_firsttraceable);
    assert(index < Py_SIZE(code));
    _PyCoLineInstrumentationData *line_data = &monitoring->lines[index];
    int8_t line_delta = line_data->line_delta;
    int line = compute_line(code, index, line_delta);
    return line;
}

int
_Py_call_instrumentation_line(PyThreadState *tstate, _PyInterpreterFrame* frame, _Py_CODEUNIT *instr)
{
    frame->prev_instr = instr;
    PyCodeObject *code = frame->f_code;
    assert(is_version_up_to_date(code, tstate->interp));
    assert(instrumentation_cross_checks(tstate->interp, code));
    int i = (int)(instr - _PyCode_CODE(code));
    _PyCoMonitoringData *monitoring = code->_co_monitoring;
    _PyCoLineInstrumentationData *line_data = &monitoring->lines[i];
    uint8_t original_opcode = line_data->original_opcode;
    if (tstate->tracing) {
        goto done;
    }
    PyInterpreterState *interp = tstate->interp;
    int8_t line_delta = line_data->line_delta;
    int line = compute_line(code, i, line_delta);
    uint8_t tools = code->_co_monitoring->line_tools != NULL ?
        code->_co_monitoring->line_tools[i] :
        (interp->monitors.tools[PY_MONITORING_EVENT_LINE] |
         code->_co_monitoring->local_monitors.tools[PY_MONITORING_EVENT_LINE]
        );
    PyObject *line_obj = PyLong_FromSsize_t(line);
    if (line_obj == NULL) {
        return -1;
    }
    PyObject *args[3] = { NULL, (PyObject *)code, line_obj };
    while (tools) {
        int tool = most_significant_bit(tools);
        assert(tool >= 0 && tool < 8);
        assert(tools & (1 << tool));
        tools &= ~(1 << tool);
        int res = call_one_instrument(interp, tstate, &args[1],
                                      2 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                      tool, PY_MONITORING_EVENT_LINE);
        if (res == 0) {
            /* Nothing to do */
        }
        else if (res < 0) {
            /* error */
            Py_DECREF(line_obj);
            return -1;
        }
        else {
            /* DISABLE  */
            remove_line_tools(code, i, 1 << tool);
        }
    }
    Py_DECREF(line_obj);
done:
    assert(original_opcode != 0);
    assert(original_opcode < INSTRUMENTED_LINE);
    assert(_PyOpcode_Deopt[original_opcode] == original_opcode);
    return original_opcode;
}

int
_Py_call_instrumentation_instruction(PyThreadState *tstate, _PyInterpreterFrame* frame, _Py_CODEUNIT *instr)
{
    PyCodeObject *code = frame->f_code;
    assert(is_version_up_to_date(code, tstate->interp));
    assert(instrumentation_cross_checks(tstate->interp, code));
    int offset = (int)(instr - _PyCode_CODE(code));
    _PyCoMonitoringData *instrumentation_data = code->_co_monitoring;
    assert(instrumentation_data->per_instruction_opcodes);
    int next_opcode = instrumentation_data->per_instruction_opcodes[offset];
    if (tstate->tracing) {
        return next_opcode;
    }
    PyInterpreterState *interp = tstate->interp;
    uint8_t tools = instrumentation_data->per_instruction_tools != NULL ?
        instrumentation_data->per_instruction_tools[offset] :
        (interp->monitors.tools[PY_MONITORING_EVENT_INSTRUCTION] |
         code->_co_monitoring->local_monitors.tools[PY_MONITORING_EVENT_INSTRUCTION]
        );
    int bytes_offset = offset * (int)sizeof(_Py_CODEUNIT);
    PyObject *offset_obj = PyLong_FromSsize_t(bytes_offset);
    if (offset_obj == NULL) {
        return -1;
    }
    PyObject *args[3] = { NULL, (PyObject *)code, offset_obj };
    while (tools) {
        int tool = most_significant_bit(tools);
        assert(tool >= 0 && tool < 8);
        assert(tools & (1 << tool));
        tools &= ~(1 << tool);
        int res = call_one_instrument(interp, tstate, &args[1],
                                      2 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                      tool, PY_MONITORING_EVENT_INSTRUCTION);
        if (res == 0) {
            /* Nothing to do */
        }
        else if (res < 0) {
            /* error */
            Py_DECREF(offset_obj);
            return -1;
        }
        else {
            /* DISABLE  */
            remove_per_instruction_tools(code, offset, 1 << tool);
        }
    }
    Py_DECREF(offset_obj);
    assert(next_opcode != 0);
    return next_opcode;
}


PyObject *
_PyMonitoring_RegisterCallback(int tool_id, int event_id, PyObject *obj)
{
    PyInterpreterState *is = _PyInterpreterState_Get();
    assert(0 <= tool_id && tool_id < PY_MONITORING_TOOL_IDS);
    assert(0 <= event_id && event_id < PY_MONITORING_EVENTS);
    PyObject *callback = is->monitoring_callables[tool_id][event_id];
    is->monitoring_callables[tool_id][event_id] = Py_XNewRef(obj);
    return callback;
}

static void
initialize_tools(PyCodeObject *code)
{
    uint8_t* tools = code->_co_monitoring->tools;
    assert(tools != NULL);
    int code_len = (int)Py_SIZE(code);
    for (int i = 0; i < code_len; i++) {
        _Py_CODEUNIT *instr = &_PyCode_CODE(code)[i];
        int opcode = instr->op.code;
        if (opcode == INSTRUMENTED_LINE) {
            opcode = code->_co_monitoring->lines[i].original_opcode;
        }
        bool instrumented = is_instrumented(opcode);
        if (instrumented) {
            opcode = DE_INSTRUMENT[opcode];
            assert(opcode != 0);
        }
        opcode = _PyOpcode_Deopt[opcode];
        if (opcode_has_event(opcode)) {
            if (instrumented) {
                int8_t event;
                if (opcode == RESUME) {
                    event = instr->op.arg != 0;
                }
                else {
                    event = EVENT_FOR_OPCODE[opcode];
                    assert(event > 0);
                }
                assert(event >= 0);
                assert(event < PY_MONITORING_INSTRUMENTED_EVENTS);
                tools[i] = code->_co_monitoring->active_monitors.tools[event];
                CHECK(tools[i] != 0);
            }
            else {
                tools[i] = 0;
            }
        }
#ifdef Py_DEBUG
        /* Initialize tools for invalid locations to all ones to try to catch errors */
        else {
            tools[i] = 0xff;
        }
        for (int j = 1; j <= _PyOpcode_Caches[opcode]; j++) {
            tools[i+j] = 0xff;
        }
#endif
        i += _PyOpcode_Caches[opcode];
    }
}

#define NO_LINE -128

static void
initialize_lines(PyCodeObject *code)
{
    _PyCoLineInstrumentationData *line_data = code->_co_monitoring->lines;
    assert(line_data != NULL);
    int code_len = (int)Py_SIZE(code);
    PyCodeAddressRange range;
    _PyCode_InitAddressRange(code, &range);
    for (int i = 0; i < code->_co_firsttraceable && i < code_len; i++) {
        line_data[i].original_opcode = 0;
        line_data[i].line_delta = -127;
    }
    int current_line = -1;
    for (int i = code->_co_firsttraceable; i < code_len; ) {
        int opcode = _Py_GetBaseOpcode(code, i);
        int line = _PyCode_CheckLineNumber(i*(int)sizeof(_Py_CODEUNIT), &range);
        line_data[i].line_delta = compute_line_delta(code, i, line);
        int length = instruction_length(code, i);
        switch (opcode) {
            case END_ASYNC_FOR:
            case END_FOR:
            case END_SEND:
            case RESUME:
                /* END_FOR cannot start a line, as it is skipped by FOR_ITER
                 * END_SEND cannot start a line, as it is skipped by SEND
                 * RESUME must not be instrumented with INSTRUMENT_LINE */
                line_data[i].original_opcode = 0;
                break;
            default:
                if (line != current_line && line >= 0) {
                    line_data[i].original_opcode = opcode;
                }
                else {
                    line_data[i].original_opcode = 0;
                }
                if (line >= 0) {
                    current_line = line;
                }
        }
        for (int j = 1; j < length; j++) {
            line_data[i+j].original_opcode = 0;
            line_data[i+j].line_delta = NO_LINE;
        }
        switch (opcode) {
            case RETURN_VALUE:
            case RAISE_VARARGS:
            case RERAISE:
                /* Blocks of code after these terminators
                 * should be treated as different lines */
                current_line = -1;
        }
        i += length;
    }
}

static void
initialize_line_tools(PyCodeObject *code, _Py_Monitors *all_events)
{
    uint8_t *line_tools = code->_co_monitoring->line_tools;
    assert(line_tools != NULL);
    int code_len = (int)Py_SIZE(code);
    for (int i = 0; i < code_len; i++) {
        line_tools[i] = all_events->tools[PY_MONITORING_EVENT_LINE];
    }
}

static
int allocate_instrumentation_data(PyCodeObject *code)
{

    if (code->_co_monitoring == NULL) {
        code->_co_monitoring = PyMem_Malloc(sizeof(_PyCoMonitoringData));
        if (code->_co_monitoring == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        code->_co_monitoring->local_monitors = (_Py_Monitors){ 0 };
        code->_co_monitoring->active_monitors = (_Py_Monitors){ 0 };
        code->_co_monitoring->tools = NULL;
        code->_co_monitoring->lines = NULL;
        code->_co_monitoring->line_tools = NULL;
        code->_co_monitoring->per_instruction_opcodes = NULL;
        code->_co_monitoring->per_instruction_tools = NULL;
    }
    return 0;
}

static int
update_instrumentation_data(PyCodeObject *code, PyInterpreterState *interp)
{
    int code_len = (int)Py_SIZE(code);
    if (allocate_instrumentation_data(code)) {
        return -1;
    }
    _Py_Monitors all_events = monitors_or(
        interp->monitors,
        code->_co_monitoring->local_monitors);
    bool multitools = multiple_tools(&all_events);
    if (code->_co_monitoring->tools == NULL && multitools) {
        code->_co_monitoring->tools = PyMem_Malloc(code_len);
        if (code->_co_monitoring->tools == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        initialize_tools(code);
    }
    if (all_events.tools[PY_MONITORING_EVENT_LINE]) {
        if (code->_co_monitoring->lines == NULL) {
            code->_co_monitoring->lines = PyMem_Malloc(code_len * sizeof(_PyCoLineInstrumentationData));
            if (code->_co_monitoring->lines == NULL) {
                PyErr_NoMemory();
                return -1;
            }
            initialize_lines(code);
        }
        if (multitools && code->_co_monitoring->line_tools == NULL) {
            code->_co_monitoring->line_tools = PyMem_Malloc(code_len);
            if (code->_co_monitoring->line_tools == NULL) {
                PyErr_NoMemory();
                return -1;
            }
            initialize_line_tools(code, &all_events);
        }
    }
    if (all_events.tools[PY_MONITORING_EVENT_INSTRUCTION]) {
        if (code->_co_monitoring->per_instruction_opcodes == NULL) {
            code->_co_monitoring->per_instruction_opcodes = PyMem_Malloc(code_len * sizeof(_PyCoLineInstrumentationData));
            if (code->_co_monitoring->per_instruction_opcodes == NULL) {
                PyErr_NoMemory();
                return -1;
            }
            /* This may not be necessary, as we can initialize this memory lazily, but it helps catch errors. */
            for (int i = 0; i < code_len; i++) {
                code->_co_monitoring->per_instruction_opcodes[i] = 0;
            }
        }
        if (multitools && code->_co_monitoring->per_instruction_tools == NULL) {
            code->_co_monitoring->per_instruction_tools = PyMem_Malloc(code_len);
            if (code->_co_monitoring->per_instruction_tools == NULL) {
                PyErr_NoMemory();
                return -1;
            }
            /* This may not be necessary, as we can initialize this memory lazily, but it helps catch errors. */
            for (int i = 0; i < code_len; i++) {
                code->_co_monitoring->per_instruction_tools[i] = 0;
            }
        }
    }
    return 0;
}

static const uint8_t super_instructions[256] = {
    [LOAD_FAST__LOAD_FAST] = 1,
    [LOAD_FAST__LOAD_CONST] = 1,
    [STORE_FAST__LOAD_FAST] = 1,
    [STORE_FAST__STORE_FAST] = 1,
    [LOAD_CONST__LOAD_FAST] = 1,
};

/* Should use instruction metadata for this */
static bool
is_super_instruction(int opcode) {
    return super_instructions[opcode] != 0;
}

int
_Py_Instrument(PyCodeObject *code, PyInterpreterState *interp)
{

    if (is_version_up_to_date(code, interp)) {
        assert(
            interp->monitoring_version == 0 ||
            instrumentation_cross_checks(interp, code)
        );
        return 0;
    }
    int code_len = (int)Py_SIZE(code);
    if (update_instrumentation_data(code, interp)) {
        return -1;
    }
    _Py_Monitors active_events = monitors_or(
        interp->monitors,
        code->_co_monitoring->local_monitors);
    _Py_Monitors new_events;
    _Py_Monitors removed_events;

    bool restarted = interp->last_restart_version > code->_co_instrumentation_version;
    if (restarted) {
        removed_events = code->_co_monitoring->active_monitors;
        new_events = active_events;
    }
    else {
        removed_events = monitors_sub(code->_co_monitoring->active_monitors, active_events);
        new_events = monitors_sub(active_events, code->_co_monitoring->active_monitors);
        assert(monitors_are_empty(monitors_and(new_events, removed_events)));
    }
    code->_co_monitoring->active_monitors = active_events;
    code->_co_instrumentation_version = interp->monitoring_version;
    if (monitors_are_empty(new_events) && monitors_are_empty(removed_events)) {
#ifdef INSTRUMENT_DEBUG
        sanity_check_instrumentation(code);
#endif
        return 0;
    }
    /* Insert instrumentation */
    for (int i = 0; i < code_len; i+= instruction_length(code, i)) {
        _Py_CODEUNIT *instr = &_PyCode_CODE(code)[i];
        if (is_super_instruction(instr->op.code)) {
            instr->op.code = _PyOpcode_Deopt[instr->op.code];
        }
        CHECK(instr->op.code != 0);
        int base_opcode = _Py_GetBaseOpcode(code, i);
        if (opcode_has_event(base_opcode)) {
            int8_t event;
            if (base_opcode == RESUME) {
                event = instr->op.arg > 0;
            }
            else {
                event = EVENT_FOR_OPCODE[base_opcode];
                assert(event > 0);
            }
            uint8_t removed_tools = removed_events.tools[event];
            if (removed_tools) {
                remove_tools(code, i, event, removed_tools);
            }
            uint8_t new_tools = new_events.tools[event];
            if (new_tools) {
                add_tools(code, i, event, new_tools);
            }
        }
    }
    uint8_t new_line_tools = new_events.tools[PY_MONITORING_EVENT_LINE];
    uint8_t removed_line_tools = removed_events.tools[PY_MONITORING_EVENT_LINE];
    if (new_line_tools | removed_line_tools) {
        _PyCoLineInstrumentationData *line_data = code->_co_monitoring->lines;
        for (int i = code->_co_firsttraceable; i < code_len;) {
            if (line_data[i].original_opcode) {
                if (removed_line_tools) {
                    remove_line_tools(code, i, removed_line_tools);
                }
                if (new_line_tools) {
                    add_line_tools(code, i, new_line_tools);
                }
            }
            i += instruction_length(code, i);
        }
    }
    uint8_t new_per_instruction_tools = new_events.tools[PY_MONITORING_EVENT_INSTRUCTION];
    uint8_t removed_per_instruction_tools = removed_events.tools[PY_MONITORING_EVENT_INSTRUCTION];
    if (new_per_instruction_tools | removed_per_instruction_tools) {
        for (int i = code->_co_firsttraceable; i < code_len;) {
            int opcode = _Py_GetBaseOpcode(code, i);
            if (opcode == RESUME || opcode == END_FOR) {
                i += instruction_length(code, i);
                continue;
            }
            if (removed_per_instruction_tools) {
                remove_per_instruction_tools(code, i, removed_per_instruction_tools);
            }
            if (new_per_instruction_tools) {
                add_per_instruction_tools(code, i, new_per_instruction_tools);
            }
            i += instruction_length(code, i);
        }
    }
#ifdef INSTRUMENT_DEBUG
    sanity_check_instrumentation(code);
#endif
    return 0;
}

#define C_RETURN_EVENTS \
    ((1 << PY_MONITORING_EVENT_C_RETURN) | \
    (1 << PY_MONITORING_EVENT_C_RAISE))

#define C_CALL_EVENTS \
    (C_RETURN_EVENTS | (1 << PY_MONITORING_EVENT_CALL))


static int
instrument_all_executing_code_objects(PyInterpreterState *interp) {
    _PyRuntimeState *runtime = &_PyRuntime;
    HEAD_LOCK(runtime);
    PyThreadState* ts = PyInterpreterState_ThreadHead(interp);
    HEAD_UNLOCK(runtime);
    while (ts) {
        _PyInterpreterFrame *frame = ts->cframe->current_frame;
        while (frame) {
            if (frame->owner != FRAME_OWNED_BY_CSTACK) {
                if (_Py_Instrument(frame->f_code, interp)) {
                    return -1;
                }
            }
            frame = frame->previous;
        }
        HEAD_LOCK(runtime);
        ts = PyThreadState_Next(ts);
        HEAD_UNLOCK(runtime);
    }
    return 0;
}

static void
set_events(_Py_Monitors *m, int tool_id, _PyMonitoringEventSet events)
{
    assert(0 <= tool_id && tool_id < PY_MONITORING_TOOL_IDS);
    for (int e = 0; e < PY_MONITORING_UNGROUPED_EVENTS; e++) {
        uint8_t *tools = &m->tools[e];
        int val = (events >> e) & 1;
        *tools &= ~(1 << tool_id);
        *tools |= (val << tool_id);
    }
}

static int
check_tool(PyInterpreterState *interp, int tool_id)
{
    if (tool_id < PY_MONITORING_SYS_PROFILE_ID &&
        interp->monitoring_tool_names[tool_id] == NULL
    ) {
        PyErr_Format(PyExc_ValueError, "tool %d is not in use", tool_id);
        return -1;
    }
    return 0;
}

int
_PyMonitoring_SetEvents(int tool_id, _PyMonitoringEventSet events)
{
    assert(0 <= tool_id && tool_id < PY_MONITORING_TOOL_IDS);
    PyInterpreterState *interp = _PyInterpreterState_Get();
    assert(events < (1 << PY_MONITORING_UNGROUPED_EVENTS));
    if (check_tool(interp, tool_id)) {
        return -1;
    }
    uint32_t existing_events = get_events(&interp->monitors, tool_id);
    if (existing_events == events) {
        return 0;
    }
    set_events(&interp->monitors, tool_id, events);
    interp->monitoring_version++;
    return instrument_all_executing_code_objects(interp);
}

int
_PyMonitoring_SetLocalEvents(PyCodeObject *code, int tool_id, _PyMonitoringEventSet events)
{
    assert(0 <= tool_id && tool_id < PY_MONITORING_TOOL_IDS);
    PyInterpreterState *interp = _PyInterpreterState_Get();
    assert(events < (1 << PY_MONITORING_UNGROUPED_EVENTS));
    if (check_tool(interp, tool_id)) {
        return -1;
    }
    if (allocate_instrumentation_data(code)) {
        return -1;
    }
    _Py_Monitors *local = &code->_co_monitoring->local_monitors;
    uint32_t existing_events = get_events(local, tool_id);
    if (existing_events == events) {
        return 0;
    }
    set_events(local, tool_id, events);
    if (is_version_up_to_date(code, interp)) {
        /* Force instrumentation update */
        code->_co_instrumentation_version = UINT64_MAX;
    }
    if (_Py_Instrument(code, interp)) {
        return -1;
    }
    return 0;
}

/*[clinic input]
module monitoring
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=37257f5987a360cf]*/
/*[clinic end generated code]*/

#include "clinic/instrumentation.c.h"

static int
check_valid_tool(int tool_id)
{
    if (tool_id < 0 || tool_id >= PY_MONITORING_SYS_PROFILE_ID) {
        PyErr_Format(PyExc_ValueError, "invalid tool %d (must be between 0 and 5)", tool_id);
        return -1;
    }
    return 0;
}

/*[clinic input]
monitoring.use_tool_id

    tool_id: int
    name: object
    /

[clinic start generated code]*/

static PyObject *
monitoring_use_tool_id_impl(PyObject *module, int tool_id, PyObject *name)
/*[clinic end generated code: output=30d76dc92b7cd653 input=ebc453761c621be1]*/
{
    if (check_valid_tool(tool_id))  {
        return NULL;
    }
    if (!PyUnicode_Check(name)) {
        PyErr_SetString(PyExc_ValueError, "tool name must be a str");
        return NULL;
    }
    PyInterpreterState *interp = _PyInterpreterState_Get();
    if (interp->monitoring_tool_names[tool_id] != NULL) {
        PyErr_Format(PyExc_ValueError, "tool %d is already in use", tool_id);
        return NULL;
    }
    interp->monitoring_tool_names[tool_id] = Py_NewRef(name);
    Py_RETURN_NONE;
}

/*[clinic input]
monitoring.free_tool_id

    tool_id: int
    /

[clinic start generated code]*/

static PyObject *
monitoring_free_tool_id_impl(PyObject *module, int tool_id)
/*[clinic end generated code: output=86c2d2a1219a8591 input=a23fb6be3a8618e9]*/
{
    if (check_valid_tool(tool_id))  {
        return NULL;
    }
    PyInterpreterState *interp = _PyInterpreterState_Get();
    Py_CLEAR(interp->monitoring_tool_names[tool_id]);
    Py_RETURN_NONE;
}

/*[clinic input]
monitoring.get_tool

    tool_id: int
    /

[clinic start generated code]*/

static PyObject *
monitoring_get_tool_impl(PyObject *module, int tool_id)
/*[clinic end generated code: output=1c05a98b404a9a16 input=eeee9bebd0bcae9d]*/

/*[clinic end generated code]*/
{
    if (check_valid_tool(tool_id))  {
        return NULL;
    }
    PyInterpreterState *interp = _PyInterpreterState_Get();
    PyObject *name = interp->monitoring_tool_names[tool_id];
    if (name == NULL) {
        Py_RETURN_NONE;
    }
    return Py_NewRef(name);
}

/*[clinic input]
monitoring.register_callback


    tool_id: int
    event: int
    func: object
    /

[clinic start generated code]*/

static PyObject *
monitoring_register_callback_impl(PyObject *module, int tool_id, int event,
                                  PyObject *func)
/*[clinic end generated code: output=e64daa363004030c input=df6d70ea4cf81007]*/
{
    if (check_valid_tool(tool_id))  {
        return NULL;
    }
    if (_Py_popcount32(event) != 1) {
        PyErr_SetString(PyExc_ValueError, "The callback can only be set for one event at a time");
        return NULL;
    }
    int event_id = _Py_bit_length(event)-1;
    if (event_id < 0 || event_id >= PY_MONITORING_EVENTS) {
        PyErr_Format(PyExc_ValueError, "invalid event %d", event);
        return NULL;
    }
    if (func == Py_None) {
        func = NULL;
    }
    func = _PyMonitoring_RegisterCallback(tool_id, event_id, func);
    if (func == NULL) {
        Py_RETURN_NONE;
    }
    return func;
}

/*[clinic input]
monitoring.get_events -> int

    tool_id: int
    /

[clinic start generated code]*/

static int
monitoring_get_events_impl(PyObject *module, int tool_id)
/*[clinic end generated code: output=4450cc13f826c8c0 input=a64b238f76c4b2f7]*/
{
    if (check_valid_tool(tool_id))  {
        return -1;
    }
    _Py_Monitors *m = &_PyInterpreterState_Get()->monitors;
    _PyMonitoringEventSet event_set = get_events(m, tool_id);
    return event_set;
}

/*[clinic input]
monitoring.set_events

    tool_id: int
    event_set: int
    /

[clinic start generated code]*/

static PyObject *
monitoring_set_events_impl(PyObject *module, int tool_id, int event_set)
/*[clinic end generated code: output=1916c1e49cfb5bdb input=a77ba729a242142b]*/
{
    if (check_valid_tool(tool_id))  {
        return NULL;
    }
    if (event_set < 0 || event_set >= (1 << PY_MONITORING_EVENTS)) {
        PyErr_Format(PyExc_ValueError, "invalid event set 0x%x", event_set);
        return NULL;
    }
    if ((event_set & C_RETURN_EVENTS) && (event_set & C_CALL_EVENTS) != C_CALL_EVENTS) {
        PyErr_Format(PyExc_ValueError, "cannot set C_RETURN or C_RAISE events independently");
        return NULL;
    }
    event_set &= ~C_RETURN_EVENTS;
    if (_PyMonitoring_SetEvents(tool_id, event_set)) {
        return NULL;
    }
    Py_RETURN_NONE;
}

/*[clinic input]
monitoring.get_local_events -> int

    tool_id: int
    code: object
    /

[clinic start generated code]*/

static int
monitoring_get_local_events_impl(PyObject *module, int tool_id,
                                 PyObject *code)
/*[clinic end generated code: output=d3e92c1c9c1de8f9 input=bb0f927530386a94]*/
{
    if (!PyCode_Check(code)) {
        PyErr_Format(
            PyExc_TypeError,
            "code must be a code object"
        );
        return -1;
    }
    if (check_valid_tool(tool_id))  {
        return -1;
    }
    _PyMonitoringEventSet event_set = 0;
    _PyCoMonitoringData *data = ((PyCodeObject *)code)->_co_monitoring;
    if (data != NULL) {
        for (int e = 0; e < PY_MONITORING_UNGROUPED_EVENTS; e++) {
            if ((data->local_monitors.tools[e] >> tool_id) & 1) {
                event_set |= (1 << e);
            }
        }
    }
    return event_set;
}

/*[clinic input]
monitoring.set_local_events

    tool_id: int
    code: object
    event_set: int
    /

[clinic start generated code]*/

static PyObject *
monitoring_set_local_events_impl(PyObject *module, int tool_id,
                                 PyObject *code, int event_set)
/*[clinic end generated code: output=68cc755a65dfea99 input=5655ecd78d937a29]*/
{
    if (!PyCode_Check(code)) {
        PyErr_Format(
            PyExc_TypeError,
            "code must be a code object"
        );
        return NULL;
    }
    if (check_valid_tool(tool_id))  {
        return NULL;
    }
    if (event_set < 0 || event_set >= (1 << PY_MONITORING_EVENTS)) {
        PyErr_Format(PyExc_ValueError, "invalid event set 0x%x", event_set);
        return NULL;
    }
    if ((event_set & C_RETURN_EVENTS) && (event_set & C_CALL_EVENTS) != C_CALL_EVENTS) {
        PyErr_Format(PyExc_ValueError, "cannot set C_RETURN or C_RAISE events independently");
        return NULL;
    }
    event_set &= ~C_RETURN_EVENTS;
    if (_PyMonitoring_SetLocalEvents((PyCodeObject*)code, tool_id, event_set)) {
        return NULL;
    }
    Py_RETURN_NONE;
}

/*[clinic input]
monitoring.restart_events

[clinic start generated code]*/

static PyObject *
monitoring_restart_events_impl(PyObject *module)
/*[clinic end generated code: output=e025dd5ba33314c4 input=add8a855063c8008]*/
{
    /* We want to ensure that:
     * last restart version > instrumented version for all code objects
     * last restart version < current version
     */
    PyInterpreterState *interp = _PyInterpreterState_Get();
    interp->last_restart_version = interp->monitoring_version + 1;
    interp->monitoring_version = interp->last_restart_version + 1;
    if (instrument_all_executing_code_objects(interp)) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static int
add_power2_constant(PyObject *obj, const char *name, int i)
{
    PyObject *val = PyLong_FromLong(1<<i);
    if (val == NULL) {
        return -1;
    }
    int err = PyObject_SetAttrString(obj, name, val);
    Py_DECREF(val);
    return err;
}

static const char *const event_names [] = {
    [PY_MONITORING_EVENT_PY_START] = "PY_START",
    [PY_MONITORING_EVENT_PY_RESUME] = "PY_RESUME",
    [PY_MONITORING_EVENT_PY_RETURN] = "PY_RETURN",
    [PY_MONITORING_EVENT_PY_YIELD] = "PY_YIELD",
    [PY_MONITORING_EVENT_CALL] = "CALL",
    [PY_MONITORING_EVENT_LINE] = "LINE",
    [PY_MONITORING_EVENT_INSTRUCTION] = "INSTRUCTION",
    [PY_MONITORING_EVENT_JUMP] = "JUMP",
    [PY_MONITORING_EVENT_BRANCH] = "BRANCH",
    [PY_MONITORING_EVENT_C_RETURN] = "C_RETURN",
    [PY_MONITORING_EVENT_PY_THROW] = "PY_THROW",
    [PY_MONITORING_EVENT_RAISE] = "RAISE",
    [PY_MONITORING_EVENT_EXCEPTION_HANDLED] = "EXCEPTION_HANDLED",
    [PY_MONITORING_EVENT_C_RAISE] = "C_RAISE",
    [PY_MONITORING_EVENT_PY_UNWIND] = "PY_UNWIND",
    [PY_MONITORING_EVENT_STOP_ITERATION] = "STOP_ITERATION",
};

/*[clinic input]
monitoring._all_events
[clinic start generated code]*/

static PyObject *
monitoring__all_events_impl(PyObject *module)
/*[clinic end generated code: output=6b7581e2dbb690f6 input=62ee9672c17b7f0e]*/
{
    PyInterpreterState *interp = _PyInterpreterState_Get();
    PyObject *res = PyDict_New();
    if (res == NULL) {
        return NULL;
    }
    for (int e = 0; e < PY_MONITORING_UNGROUPED_EVENTS; e++) {
        uint8_t tools = interp->monitors.tools[e];
        if (tools == 0) {
            continue;
        }
        PyObject *tools_obj = PyLong_FromLong(tools);
        assert(tools_obj != NULL);
        int err = PyDict_SetItemString(res, event_names[e], tools_obj);
        Py_DECREF(tools_obj);
        if (err < 0) {
            Py_DECREF(res);
            return NULL;
        }
    }
    return res;
}

static PyMethodDef methods[] = {
    MONITORING_USE_TOOL_ID_METHODDEF
    MONITORING_FREE_TOOL_ID_METHODDEF
    MONITORING_GET_TOOL_METHODDEF
    MONITORING_REGISTER_CALLBACK_METHODDEF
    MONITORING_GET_EVENTS_METHODDEF
    MONITORING_SET_EVENTS_METHODDEF
    MONITORING_GET_LOCAL_EVENTS_METHODDEF
    MONITORING_SET_LOCAL_EVENTS_METHODDEF
    MONITORING_RESTART_EVENTS_METHODDEF
    MONITORING__ALL_EVENTS_METHODDEF
    {NULL, NULL}  // sentinel
};

static struct PyModuleDef monitoring_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "sys.monitoring",
    .m_size = -1, /* multiple "initialization" just copies the module dict. */
    .m_methods = methods,
};

PyObject *_Py_CreateMonitoringObject(void)
{
    PyObject *mod = _PyModule_CreateInitialized(&monitoring_module, PYTHON_API_VERSION);
    if (mod == NULL) {
        return NULL;
    }
    if (PyObject_SetAttrString(mod, "DISABLE", &DISABLE)) {
        goto error;
    }
    if (PyObject_SetAttrString(mod, "MISSING", &_PyInstrumentation_MISSING)) {
        goto error;
    }
    PyObject *events = _PyNamespace_New(NULL);
    if (events == NULL) {
        goto error;
    }
    int err = PyObject_SetAttrString(mod, "events", events);
    Py_DECREF(events);
    if (err) {
        goto error;
    }
    for (int i = 0; i < PY_MONITORING_EVENTS; i++) {
        if (add_power2_constant(events, event_names[i], i)) {
            goto error;
        }
    }
    err = PyObject_SetAttrString(events, "NO_EVENTS", _PyLong_GetZero());
    if (err) goto error;
    PyObject *val = PyLong_FromLong(PY_MONITORING_DEBUGGER_ID);
    err = PyObject_SetAttrString(mod, "DEBUGGER_ID", val);
    Py_DECREF(val);
    if (err) goto error;
    val = PyLong_FromLong(PY_MONITORING_COVERAGE_ID);
    err = PyObject_SetAttrString(mod, "COVERAGE_ID", val);
    Py_DECREF(val);
    if (err) goto error;
    val = PyLong_FromLong(PY_MONITORING_PROFILER_ID);
    err = PyObject_SetAttrString(mod, "PROFILER_ID", val);
    Py_DECREF(val);
    if (err) goto error;
    val = PyLong_FromLong(PY_MONITORING_OPTIMIZER_ID);
    err = PyObject_SetAttrString(mod, "OPTIMIZER_ID", val);
    Py_DECREF(val);
    if (err) goto error;
    return mod;
error:
    Py_DECREF(mod);
    return NULL;
}
