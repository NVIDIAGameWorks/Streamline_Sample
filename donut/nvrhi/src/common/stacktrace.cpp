
#include <nvrhi/common/stacktrace.h>

#if defined(STACKWALKER_ENABLED) && defined(ENABLE_STACKTRACE)

#include <mutex>
#include <StackWalker.h>

namespace NVRHI
{

    const char* getStackTrace(int ignoreFrameCount)
    {
        class MyStackWalker : public StackWalker
        {
            std::string output;
            int ignoreFrameCount = -1;
        public:
            MyStackWalker() : StackWalker(SymAll | RetrieveSymbol | RetrieveLine) {}

            void reset() { output.clear(); ignoreFrameCount = -1; }

            const std::string& getOutput() { return output; }

            void setIgnoreFrameCount(int _ignoreFrameCount) { ignoreFrameCount = _ignoreFrameCount; }

            void OnCallstackEntry(CallstackEntryType eType, CallstackEntry& entry)
            {
                CHAR   buffer[1024];
                size_t bufferSize = sizeof(buffer);

                if ((eType != lastEntry) && (entry.offset != 0))
                {
                    static const char funcMissingText[] = "(funcname missing)";
                    if (entry.name[0] == 0)
                        strncpy_s(buffer, sizeof(buffer), funcMissingText, sizeof(funcMissingText));
                    else
                        _snprintf_s(buffer, bufferSize, "%s | ", entry.name);
                    buffer[bufferSize - 1] = 0;
                    OnOutput(buffer);
                }
            }
        protected:
            virtual void OnOutput(LPCSTR szText) {
                if (ignoreFrameCount-- <= 0)
                    output += szText;

                // default function sends a to OutputDebugString
                //StackWalker::OnOutput(szText);
            }
        };

        static std::mutex stackTraceMutex;

        // stack walker is not thread safe, nor is dbghelp, so protecting execution of these is a good thing.
        std::lock_guard<std::mutex> guard(stackTraceMutex);
        static MyStackWalker sw;
        {
            // discard module and library information. alternatively we could send to debug output
            static bool firstRun = true;
            if (firstRun)
            {
                firstRun = false;
                sw.ShowCallstack();
            }
        }

        sw.reset();
        sw.setIgnoreFrameCount(ignoreFrameCount);
        sw.ShowCallstack();
        return sw.getOutput().c_str(); // reset may get called before this gets copied on another thread.
    }
} // namespace NVRHI

#else //defined(ENABLE_STACKTRACE) && defined(WIN32) && (defined(_DEBUG) || (defined(_RELEASE) && STACKTRACE_IN_RELEASE))
namespace NVRHI
{
    const char* getStackTrace(int /*ignoreFrameCount*/) { return "(no trace)"; }
}
#endif //defined(ENABLE_STACKTRACE) && defined(WIN32) && (defined(_DEBUG) || (defined(_RELEASE) && STACKTRACE_IN_RELEASE))
