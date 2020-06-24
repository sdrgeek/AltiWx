#include "pass_processing.h"
#include "logger/logger.h"
#include "config/config.h"
#include "downlink_recorder.h"
#include "dsp/dsp_manager.h"
#include "sol/sol.hpp"
#include <filesystem>
#include "lua/lua_logger.h"
#include "lua/lua_functions.h"

// Generate output filename / path
std::string generateFilepath(SatellitePass &satellitePass, SatelliteConfig &satelliteConfig, DownlinkConfig &downlinkConfig)
{
    std::tm *timeReadable = gmtime(&satellitePass.aos);
    std::string name = satelliteConfig.getName() + "_" + downlinkConfig.name + "_" + std::to_string(timeReadable->tm_year + 1900) +
                       "-" + std::to_string(timeReadable->tm_mon) + "-" + std::to_string(timeReadable->tm_mday) +
                       "--" + std::to_string(timeReadable->tm_hour) + ":" + (timeReadable->tm_min > 9 ? std::to_string(timeReadable->tm_min) : "0" + std::to_string(timeReadable->tm_min));

    std::string workdDir = configManager->getConfig().dataDirectory + "/" + std::to_string(timeReadable->tm_year + 1900) +
                           "-" + std::to_string(timeReadable->tm_mon) + "-" + std::to_string(timeReadable->tm_mday) + "/" + satelliteConfig.getName() + "/" + downlinkConfig.name + "/" + name;

    std::filesystem::create_directories(workdDir);

    return workdDir + "/" + name;
}

struct ToProcess
{
    std::string filename;
    std::string filePath;
    std::string script;
    std::string downlink;
    long samplerate;
};

void processPass(SatellitePass pass)
{
    // Get SatelliteConfig
    SatelliteConfig satelliteConfig = configManager->getConfig().getSatelliteConfigFromNORAD(pass.norad);
    logger->info("AOS " + pass.tle.name);

    // Save every recorded file
    std::vector<ToProcess> filePaths;

    // Attach all downlink recorders
    std::vector<std::shared_ptr<DownlinkRecorder>> downlinkRecoders;
    for (DownlinkConfig &downlinkConfig : satelliteConfig.downlinkConfigs)
    {
        logger->debug("Adding recorder for " + downlinkConfig.name + " downlink on " + std::to_string(downlinkConfig.frequency) + " Hz");

        // Generate filename / path, store them and setup recorder
        std::string fileName = generateFilepath(pass, satelliteConfig, downlinkConfig);
        std::string filePath = fileName + +"." + downlinkConfig.outputExtension;
        filePaths.push_back({fileName, filePath, downlinkConfig.postProcessingScript, downlinkConfig.name, downlinkConfig.bandwidth});
        logger->debug("Using file path " + filePath);

        std::shared_ptr<DownlinkRecorder> recorder = std::make_shared<DownlinkRecorder>(rtlDSP, downlinkConfig, satelliteConfig, filePath);
        downlinkRecoders.push_back(recorder);
    }

    // Start recordign all downlinks
    for (std::shared_ptr<DownlinkRecorder> &recorder : downlinkRecoders)
        recorder->start();

    // Wait until LOS
    while (time(NULL) <= pass.los)
        std::this_thread::sleep_for(std::chrono::seconds(1));

    // Stop recording, the pass is over!
    for (std::shared_ptr<DownlinkRecorder> &recorder : downlinkRecoders)
        recorder->stop();

    logger->info("LOS " + pass.tle.name);

    logger->info("Processing data for " + satelliteConfig.getName());

    // Run processing scripts
    std::vector<std::string> finalFiles;
    for (ToProcess fileToProcess : filePaths)
    {
        logger->debug("Processing " + fileToProcess.filename + " with " + fileToProcess.script);

        // Maybe nothing has to be processed? Then skip!
        if (fileToProcess.script == "none")
        {
            logger->debug("No processing script! Skipping...");
            continue;
        }

        // Check the script exists
        if (!std::filesystem::exists("scripts/" + fileToProcess.script))
        {
            logger->critical("Script " + (std::string) "scripts/" + fileToProcess.script + " does not exist!");
            continue;
        }

        // Setup a lua environment, run the script, get the result out
        try
        {
            sol::state lua;                                                                                      // sol instance
            lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::os, sol::lib::io, sol::lib::string); // Open most library that may be used
            bindLogger(lua, fileToProcess.script);                                                               // Add specific logger
            bindCustomLuaFunctions(lua);                                                                         // Add custom functions
            // Variables
            lua["filename"] = fileToProcess.filename;
            lua["input_file"] = fileToProcess.filePath;
            std::tm *timeReadable = gmtime(&pass.aos);
            lua["date"] = std::to_string(timeReadable->tm_year + 1900) +
                          "-" + std::to_string(timeReadable->tm_mon) + "-" + std::to_string(timeReadable->tm_mday) +
                          "--" + std::to_string(timeReadable->tm_hour) + ":" + (timeReadable->tm_min > 9 ? std::to_string(timeReadable->tm_min) : "0" + std::to_string(timeReadable->tm_min));
            lua["satname"] = pass.tle.name;
            lua["downlink"] = fileToProcess.downlink;
            lua["northbound"] = pass.northbound;
            lua["southbound"] = pass.southbound;
            lua["samplerate"] = fileToProcess.samplerate;
            lua.script_file("scripts/" + fileToProcess.script); // Run the script
            std::string output_file = lua["output_file"];
            finalFiles.push_back(output_file); // Store outputs
        }
        catch (std::exception &e)
        {
            logger->error(e.what());
        }
    }
}