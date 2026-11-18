#ifndef CRASH_LOGGER_H
#define CRASH_LOGGER_H

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <dlfcn.h>
#include <ucontext.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <android/log.h>

namespace Logger {
    inline FILE* s_logFile = nullptr;
    inline char s_logPath[256] = {0};

    inline void Init() {
        if (s_logFile) return;
        mkdir("/storage/emulated/0/Download/NINMODS_DUMP", 0777);
        mkdir("/sdcard/Download/NINMODS_DUMP", 0777);
        const char* candidatePaths[] = {
            "/storage/emulated/0/Download/NINMODS_DUMP/dumper_log.txt",
            "/sdcard/Download/NINMODS_DUMP/dumper_log.txt"
        };
        for (const char* p : candidatePaths) {
            FILE* f = fopen(p, "a");
            if (f) {
                s_logFile = f;
                strncpy(s_logPath, p, sizeof(s_logPath) - 1);
                fprintf(f, "\n=== [MOD INJECTED] PID: %d ===\n", getpid());
                fflush(f);
                break;
            }
        }
    }

    inline void Log(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        __android_log_print(ANDROID_LOG_INFO, "NINMODS_LOG", "%s", buf);

        if (!s_logFile) Init();
        if (s_logFile) {
            fprintf(s_logFile, "[%d] %s\n", (int)getpid(), buf);
            fflush(s_logFile);
        }
    }

    inline const char* s_currentStep = "None";
    inline void Step(const char* s) {
        s_currentStep = s;
        Log("--> STEP: %s", s);
    }

    inline void SignalHandler(int sig, siginfo_t* info, void* context) {
        const char* sigName = "UNKNOWN";
        if (sig == SIGSEGV) sigName = "SIGSEGV (Segmentation Fault / Bad Memory)";
        else if (sig == SIGBUS) sigName = "SIGBUS (Bus Error)";
        else if (sig == SIGABRT) sigName = "SIGABRT (Abort)";
        else if (sig == SIGFPE) sigName = "SIGFPE (Floating Point Error)";
        else if (sig == SIGILL) sigName = "SIGILL (Illegal Instruction)";

        Log("!!! CRASH CAUGHT !!!");
        Log("Signal: %d [%s]", sig, sigName);
        Log("Last Active Step: %s", s_currentStep);
        if (info) {
            Log("Faulting Address: %p", info->si_addr);
        }

        if (context) {
            ucontext_t* uc = (ucontext_t*)context;
#if defined(__aarch64__)
            uintptr_t pc = (uintptr_t)uc->uc_mcontext.pc;
            uintptr_t lr = (uintptr_t)uc->uc_mcontext.regs[30];
#elif defined(__arm__)
            uintptr_t pc = (uintptr_t)uc->uc_mcontext.arm_pc;
            uintptr_t lr = (uintptr_t)uc->uc_mcontext.arm_lr;
#else
            uintptr_t pc = 0, lr = 0;
#endif
            Log("Crash PC: %p, LR: %p", (void*)pc, (void*)lr);
            Dl_info dlinfo;
            if (pc && dladdr((void*)pc, &dlinfo) && dlinfo.dli_fname) {
                Log("Crash PC sym: [%s + 0x%lx] (%s)", 
                    dlinfo.dli_fname,
                    dlinfo.dli_fbase ? (pc - (uintptr_t)dlinfo.dli_fbase) : 0,
                    dlinfo.dli_sname ? dlinfo.dli_sname : "unknown");
            }
            if (lr && dladdr((void*)lr, &dlinfo)) {
                Log("Crash LR: %p in [%s + 0x%lx] (sym: %s)",
                    (void*)lr,
                    dlinfo.dli_fname ? dlinfo.dli_fname : "unknown",
                    dlinfo.dli_fbase ? (lr - (uintptr_t)dlinfo.dli_fbase) : 0,
                    dlinfo.dli_sname ? dlinfo.dli_sname : "unknown");
            }
        }

        if (s_logFile) {
            fflush(s_logFile);
            fsync(fileno(s_logFile));
            fclose(s_logFile);
            s_logFile = nullptr;
        }

        signal(sig, SIG_DFL);
        raise(sig);
    }

    inline void InstallCrashHandler() {
        Init();
        Log("Logger initialized. Active log: %s", s_logPath[0] ? s_logPath : "logcat");
    }
}

#endif // CRASH_LOGGER_H
