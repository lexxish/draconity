#include <utility>

// TODO needs to be replaced with cross platform shimming approach

#include <Zydis/Zydis.h>
#include <bson.h>
// TODO not on windows
// #include <dlfcn.h>
#include <fcntl.h>
#include <libgen.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif //__APPLE__

#include <pthread.h>
// TODO not on windows
// #include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"
#include "draconity.h"

#ifdef __APPLE__
#include "CoreSymbolication.h"
#include <sys/mman.h>
#else // _WIN32 (or linux)
// TODO can this be removed? Only would be used for debugging of patching code
#include <memoryapi.h>

#endif

#ifndef streq
#define streq(a, b) !strcmp(a, b)
#endif

// #define DEBUG

static void *server_so = NULL, *mrec_so = NULL;

#if CPP_PORT_IS_DONE // unclear whether draconity.h's _engine macro usage here is intentional - suspect not though
drg_engine *_engine = NULL;
#endif //CPP_PORT_IS_DONE


#include "api.h" // this defines the function pointers

#define state draconity_state

//typedef struct code_hook {
//    struct code_hook **handle;
//    const char *name;
//    void *target;
//    uint64_t offset;
//    bool active;
//    uint8_t *addr, *patch, *orig;
//    size_t size;
//} code_hook;

//code_hook *DSXEngine_New_hook = NULL,
//        *DSXEngine_Create_hook = NULL,
//        *DSXEngine_LoadGrammar_hook = NULL,
//        *DSXEngine_GetMicState_hook = NULL;

class CodeHook {
public:
    CodeHook(std::string name, void *target, off_t offset = 0) {
        this->name = std::move(name);
        this->target = target;
        this->offset = offset;

        this->active = false;
        this->addr = nullptr;//.getAddress(); // TODO: looked up by symbolicator
        this->patch = nullptr; // filled in when you have a patch function
        this->orig = nullptr; // filled in when you have a patch function
        this->size = 0; // filled in by patch function
    }

    void hook_apply() {
        patch_write(this->addr, this->patch, this->size);
    }

    void hook_revert() {
        patch_write(this->addr, this->orig, this->size);
    }

    uint8_t *getAddr() const {
        return addr;
    }

    void setAddr(uint8_t *addr) {
        CodeHook::addr = addr;
    }

    uint8_t *getPatch() const {
        return patch;
    }

    void setPatch(int index, int value) {
        CodeHook::patch[index] = value;
    }

    uint8_t *getOrig() const {
        return orig;
    }

    void setOrig(uint8_t *orig) {
        CodeHook::orig = orig;
    }

    size_t getSize() const {
        return size;
    }

    void setSize(size_t size) {
        CodeHook::size = size;
    }

private:
    std::string name;
    void *target;
    off_t offset;

    bool active;
    uint8_t *addr, *patch, *orig;
    size_t size;

    static void patch_write(void *addr, void *patch, size_t size) {
        uint64_t prot_size = (size + 0xfff) & ~0xfff;
        uint64_t prot_addr = (uintptr_t) addr & ~0xfff;
        // mprotect((void *) prot_addr, size, PROT_READ | PROT_WRITE | PROT_EXEC);
        memcpy(addr, patch, size);
        // mprotect((void *) prot_addr, size, PROT_READ | PROT_EXEC);
    }
};

CodeHook *DSXEngine_New_hook = nullptr,
        *DSXEngine_Create_hook = nullptr,
        *DSXEngine_LoadGrammar_hook = nullptr,
        *DSXEngine_GetMicState_hook = nullptr;


#if RUN_IN_DRAGON
int draconity_set_param(const char *key, const char *value) {
    if (!_engine) return -1;
    void *param = _DSXEngine_GetParam(_engine, key);
    int ret = _DSXEngine_SetStringValue(_engine, param, value);
    _DSXEngine_DestroyParam(_engine, param);
    return ret;
}

//static char *homedir() {
//    char *home = getenv("HOME");
//    if (home) {
//        return strdup(home);
//    } else {
//        struct passwd pw, *pwp;
//        char buf[1024];
//        if (getpwuid_r(getuid(), &pw, buf, sizeof(buf), &pwp) == 0) {
//            return strdup(pwp->pw_dir);
//        }
//        return NULL;
//    }
//}

typedef struct {
    char *timeout;
    char *timeout_incomplete;
} config;

static bool prevent_wake = false;

void draconity_set_default_params() {
    config config = {
        .timeout = strdup("80"),
        .timeout_incomplete = strdup("500"),
    };

    draconity_set_param("DwTimeOutComplete", config.timeout);
    draconity_set_param("DwTimeOutIncomplete", config.timeout_incomplete);
    free(config.timeout);
    free(config.timeout_incomplete);
    memset(&config, 0, sizeof(config));

    draconity_set_param("DemonThreadPhraseFinishWait", "0");
    // draconity_set_param("TwoPassSkipSecondPass", "1");
    // draconity_set_param("ReturnPhonemes", "1");
    // draconity_set_param("ReturnNoise", "1");
    // draconity_set_param("ReturnPauseFillers", "1");
    // draconity_set_param("Pass2DefaultSpeed", "1");
    draconity_set_param("NumWordsAvailable", "10000");
    // draconity_set_param("MinPartialUpdateCFGTime", "1");
    // draconity_set_param("MinPartialUpdateTime", "1");
    // draconity_set_param("LiveMicMinStopFrames", "1");
    // draconity_set_param("SkipLettersVocInVocLookup", "1");
    draconity_set_param("UseParallelRecognizers", "1");
    // draconity_set_param("UsePitchTracking", "1");
    draconity_set_param("Pass1A_DurationThresh_ms", "50");
    // draconity_set_param("ComputeSpeed", "10");
    // draconity_set_param("DisableWatchdog", "1");
    // draconity_set_param("DoBWPlus", "1");
    draconity_set_param("ExtraDictationWords", "10000");
    draconity_set_param("MaxCFGWords", "20000");
    draconity_set_param("MaxPronGuessedWords", "20000");
    draconity_set_param("PhraseHypothesisCallbackThread", "1");
    // CrashOnSDAPIError
}

static void engine_setup(drg_engine *engine) {
    // _SDApi_SetShowCalls(true);
    // _SDApi_SetShowCallsWithFileSpecArgs(true);
    // _SDApi_SetShowCallPointerArguments(true);
    // _SDApi_SetShowCallMemDeltas(true);
    // _SDApi_SetShowAllocation(true);
    // _SDApi_SetShowAllocationHistogram(true);

    static unsigned int cb_key = 0;
    int ret = _DSXEngine_RegisterAttribChangedCallback(engine, draconity_attrib_changed, NULL, &cb_key);
    if (ret) draconity_logf("error adding attribute callback: %d", ret);

    ret = _DSXEngine_RegisterMimicDoneCallback(engine, draconity_mimic_done, NULL, &cb_key);
    if (ret) draconity_logf("error adding mimic done callback: %d", ret);

    ret = _DSXEngine_RegisterPausedCallback(engine, draconity_paused, "paused?", NULL, &cb_key);
    if (ret) draconity_logf("error adding paused callback: %d", ret);

    ret = _DSXEngine_SetBeginPhraseCallback(engine, draconity_phrase_begin, NULL, &cb_key);
    if (ret) draconity_logf("error setting phrase begin callback: %d", ret);

    draconity_set_default_params();

    printf("[+] status: start\n");
    fflush(stdout);
    draconity_publish("status", BCON_NEW("cmd", BCON_UTF8("start")));
}

static void engine_acquire(drg_engine *engine, bool early) {
    if (!_engine) {
        printf("[+] engine acquired\n");
        _engine = engine;
        engine_setup(engine);
    }
    if (!early && !state.ready) {
        if (_DSXEngine_GetCurrentSpeaker(_engine)) {
            draconity_ready();
        }
    }
}

static void *DSXEngine_New() {
    DSXEngine_New_hook->hook_revert();
    drg_engine *engine = _DSXEngine_New();
    DSXEngine_New_hook->hook_apply();
    draconity_logf("DSXEngine_New() = %p", engine);
    engine_acquire(engine, true);
    return _engine;
}

static int DSXEngine_Create(char *s, uint64_t val, drg_engine **engine) {
    DSXEngine_Create_hook->hook_revert();
    int ret = _DSXEngine_Create(s, val, engine);
    DSXEngine_Create_hook->hook_apply();
    draconity_logf("DSXEngine_Create(%s, %llu, &%p) = %d", s, val, engine, ret);
    engine_acquire(*engine, true);
    return ret;
}

static int DSXEngine_GetMicState(drg_engine *engine, int64_t *state) {
    engine_acquire(engine, false);
    DSXEngine_GetMicState_hook->hook_revert();
    int ret = _DSXEngine_GetMicState(engine, state);
    DSXEngine_GetMicState_hook->hook_apply();
    return ret;
}

static int DSXEngine_LoadGrammar(drg_engine *engine, int format, dsx_dataptr *data, drg_grammar **grammar) {
    engine_acquire(engine, false);
#ifdef DEBUG
    printf("dumping grammar: %d\n", data->size);
    uint8_t *buf = data->data;
    for (int i = 0; i < data->size; i++) {
        printf("%02x", buf[i]);
    }
    printf("\n\n"); fflush(stdout);
#endif

    DSXEngine_LoadGrammar_hook->hook_revert();
    int ret = _DSXEngine_LoadGrammar(engine, format, data, grammar);
    DSXEngine_LoadGrammar_hook->hook_apply();
    return ret;
}

static int DSXEngine_SetBeginPhraseCallback() {
    draconity_logf("warning: called stubbed SetBeginPhraseCallback()\n");
    return 0;
}

static int DSXEngine_SetEndPhraseCallback() {
    draconity_logf("warning: called stubbed SetEndPhraseCallback()\n");
    return 0;
}

static int DSXFileSystem_PreferenceSetValue(drg_filesystem *fs, char *a, char *b, char *c, char *d) {
    // printf("DSXFileSystem_PreferenceSetValue(%p, %s, %s, %s, %s);\n", fs, a, b, c, d);
    return _DSXFileSystem_PreferenceSetValue(fs, a, b, c, d);
}

// track which dragon grammars are active, so "dragon" pseudogrammar can activate them
int DSXGrammar_Activate(drg_grammar *grammar, uint64_t unk1, bool unk2, const char *main_rule) {
    pthread_mutex_lock(&state.dragon_lock);
    foreign_grammar *fg = malloc(sizeof(foreign_grammar));
    fg->grammar = grammar;
    fg->unk1 = unk1;
    fg->unk2 = unk2;
    if (main_rule) fg->main_rule = strdup(main_rule);
    else           fg->main_rule = NULL;
    tack_push(&state.dragon_grammars, fg);
    int ret = 0;
    if (state.dragon_enabled) {
        ret = _DSXGrammar_Activate(grammar, unk1, unk2, main_rule);
    }
    pthread_mutex_unlock(&state.dragon_lock);
    return ret;
}

int DSXGrammar_Deactivate(drg_grammar *grammar, uint64_t unk1, const char *main_rule) {
    pthread_mutex_lock(&state.dragon_lock);
    foreign_grammar *fg;
    tack_foreach(&state.dragon_grammars, fg) {
        if (fg->grammar == grammar && ((fg->main_rule == NULL && main_rule == NULL)
                    || (fg->main_rule && main_rule && strcmp(fg->main_rule, main_rule) == 0))) {
            tack_remove(&state.dragon_grammars, fg);
            free((void *)fg->main_rule);
            free(fg);
            break;
        }
    }
    int ret = 0;
    if (state.dragon_enabled) {
        ret = _DSXGrammar_Deactivate(grammar, unk1, main_rule);
    }
    pthread_mutex_unlock(&state.dragon_lock);
    return ret;
}

int DSXGrammar_SetList(drg_grammar *grammar, const char *name, dsx_dataptr *data) {
    pthread_mutex_lock(&state.dragon_lock);
    int ret = 0;
    if (state.dragon_enabled) {
        ret = _DSXGrammar_SetList(grammar, name, data);
    }
    pthread_mutex_unlock(&state.dragon_lock);
    return ret;
}
#endif //RUN_IN_DRAGON

/*
int DSXGrammar_RegisterEndPhraseCallback(drg_grammar *grammar, void *cb, void *user, unsigned int *key) {
    *key = 0;
    return 0;
    // return _DSXGrammar_RegisterEndPhraseCallback(grammar, cb, user, key);
}

int DSXGrammar_RegisterBeginPhraseCallback(drg_grammar *grammar, void *cb, void *user, unsigned int *key) {
    *key = 0;
    return 0;
    // return _DSXGrammar_RegisterBeginPhraseCallback(grammar, cb, user, key);
}

Oint DSXGrammar_RegisterPhraseHypothesisCallback(drg_grammar *grammar, void *cb, void *user, unsigned int *key) {
    *key = 0;
    return 0;
    // return _DSXGrammar_RegisterPhraseHypothesisCallback(grammar, cb, user, key);
}
*/

// patching code starts here
static ZydisDecoder dis;
static ZydisFormatter dis_fmt;
static bool dis_ready = false;

static void dis_ensure() {
    if (!dis_ready) {
        if (!ZYAN_SUCCESS(ZydisDecoderInit(&dis, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_ADDRESS_WIDTH_64)) ||
            !ZYAN_SUCCESS(ZydisFormatterInit(&dis_fmt, ZYDIS_FORMATTER_STYLE_INTEL))) {
            printf("failed to init disassembler\n");
            abort();
        }
        dis_ready = true;
    }
}

static void dis_mem(void *addr, size_t size) {
    dis_ensure();
    size_t offset = 0;
    ZydisDecodedInstruction ins;
    char buf[256];
    while (ZYDIS_SUCCESS(
            ZydisDecoderDecodeBuffer(&dis, addr + offset, size - offset, (uint64_t) addr + offset, &ins))) {
        // TODO enable for debugging patching code
        //ZydisFormatterFormatInstruction(&dis_fmt, &ins, buf, sizeof(buf));
        // printf("%016llx %s\n", ins.instrAddress, buf);
        offset += ins.length;
    }
}

static size_t dis_code_size(void *addr, size_t size) {
    dis_ensure();
    size_t offset = 0;
    ZydisDecodedInstruction ins;
    while (ZYDIS_SUCCESS(ZydisDecoderDecodeBuffer(&dis, addr + offset, 64, (uint64_t) addr + offset, &ins)) &&
           offset < size) {
        offset += ins.length;
    }
    return offset;
}

static void patch_stub(void *addr, void *to) {
    uint64_t *target = (void *) (addr + 6 + *(uint32_t *) (addr + 2));
    *target = (uintptr_t) to;
}

#define JMP_SIZE 12

static void fill_jmp(void *buf, void *addr) {
    uint8_t jmp[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    *(uint64_t *) &jmp[2] = (uintptr_t) addr;
    memcpy(buf, jmp, sizeof(jmp));
}

static void patch_redirect(unsigned char *addr, void *to, CodeHook *hook) {
    hook->setSize(dis_code_size(addr, JMP_SIZE));
    hook->setOrig(malloc(hook->getSize());
    memcpy(hook->getOrig(), addr, hook->getSize());

    hook->setPatch(malloc(hook->getSize());
    fill_jmp(hook->getPatch(), to);
    for (int i = JMP_SIZE; i < hook->getSize(); i++) {
        hook->setPatch(i, 0x90);
    }
    hook->setAddr(addr);
    hook->hook_apply();
}

typedef struct {
    std::string name;
    void **ptr;
    bool active;
} symload;

#ifdef __APPLE__
static void walk_image(CSSymbolicatorRef csym, const char *image, code_hook *hooks, symload *loads) {
    CSSymbolOwnerRef owner = CSSymbolicatorGetSymbolOwnerWithNameAtTime(csym, image, kCSNow);
    if (CSIsNull(owner)) {
        printf("  [!] IMAGE NOT FOUND: %s\n", image);
        return;
    }
    CSSymbolOwnerForeachSymbol(owner, ^int (CSSymbolRef sym) {
        const char *name = CSSymbolGetName(sym);
        if (name) {
            CSRange range = CSSymbolGetRange(sym);
            const char *stub = "DYLD-STUB$$";
            bool is_stub = (strncmp(name, stub, strlen(stub)) == 0);
            if (is_stub) {
                name += strlen(stub);
            }
            code_hook *hook = hooks;
            while (hook && hook->name) {
                if (strcmp(name, hook->name) == 0) {
                    if (range.size < 12 && !is_stub) {
                        printf("[!] skipping %s hook: not enough space (%llu < 12)\n", name, range.size);
                    } else {
                        uint8_t *addr = (void *)range.addr;
                        addr += hook->offset;
                        if (hook->offset) {
                            printf("[+] hooking %s+%#llx (%p)\n", name, hook->offset, addr);
                        } else {
                            printf("[+] hooking %s (%p)\n", name, addr);
                        }
                        if (is_stub) {
                            patch_stub(addr, hook->target);
                        } else {
                            patch_redirect(addr, hook->target, hook);
                        }
                        if (hook->handle) {
                            *hook->handle = hook;
                        }
                        hook->active = true;
                    }
                }
                hook++;
            }
            symload *load = loads;
            while (load && load->name) {
                if (strcmp(name, load->name) == 0) {
                    *load->ptr = (void *)range.addr;
                    printf("%46s = %p\n", name, *load->ptr);
                    load->active = true;
                }
                load++;
            }
            return 0;
        }
        return 1;
    });
    code_hook *hook = hooks;
    while (hook && hook->name) {
        if (!hook->active) {
            printf("[!] Failed to hook %s\n", hook->name);
        }
        hook++;
    }
    symload *load = loads;
    while (load && load->name) {
        if (!load->active) {
            printf("[!] Failed to load %s\n", load->name);
        }
        load++;
    }
}
#endif //__APPLE__

//#define h(_name) {.handle=NULL, .name=#_name, .target=_name, .offset=0, .active=false, 0}
//#define ho(_name, _handle) CodeHook(.handle=_handle, .name=#_name, .target=_name, .offset=0, .active=false, 0)
#define ho(_name, _handle) CodeHook(#_name, _handle)
static std::initializer_list<CodeHook> dragon_hooks = {
#if RUN_IN_DRAGON
ho(DSXEngine_New, &DSXEngine_New_hook),
ho(DSXEngine_Create, &DSXEngine_Create_hook),
ho(DSXEngine_GetMicState, &DSXEngine_GetMicState_hook),
ho(DSXEngine_LoadGrammar, &DSXEngine_LoadGrammar_hook),
#endif //RUN_IN_DRAGON
};
#undef h

class SymbolLoad {
private:
    symload sym;

public:
    explicit SymbolLoad(symload symload) {
        sym = std::move(symload);
    }

    SymbolLoad(std::string name, void **ptr, bool active) {
        sym.name = std::move(name);
        sym.ptr = ptr;
        sym.active = active;
    }
};

#define s(x) SymbolLoad(#x, (void **)&_##x, false)
static std::initializer_list<SymbolLoad> server_syms = {
        s(DSXEngine_Create),
        s(DSXEngine_New),
        s(DSXEngine_AddWord),
        s(DSXEngine_AddTemporaryWord),
        s(DSXEngine_DeleteWord),
        s(DSXEngine_ValidateWord),
        s(DSXEngine_EnumWords),

        s(DSXWordEnum_GetCount),
        s(DSXWordEnum_Next),
        s(DSXWordEnum_End),

        s(DSXEngine_GetCurrentSpeaker),
        s(DSXEngine_GetMicState),
        s(DSXEngine_LoadGrammar),
        s(DSXEngine_Mimic),
        s(DSXEngine_Pause),
        s(DSXEngine_RegisterAttribChangedCallback),
        s(DSXEngine_RegisterMimicDoneCallback),
        s(DSXEngine_RegisterPausedCallback),
        s(DSXEngine_Resume),
        s(DSXEngine_ResumeRecognition),
        s(DSXEngine_SetBeginPhraseCallback),
        s(DSXEngine_SetEndPhraseCallback),

        s(DSXEngine_SetStringValue),
        s(DSXEngine_GetValue),
        s(DSXEngine_GetParam),
        s(DSXEngine_DestroyParam),

        s(DSXFileSystem_PreferenceGetValue),
        s(DSXFileSystem_PreferenceSetValue),
        s(DSXFileSystem_SetResultsDirectory),
        s(DSXFileSystem_SetUsersDirectory),
        s(DSXFileSystem_SetVocabsLocation),

        s(DSXGrammar_Activate),
        s(DSXGrammar_Deactivate),
        s(DSXGrammar_Destroy),
        s(DSXGrammar_GetList),
        s(DSXGrammar_RegisterBeginPhraseCallback),
        s(DSXGrammar_RegisterEndPhraseCallback),
        s(DSXGrammar_RegisterPhraseHypothesisCallback),
        s(DSXGrammar_SetApplicationName),
        s(DSXGrammar_SetApplicationName),
        s(DSXGrammar_SetList),
        s(DSXGrammar_SetPriority),
        s(DSXGrammar_SetSpecialGrammar),
        s(DSXGrammar_Unregister),

        s(DSXResult_BestPathWord),
        s(DSXResult_GetWordNode),
        s(DSXResult_Destroy),
        SymbolLoad({nullptr}),
};

static std::initializer_list<SymbolLoad> mrec_syms = {
        s(SDApi_SetShowCalls),
        s(SDApi_SetShowCallsWithFileSpecArgs),
        s(SDApi_SetShowCallPointerArguments),
        s(SDApi_SetShowCallMemDeltas),
        s(SDApi_SetShowAllocation),
        s(SDApi_SetShowAllocationHistogram),
        s(SDRule_New),
        s(SDRule_Delete),
        SymbolLoad({nullptr}),
};
#undef s

static void *draconity_install(void *_) {
    printf("[+] draconity starting\n");

#ifdef __APPLE__
    char *image = getenv("DRACONITY_IMAGE");
    if (!image) {
        char path[MAXPATHLEN];
        uint32_t size = MAXPATHLEN;
        if (_NSGetExecutablePath(path, &size)) {
            printf("[!] failed to find executable\n");
            return NULL;
        }
        image = basename(path);
    }
    CSSymbolicatorRef csym = CSSymbolicatorCreateWithTask(mach_task_self());
    walk_image(csym, "server.so", NULL, server_syms);
    walk_image(csym, "mrec.so", NULL, mrec_syms);
    walk_image(csym, image, dragon_hooks, NULL);
    CSRelease(csym);
    if (!_engine) {
        printf("[+] waiting for engine\n");
    }
#endif //__APPLE__
    draconity_init();
    return nullptr;
}

__attribute__((constructor))
void cons() {
#ifdef DEBUG
    int log = open("/tmp/draconity.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
    dup2(log, 1);
    dup2(log, 2);
#endif
    draconity_install(nullptr);
}