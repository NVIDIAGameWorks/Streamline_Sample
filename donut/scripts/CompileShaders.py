# Copyright (c) 2012-2018, NVIDIA CORPORATION. All rights reserved. 
# 
# NVIDIA CORPORATION and its licensors retain all intellectual property 
# and proprietary rights in and to this software, related documentation 
# and any modifications thereto. Any use, reproduction, disclosure or 
# distribution of this software and related documentation without an express 
# license agreement from NVIDIA CORPORATION is strictly prohibited. 

import sys
import string
import os
import threading
import subprocess
import signal
import zlib
import re
import array
import struct
import datetime
import argparse
import multiprocessing

fxcExe = 'fxc.exe' # assume these are on PATH, unless specified by the user
dxcExe = 'dxc.exe'

fxcOptions = ['/O3', '/Zi', '/nologo', '/Zpr', '/Qstrip_debug', '/Qstrip_priv', '/WX']
dxilOptions = ['-O3', '-Zi', '-nologo', '-Zpr', '-Qstrip_debug', '-WX']
spirvOptions = ['-O3', '-Zi', '-nologo', '-Zpr', '-spirv', '-fspv-target-env=vulkan1.0', '-WX' ]
compressShaders = False
shaderCompressionLevel = 3    # 10% worse compression than level 9, but about 5x faster

# these offsets must reflect the values in nvrhi/vulkan/resources.h : struct HLSLCompilerParameters

SPIRV_BindingsPerResourceType   = 128
SPIRV_BindingsPerStage          = 512

SPIRV_ShaderStageOffsets = {
    "vs": 0 * SPIRV_BindingsPerStage,
    "hs": 1 * SPIRV_BindingsPerStage,
    "ds": 2 * SPIRV_BindingsPerStage,
    "gs": 3 * SPIRV_BindingsPerStage,
    "ps": 4 * SPIRV_BindingsPerStage,
    "cs": 0 # compute shaders don't use per-stage offsets
};

SHADER_FLAG_DXBC          = 0x01
SHADER_FLAG_DXIL          = 0x02
SHADER_FLAG_SPIRV         = 0x04

SHADER_FLAG_UAV_EXTENSION = 0x20
SHADER_FLAG_FAST_GS       = 0x40
SHADER_FLAG_TYPED_UAV_LOAD = 0x80
SHADER_FLAG_WAVE_MATCH    = 0x100

PLATFORM_DXBC             = 'DXBC'
PLATFORM_DXIL             = 'DXIL'
PLATFORM_SPIRV            = 'SPIRV'

parser = argparse.ArgumentParser(description='Compiles shaders into binary files.')
parser.add_argument('-infile', nargs='?', required=True, type=argparse.FileType('r'), dest='infile')
parser.add_argument('-out', required=True, type=str, dest='output')
parser.add_argument('-parallel', action='store_true', dest='parallel',   help='compile shaders in multiple CPU threads')
parser.add_argument('-verbose', action='store_true', dest='verbose',     help='print commands before executing them')
parser.add_argument('-nomaxwell', action='store_true', dest='nomaxwell', help='excludes all shaders that use GM20x GL extensions')
parser.add_argument('-force', action='store_true', dest='force',         help='treats all source files as modified')
parser.add_argument('-fxc', type=str, dest='fxc',                        help='path to fxc executable')
parser.add_argument('-dxc', type=str, dest='dxc',                        help='path to dxc executable')
parser.add_argument('-I', dest='include_dirs', action='append',          help='include paths for the shader compiler')

group = parser.add_mutually_exclusive_group(required = True)
group.add_argument('-dxbc', action='store_const', dest='platform', const=PLATFORM_DXBC,            help='compile DirectX DXBC shaders')
group.add_argument('-dxil', action='store_const', dest='platform', const=PLATFORM_DXIL,            help='compile DirectX DXIL shaders')
group.add_argument('-spirv', action='store_const', dest='platform', const=PLATFORM_SPIRV,          help='compile SPIR-V shaders')

args = parser.parse_args()

currentModuleFileName = os.path.abspath(__file__)
if os.path.dirname(args.infile.name) != '':
    os.chdir(os.path.dirname(args.infile.name))

dstFolder = args.output
if args.fxc:
    fxcExe = args.fxc
if args.dxc:
    dxcExe = args.dxc

if args.include_dirs is None:
    args.include_dirs = []

for path in args.include_dirs:
    fxcOptions.append('/I')
    fxcOptions.append(path)
    dxilOptions.append('-I')
    dxilOptions.append(path.replace('/', os.path.sep)) # dxc doesn't understand forward slashes in include paths after the drive letter
    spirvOptions.append('-I')
    spirvOptions.append(path.replace('/', os.path.sep))

if not os.path.exists(dstFolder):
    print("Creating folder '%s'" % dstFolder)
    os.makedirs(dstFolder)

def unsigned(n):
 return n & 0xFFFFFFFF

if len(sys.argv) < 2:
    print('Missing target file argument')
    sys.exit(0)

commonFiles = [currentModuleFileName]
if args.platform == PLATFORM_DXBC:
    commonFiles.append(fxcExe)
else:
    commonFiles.append(dxcExe)

mostRecentCommonFile = max(*map(os.path.getmtime, commonFiles))

searchPatterns = ['/T', '/D', '/E']
ignoreIncludes = ['util/util.h']

dependencies = {}

def getDependencies(rootFilePath, callStack = []):
    if rootFilePath in dependencies:
        return dependencies[rootFilePath]

    callStack = [rootFilePath] + callStack

    deps = []
    try:
        with open(rootFilePath, 'r') as infile:
            for line in infile:
                m = re.match('\\s*#include\\s+["<]([^>"]+)[>"]', line)
                if m is not None:
                    dep = m.group(1)
                    if dep not in ignoreIncludes:

                        found = False
                        for includePath in [os.path.dirname(rootFilePath)] + args.include_dirs:
                            if os.path.exists(os.path.join(includePath, dep)):
                                found = True
                                break

                        if not found:
                            print("ERROR: Cannot find include file    " + 
                                "\n                    included in    ".join([dep] + callStack))
                            sys.exit(1)

                        dep = os.path.join(includePath, dep)
                        deps.append(dep)
                        subdeps = getDependencies(dep, callStack)
                        deps.extend(subdeps)

    except IOError:
        print("ERROR: Cannot open file    " + 
                "\n        included in    ".join(callStack))
        sys.exit(1)

    dependencies[rootFilePath] = deps
    return deps

def stripShaderName(fileName, slashesAndDots):
    fileName = fileName.replace('.hlsl', '')
    if slashesAndDots:
        fileName = fileName.replace('/', '_').replace('.', '_')
    return fileName


compilationTasks = []
shaderCaches = {}

def processShaderConfig(shaderConfig, fileSrcModified, fileName, rootFilePath, platform, textflags):

    # automatic substitutions
    opening = shaderConfig.find('{')
    if opening >= 0:
        closing = shaderConfig.find('}')
        assert closing > opening
        options = shaderConfig[opening+1:closing].split(',')
        for option in options:
            newConfig = shaderConfig[:opening] + option + shaderConfig[closing+1:]
            processShaderConfig(newConfig, fileSrcModified, fileName, rootFilePath, platform, textflags)
        return

    parameters = {}
    for pattern in searchPatterns:
        position = 0;
        while position != -1:
            position = shaderConfig.find(pattern + ' ', position)
            if position == -1:
                break
            position += len(pattern)
            parameters.setdefault(pattern, []).append(shaderConfig[position:].split()[0])

    if platform == PLATFORM_DXBC:
        definePrefix = '/D'
    else:
        definePrefix = '-D'

    combinedDefines = []
    hashKey = ''
 
    if '/D' in parameters:
        for shaderDefine in parameters['/D']:
            combinedDefines.append(definePrefix + shaderDefine)
            hashKey += shaderDefine + ';'

    shaderProfile = parameters['/T'][0]
    isShaderLibrary = shaderProfile.startswith('lib_')

    if isShaderLibrary:
        entryName = None
    elif '/E' in parameters:
        entryName = parameters['/E'][0]
    else:
        entryName = 'main'

    if sys.version_info.major == 3:
        defineHash = unsigned(zlib.crc32(bytes(hashKey, encoding = 'ascii')))
    else:
        defineHash = unsigned(zlib.crc32(hashKey))

    compiledShaderName = stripShaderName(fileName, True)
    if entryName is not None and entryName != 'main':
        compiledShaderName += '_' + entryName

    compiledPermutationName = compiledShaderName
    if len(combinedDefines) > 0:
        compiledPermutationName += ('_%08x' % defineHash)
    
    compiledPermutationPath = os.path.join(dstFolder, compiledPermutationName + '.bin')
    
    compiledFileCached = not args.force and os.path.exists(compiledPermutationPath) and os.path.getmtime(compiledPermutationPath) > fileSrcModified

    flags = 0

    if "H" in textflags:
        flags |= SHADER_FLAG_UAV_EXTENSION
    if "U" in textflags:
        flags |= SHADER_FLAG_TYPED_UAV_LOAD
    if "G" in textflags:
        flags |= SHADER_FLAG_FAST_GS
    if "M" in textflags:
        flags |= SHADER_FLAG_WAVE_MATCH

    commandList = []
    if platform == PLATFORM_DXBC:
        flags |= SHADER_FLAG_DXBC

        command = [fxcExe, rootFilePath]
        command += fxcOptions
        command += combinedDefines
        command += ['/T', shaderProfile]
        if entryName is not None:
            command += ['/E', entryName]
        command += ['/Fo', compiledPermutationPath]
        for spacenum in range(10):
            command += ['/D' + 'DESCRIPTOR_SET_'+str(spacenum)+'= '] # ignore descriptor sets (that whitespace is important!)
        commandList.append(command)

    elif platform == PLATFORM_DXIL:
        flags |= SHADER_FLAG_DXIL

        command = [dxcExe, rootFilePath]
        command += dxilOptions
        command += combinedDefines
        command += ['-T', shaderProfile]
        if entryName is not None:
            command += ['-E', entryName]
        command += ['-Fo', compiledPermutationPath]
        for spacenum in range(10):
            command += ['-D' + 'DESCRIPTOR_SET_'+str(spacenum)+'='] # ignore descriptor sets - everything in default space0
        commandList.append(command)

    elif platform == PLATFORM_SPIRV:
        flags |= SHADER_FLAG_SPIRV
        
		# HLSL->SPIR-V needs a register mapping for all HLSL stages and register types into register#+descriptorset

        # this accommodates the case where the same shader is used in multiple binding set locations; DESCRIPTOR_SET_BASE permutations allow generated descriptor set numbers to be offset
        shader_desciptor_set_basenum = 0
        if '/D' in parameters:
            for shaderDefine in parameters['/D']:
                if shaderDefine.startswith('DESCRIPTOR_SET_BASE='):
                    shader_desciptor_set_basenum = int(shaderDefine.split('=')[1])

        # extract shader stage from /T foo_bar
        shadertype = shaderProfile[:2]
        if shadertype not in SPIRV_ShaderStageOffsets:
            print("ERROR: unknown shader type '%s'" % shadertype)
            sys.exit(1)
        register_offset = SPIRV_ShaderStageOffsets[shadertype]

        command = [dxcExe, rootFilePath]
        command += spirvOptions
        command += combinedDefines
        command += ['-T', parameters['/T'][0]]
        if entryName is not None:
            command += ['-E', entryName]
        command += ['-Fo', compiledPermutationPath]
        for spacenum in range(10):
            command += ['-D' + 'DESCRIPTOR_SET_'+str(spacenum)+'=,space'+str(shader_desciptor_set_basenum + spacenum)] # respect descriptor sets, necessary for Vulkan
            command += ['-fvk-t-shift', str(register_offset + 0*SPIRV_BindingsPerResourceType), str(spacenum)]
            command += ['-fvk-s-shift', str(register_offset + 1*SPIRV_BindingsPerResourceType), str(spacenum)]
            command += ['-fvk-b-shift', str(register_offset + 2*SPIRV_BindingsPerResourceType), str(spacenum)]
            command += ['-fvk-u-shift', str(register_offset + 3*SPIRV_BindingsPerResourceType), str(spacenum)]
        commandList.append(command)

    if not compiledFileCached and len(commandList) > 0:
        compilationTasks.append((fileName, entryName, combinedDefines, commandList))

    if len(combinedDefines) > 0:
        if compiledShaderName in shaderCaches:
            cacheEntries = shaderCaches[compiledShaderName]
        else:
            cacheEntries = []
            shaderCaches[compiledShaderName] = cacheEntries

        cacheEntries.append((defineHash, hashKey, compiledPermutationPath, flags))
    

filesInCache = set()

for shaderConfig in args.infile.readlines():
    
    shaderConfig = shaderConfig.strip()

    if shaderConfig.find("#") == 0:
        continue # comment

    if len(shaderConfig) == 0:
        continue # empty line
    
    textflags = ""
    if shaderConfig.startswith('['):
        end = shaderConfig.find(']')
        textflags = shaderConfig[1:end].upper().strip()
        shaderConfig = shaderConfig[end+1:].lstrip()

        for letter in textflags:
            if letter not in "HUGM ":
                print("ERROR: unknown flag '%s'" % letter)
                sys.exit(1)

    fileName = shaderConfig.split()[0]
    rootFilePath = fileName

    fileDependencies = getDependencies(rootFilePath)

    fileSrcModified = max(os.path.getmtime(rootFilePath), mostRecentCommonFile, *map(os.path.getmtime, fileDependencies))

    processShaderConfig(shaderConfig, fileSrcModified, fileName, rootFilePath, args.platform, textflags)



allFilesCached = len(compilationTasks) == 0
terminate = False

def sigint_handler(signal, frame):
    global terminate
    #print("SIGINT received, terminating")
    terminate = True

signal.signal(signal.SIGINT, sigint_handler)

report_lock = threading.Lock()
errors = []

# Workaround for http://bugs.python.org/issue1731717
subprocess_fix = threading.Lock()

def compileShader(progressIndicator, fileName, entryName, combinedDefines, commandList):
    global terminate

    def report(commandName, code, err):
        global terminate
        report_lock.acquire()
        try:
            codeStr = " OK  " if code == 0 else "FAIL "
            print("%s %6s : %s %s:%s %s" % (progressIndicator, commandName, codeStr, fileName, entryName, " ".join(combinedDefines)))

            if code != 0:
                errors.append((fileName, entryName, combinedDefines, err))
                terminate = True
        finally:
            report_lock.release()
        
        sys.stdout.flush()    

    for command in commandList:
        if terminate:
            break

        subprocess_fix.acquire()
        try:
            if args.verbose:
                print()
                print(" ".join(command))
            process = subprocess.Popen(command, stdout = subprocess.PIPE, stderr = subprocess.PIPE)
        finally:
            subprocess_fix.release()

        out, err = process.communicate()
        code = process.returncode
        commandName = command[0]
        commandName = os.path.splitext(os.path.basename(commandName))[0]
        report(commandName, code, out + err)


task_lock = threading.Lock()

def compileShaders(tasks, initialTaskCount):
    #just do the operations serially
    while True:
        if terminate:
            break

        task = None
        tasksLeft = 0

        task_lock.acquire()
        tasksLeft = len(tasks)
        if tasksLeft > 0:
            task = tasks[0]
            del tasks[0]
        task_lock.release()

        if task is None:
            break

        progressIndicator = "[%5.1f%%]" % ((initialTaskCount - tasksLeft) * 100.0 / initialTaskCount)
        compileShader(progressIndicator, *task)
        
if not allFilesCached:
    startTime = datetime.datetime.now()
    taskCount = len(compilationTasks)

    if args.parallel:
        cpuCount = multiprocessing.cpu_count()
    else:
        cpuCount = 1
    print("Compiling %d shaders in %d threads..." % (taskCount, cpuCount))

    threads = []
    initialTaskCount = len(compilationTasks)
    for i in range(cpuCount):
        thread = threading.Thread(target = compileShaders, args = (compilationTasks, initialTaskCount))
        thread.start()
        threads.append(thread)
        
    for thread in threads:
        thread.join()

    endTime = datetime.datetime.now()
    timeDelta = (endTime - startTime).total_seconds()
    print("Processing time: %.1f seconds, %.2f seconds per shader" % (timeDelta, timeDelta / taskCount))

    print("Errors: %d" % len(errors))

    for fileName, entryName, combinedDefines, err in errors:
        sys.stderr.write("Errors for %s / %s %s:\n" % (fileName, entryName, " ".join(combinedDefines)))

        if sys.version_info >= (3,0):
            err = err.decode('utf-8', 'replace')

        for line in err.splitlines():
            # try to convert the DXC error reporting style to something understood by Visual Studio
            match = re.match(r'([a-zA-Z0-9_./\\:]+):(\d+):(\d+): (.*)', line)

            if match is not None:
                fileName = match.group(1)
                lineNumber = int(match.group(2))
                column = int(match.group(3))
                message = match.group(4)

                fileName = os.path.abspath(fileName)
                line = '%(fileName)s(%(lineNumber)d,%(column)d): %(message)s' % locals()

            sys.stderr.write(line)
            sys.stderr.write('\n')
        
    if len(errors) > 0:
        sys.exit(1)


for compiledShaderName in shaderCaches:
    cacheEntries = shaderCaches[compiledShaderName]

    outputFile = os.path.join(dstFolder, compiledShaderName + '.bin')

    if os.path.exists(outputFile):
        maxInputTime = max([os.path.getmtime(compiledPermutationPath) for (defineHash, hashKey, compiledPermutationPath, flags) in cacheEntries])
        outputTime = os.path.getmtime(outputFile)

        if outputTime > maxInputTime:
            continue

    shaderCacheFile = open(outputFile, 'wb')
    shaderCacheFile.write(b'NVSP')

    for (defineHash, hashKey, compiledPermutationPath, flags) in cacheEntries:
        if sys.version_info.major == 3:
            hashKey = bytes(hashKey, 'ascii')
        binaryFile = open(compiledPermutationPath, 'rb')
        data = binaryFile.read()
        binaryFile.close()

        fileCrc = unsigned(zlib.crc32(data))

        header = struct.pack('<IIIII', len(hashKey), len(data), fileCrc, defineHash, flags)
        shaderCacheFile.write(header)
        shaderCacheFile.write(hashKey)
        shaderCacheFile.write(data)

    shaderCacheFile.close()
