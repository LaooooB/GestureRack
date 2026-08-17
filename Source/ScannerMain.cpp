#include <JuceHeader.h>
#include <iostream>
#include "PluginCategory.h"

namespace
{
struct Args
{
    juce::File pathsFile;
    juce::File catalogFile;
    juce::File deadmanFile;
    juce::File statusFile;
    bool clearBlacklist = false;
};

bool parseArgs (int argc, char* argv[], Args& out)
{
    for (int i = 1; i < argc; ++i)
    {
        const juce::String key (argv[i]);
        auto next = [&]() -> juce::String
        {
            if (i + 1 >= argc)
                return {};
            return juce::String (argv[++i]);
        };

        if (key == "--paths") out.pathsFile = juce::File (next());
        else if (key == "--catalog") out.catalogFile = juce::File (next());
        else if (key == "--deadman") out.deadmanFile = juce::File (next());
        else if (key == "--status") out.statusFile = juce::File (next());
        else if (key == "--clear-blacklist") out.clearBlacklist = true;
    }
    return out.pathsFile.getFullPathName().isNotEmpty()
        && out.catalogFile.getFullPathName().isNotEmpty()
        && out.deadmanFile.getFullPathName().isNotEmpty()
        && out.statusFile.getFullPathName().isNotEmpty();
}

void writeStatus (const juce::File& file, const juce::String& text)
{
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::String (juce::Time::currentTimeMillis()) + "\t" + text);
}

void saveCatalog (juce::KnownPluginList& list, const juce::File& file)
{
    juce::KnownPluginList normalized;
    for (auto plugin : list.getTypes())
    {
        plugin.category = gr::normalizedPluginCategoryName (plugin);
        normalized.addType (plugin);
    }
    for (const auto& blacklisted : list.getBlacklistedFiles())
        normalized.addToBlacklist (blacklisted);

    file.getParentDirectory().createDirectory();
    if (auto xml = normalized.createXml())
        file.replaceWithText (xml->toString());
}
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    Args args;
    if (! parseArgs (argc, argv, args))
    {
        std::cerr << "GestureRackScanner: invalid arguments\n";
        return 2;
    }

    juce::KnownPluginList knownPlugins;
    if (args.catalogFile.existsAsFile())
        if (auto xml = juce::XmlDocument::parse (args.catalogFile))
            knownPlugins.recreateFromXml (*xml);

    if (args.deadmanFile.existsAsFile())
    {
        juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (knownPlugins, args.deadmanFile);
        saveCatalog (knownPlugins, args.catalogFile);
        args.deadmanFile.deleteFile();
    }

    if (args.clearBlacklist)
    {
        knownPlugins.clearBlacklistedFiles();
        saveCatalog (knownPlugins, args.catalogFile);
    }

    juce::StringArray pathLines;
    if (args.pathsFile.existsAsFile())
        pathLines = juce::StringArray::fromLines (args.pathsFile.loadFileAsString());

    juce::FileSearchPath paths;
    for (auto line : pathLines)
    {
        line = line.trim();
        if (line.isNotEmpty())
            paths.addIfNotAlreadyThere (juce::File (line));
    }
    paths.removeRedundantPaths();
    if (paths.getNumPaths() == 0)
    {
        writeStatus (args.statusFile, "NO PATHS");
        return 3;
    }

    juce::AudioPluginFormatManager formatManager;
    juce::addDefaultFormatsToManager (formatManager);
    bool scannedVst3 = false;

    for (auto* format : formatManager.getFormats())
    {
        if (format == nullptr || ! format->canScanForPlugins() || format->getName() != "VST3")
            continue;
        scannedVst3 = true;
        juce::PluginDirectoryScanner scanner (knownPlugins, *format, paths, true,
                                               args.deadmanFile, false);

        for (;;)
        {
            auto next = scanner.getNextPluginFileThatWillBeScanned();
            if (next.isEmpty())
                break;
            writeStatus (args.statusFile,
                         "SCAN\t" + juce::String (scanner.getProgress(), 3) + "\t" + next);

            juce::String pluginBeingScanned;
            if (! scanner.scanNextFile (true, pluginBeingScanned))
                break;

            saveCatalog (knownPlugins, args.catalogFile);
        }

        for (const auto& failed : scanner.getFailedFiles())
            knownPlugins.addToBlacklist (failed);
    }

    knownPlugins.scanFinished();
    saveCatalog (knownPlugins, args.catalogFile);
    args.deadmanFile.deleteFile();
    writeStatus (args.statusFile,
                 scannedVst3
                    ? "DONE\t" + juce::String (knownPlugins.getNumTypes())
                        + "\t" + juce::String (knownPlugins.getBlacklistedFiles().size())
                    : "VST3 FORMAT UNAVAILABLE");
    return scannedVst3 ? 0 : 4;
}
