/*
* Copyright (c) 2024 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#pragma once

// Logger function callback which allows client to implement logging
namespace rtxmu
{
    enum class Level
    {
        DISABLED = 0,
        FATAL,
        ERR,
        WARN,
        INFO,
        DBG
    };

    class Logger
    {
    public:

        static Level s_loggerVerbosity;
        static void (*s_loggerCallback)(const char*);
        static void setLoggerSettings(Level verbosity);
        static void setLoggerCallback(void (*loggerCallback)(const char*));
        static void log(Level verbosity, const char* msg);
        static bool isEnabled(Level verbosity);
    };
}// end rtxmu namespace
