#pragma once

#ifdef AGS_ENABLE
namespace ags {
    const char* getVendorName(int vendorId)
    {
        switch (vendorId)
        {
        case 0x1002: return "AMD";
        case 0x8086: return "INTEL";
        case 0x10DE: return "NVIDIA";
        default: return "unknown";
        }
    }

    void PrintDisplayInfo(const AGSGPUInfo& gpuInfo)
    {
        for (int gpuIndex = 0; gpuIndex < gpuInfo.numDevices; gpuIndex++)
        {
            const AGSDeviceInfo& device = gpuInfo.devices[gpuIndex];

            donut::log::info("\n---------- Device %d%s, %s\n", gpuIndex, device.isPrimaryDevice ? " [primary]" : "", device.adapterString);

            donut::log::info("Vendor id:   0x%04X (%s)\n", device.vendorId, getVendorName(device.vendorId));
            donut::log::info("Device id:   0x%04X\n", device.deviceId);
            donut::log::info("Revision id: 0x%04X\n\n", device.revisionId);

            const char* asicFamily[] =
            {
                "unknown",
                "Pre GCN",
                "GCN Gen1",
                "GCN Gen2",
                "GCN Gen3",
                "GCN Gen4",
                "Vega",
                "RDNA",
                "RDNA2"
            };

            static_assert(_countof(asicFamily) == AGSDeviceInfo::AsicFamily_Count, "asic family table out of date");

            if (device.vendorId == 0x1002)
            {
                char wgpInfo[256] = {};
                if (device.asicFamily >= AGSDeviceInfo::AsicFamily_RDNA)
                {
                    donut::log::info(wgpInfo, ", %d WGPs", device.numWGPs);
                }

                donut::log::info("Architecture: %s, %s%s%d CUs%s, %d ROPs\n", asicFamily[device.asicFamily], device.isAPU ? "(APU), " : "", device.isExternal ? "(External), " : "", device.numCUs, wgpInfo, device.numROPs);
                donut::log::info("    core clock %d MHz, memory clock %d MHz\n", device.coreClock, device.memoryClock);
                donut::log::info("    %.1f Tflops\n", device.teraFlops);
                donut::log::info("local memory: %d MBs (%.1f GB/s), shared memory: %d MBs\n\n", (int)(device.localMemoryInBytes / (1024 * 1024)), (float)device.memoryBandwidth / 1024.0f, (int)(device.sharedMemoryInBytes / (1024 * 1024)));
            }

            donut::log::info("\n");

            if (device.eyefinityEnabled)
            {
                donut::log::info("SLS grid is %d displays wide by %d displays tall\n", device.eyefinityGridWidth, device.eyefinityGridHeight);
                donut::log::info("SLS resolution is %d x %d pixels%s\n", device.eyefinityResolutionX, device.eyefinityResolutionY, device.eyefinityBezelCompensated ? ", bezel-compensated" : "");
            }
            else
            {
                donut::log::info("Eyefinity not enabled on this device\n");
            }

            donut::log::info("\n");

            for (int i = 0; i < device.numDisplays; i++)
            {
                const AGSDisplayInfo& display = device.displays[i];

                donut::log::info("\t---------- Display %d %s----------------------------------------\n", i, display.isPrimaryDisplay ? "[primary]" : "---------");

                donut::log::info("\tdevice name: %s\n", display.displayDeviceName);
                donut::log::info("\tmonitor name: %s\n\n", display.name);

                donut::log::info("\tMax resolution:             %d x %d, %.1f Hz\n", display.maxResolutionX, display.maxResolutionY, display.maxRefreshRate);
                donut::log::info("\tCurrent resolution:         %d x %d, Offset (%d, %d), %.1f Hz\n", display.currentResolution.width, display.currentResolution.height, display.currentResolution.offsetX, display.currentResolution.offsetY, display.currentRefreshRate);
                donut::log::info("\tVisible resolution:         %d x %d, Offset (%d, %d)\n\n", display.visibleResolution.width, display.visibleResolution.height, display.visibleResolution.offsetX, display.visibleResolution.offsetY);

                donut::log::info("\tchromaticity red:           %f, %f\n", display.chromaticityRedX, display.chromaticityRedY);
                donut::log::info("\tchromaticity green:         %f, %f\n", display.chromaticityGreenX, display.chromaticityGreenY);
                donut::log::info("\tchromaticity blue:          %f, %f\n", display.chromaticityBlueX, display.chromaticityBlueY);
                donut::log::info("\tchromaticity white point:   %f, %f\n\n", display.chromaticityWhitePointX, display.chromaticityWhitePointY);

                donut::log::info("\tluminance: [min, max, avg]  %f, %f, %f\n", display.minLuminance, display.maxLuminance, display.avgLuminance);

                donut::log::info("\tscreen reflectance diffuse  %f\n", display.screenDiffuseReflectance);
                donut::log::info("\tscreen reflectance specular %f\n\n", display.screenSpecularReflectance);

                if (display.HDR10)
                    donut::log::info("\tHDR10 supported\n");

                if (display.dolbyVision)
                    donut::log::info("\tDolby Vision supported\n");

                if (display.freesync)
                    donut::log::info("\tFreesync supported\n");

                if (display.freesyncHDR)
                    donut::log::info("\tFreesync HDR supported\n");

                donut::log::info("\n");

                if (display.eyefinityInGroup)
                {
                    donut::log::info("\tEyefinity Display [%s mode] %s\n", display.eyefinityInPortraitMode ? "portrait" : "landscape", display.eyefinityPreferredDisplay ? " (preferred display)" : "");

                    donut::log::info("\tGrid coord [%d, %d]\n", display.eyefinityGridCoordX, display.eyefinityGridCoordY);
                }

                donut::log::info("\tlogical display index: %d\n", display.logicalDisplayIndex);
                donut::log::info("\tADL adapter index: %d\n\n", display.adlAdapterIndex);

                donut::log::info("\n");
            }
        }
    }

    void testDriver(const char* driver, unsigned int driverToCompareAgainst)
    {
        AGSDriverVersionResult result = agsCheckDriverVersion(driver, driverToCompareAgainst);

        int major = (driverToCompareAgainst & 0xFFC00000) >> 22;
        int minor = (driverToCompareAgainst & 0x003FF000) >> 12;
        int patch = (driverToCompareAgainst & 0x00000FFF);

        if (result == AGS_SOFTWAREVERSIONCHECK_UNDEFINED)
        {
            donut::log::info("Driver check could not determine the driver version for %s\n", driver);
        }
        else
        {
            donut::log::info("Driver check shows the installed %s driver is %s the %d.%d.%d required version\n", driver, result == AGS_SOFTWAREVERSIONCHECK_OK ? "newer or the same as" : "older than", major, minor, patch);
        }
    }

    bool initialiseAGS(AGSContext*& agsContext) {
        AGSGPUInfo gpuInfo = {};
        AGSConfiguration config = {};
        if (agsInitialize(AGS_MAKE_VERSION(AMD_AGS_VERSION_MAJOR, AMD_AGS_VERSION_MINOR, AMD_AGS_VERSION_PATCH), &config, &agsContext, &gpuInfo) == AGS_SUCCESS)
        {
            donut::log::info("\nAGS Library initialized: v%d.%d.%d\n", AMD_AGS_VERSION_MAJOR, AMD_AGS_VERSION_MINOR, AMD_AGS_VERSION_PATCH);
            donut::log::info("-----------------------------------------------------------------\n");

            donut::log::info("Radeon Software Version:   %s\n", gpuInfo.radeonSoftwareVersion);
            donut::log::info("Driver Version:            %s\n", gpuInfo.driverVersion);
            donut::log::info("-----------------------------------------------------------------\n");
            PrintDisplayInfo(gpuInfo);
            donut::log::info("-----------------------------------------------------------------\n");

            if (0)
            {
                donut::log::info("\n");
                testDriver(gpuInfo.radeonSoftwareVersion, AGS_MAKE_VERSION(20, 1, 0));
                testDriver("18.8.randombetadriver", AGS_MAKE_VERSION(18, 8, 2));
                testDriver("18.8.123randomdriver", AGS_MAKE_VERSION(18, 8, 2));
                testDriver("18.9.randomdriver", AGS_MAKE_VERSION(18, 8, 2));
                testDriver("18.8.2", AGS_MAKE_VERSION(18, 8, 2));
                testDriver("18.8.2", AGS_MAKE_VERSION(18, 8, 1));
                testDriver("18.8.2", AGS_MAKE_VERSION(18, 8, 3));
                donut::log::info("\n");
            }


            return true;
        }
        else
        {
            donut::log::info("Failed to initialize AGS Library\n");
            return false;
        }

    }

    bool deinitialiseAGS(AGSContext* agsContext) {
        if (agsContext == nullptr) {
            return true;
        }
        auto h = agsDeInitialize(agsContext) == AGS_SUCCESS;
        if (h) {
            agsContext = nullptr;
        }
        return h;
    }
}
#endif