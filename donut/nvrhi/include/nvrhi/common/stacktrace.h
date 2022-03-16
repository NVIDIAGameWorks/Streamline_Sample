
#pragma once

//#define ENABLE_STACKTRACE

namespace NVRHI
{

    // this function only works on windows if the StackWalker library is downloaded in nvrhi/thirdparty when cmake is invoked AND ENABLE_STACKTRACE is defined
    // see nvrhi/thirdparty/_README.txt for details
    const char* getStackTrace(int ignoreFrameCount = -1);

}