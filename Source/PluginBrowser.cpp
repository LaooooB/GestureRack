#include "PluginBrowser.h"
#include <algorithm>

namespace
{
const juce::Colour kBg        { 20, 22, 26 };
const juce::Colour kPanel     { 27, 30, 36 };
const juce::Colour kRaised    { 35, 39, 46 };
const juce::Colour kBorder    { 55, 61, 71 };
const juce::Colour kText      { 232, 235, 240 };
const juce::Colour kMuted     { 139, 148, 162 };
const juce::Colour kAccent    { 245, 178, 60 };
const juce::Colour kBlue      { 86, 156, 235 };
const juce::Colour kGreen     { 93, 190, 126 };

juce::String compactPluginName (const juce::PluginDescription& description)
{
    return description.name.isNotEmpty()
        ? description.name
        : juce::File (description.fileOrIdentifier).getFileNameWithoutExtension();
}
}

class PluginBrowserComponent::ScanThread final : public juce::Thread
{
public:
    explicit ScanThread (PluginBrowserComponent& ownerToUse)
        : juce::Thread ("Gesture Rack Plugin Scan"), owner (ownerToUse)
    {
    }

    void run() override
    {
        owner.runScan (*this);
    }

private:
    PluginBrowserComponent& owner;
};

PluginBrowserComponent::PluginBrowserComponent (LoadCallback loadCallbackToUse,
                                                CloseCallback closeCallbackToUse)
    : loadCallback (std::move (loadCallbackToUse)),
      closeCallback (std::move (closeCallbackToUse))
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    juce::addDefaultFormatsToManager (formatManager);

    loadPaths();
    loadCatalog();
    refreshCatalog();

    addAndMakeVisible (searchBox);
    searchBox.setSingleLine (true);
    searchBox.setTextToShowWhenEmpty ("Search plugins", kMuted);
    searchBox.setColour (juce::TextEditor::backgroundColourId, kRaised);
    searchBox.setColour (juce::TextEditor::textColourId, kText);
    searchBox.setColour (juce::TextEditor::highlightColourId, kBlue.withAlpha (0.35f));
    searchBox.setColour (juce::TextEditor::highlightedTextColourId, kText);
    searchBox.setColour (juce::TextEditor::outlineColourId, kBorder);
    searchBox.setColour (juce::TextEditor::focusedOutlineColourId, kBlue);
    searchBox.setColour (juce::TextEditor::caretColourId, kAccent);
    searchBox.onTextChange = [this] { rebuildFilter(); };
    searchBox.onReturnKey = [this] { loadSelected(); };

    addAndMakeVisible (pathsButton);
    addAndMakeVisible (scanButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (closeButton);
    for (auto* button : { &pathsButton, &scanButton, &loadButton, &closeButton })
    {
        button->setColour (juce::TextButton::buttonColourId, kRaised);
        button->setColour (juce::TextButton::buttonOnColourId, kBlue);
        button->setColour (juce::TextButton::textColourOffId, kText);
        button->setColour (juce::TextButton::textColourOnId, kText);
    }

    pathsButton.onClick = [this] { showPathsMenu(); };
    scanButton.onClick = [this] { startScan(); };
    loadButton.onClick = [this] { loadSelected(); };
    closeButton.onClick = [this]
    {
        if (closeCallback)
            closeCallback();
    };
    loadButton.setEnabled (false);

    addAndMakeVisible (resultList);
    resultList.setRowHeight (36);
    resultList.setColour (juce::ListBox::backgroundColourId, kPanel);
    resultList.setColour (juce::ListBox::outlineColourId, kBorder);
    resultList.setOutlineThickness (1);

    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, kMuted);
    statusLabel.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    statusLabel.setJustificationType (juce::Justification::centredLeft);

    rebuildFilter();
    startTimerHz (12);
    setVisible (false);
}

PluginBrowserComponent::~PluginBrowserComponent()
{
    stopTimer();
    resultList.setModel (nullptr);

    if (scanThread != nullptr && scanThread->isThreadRunning())
    {
        scanThread->signalThreadShouldExit();
        scanThread->stopThread (10000);
    }
}

void PluginBrowserComponent::showForSlot (int slotIndex)
{
    targetSlot = juce::jmax (0, slotIndex);
    setVisible (true);
    toFront (true);
    searchBox.grabKeyboardFocus();
    searchBox.selectAll();
    repaint();
}

bool PluginBrowserComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (closeCallback)
            closeCallback();
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        loadSelected();
        return true;
    }

    return false;
}

int PluginBrowserComponent::getNumRows()
{
    return static_cast<int> (filteredIndices.size());
}

void PluginBrowserComponent::paintListBoxItem (int rowNumber,
                                               juce::Graphics& g,
                                               int width,
                                               int height,
                                               bool rowIsSelected)
{
    if (! juce::isPositiveAndBelow (rowNumber, static_cast<int> (filteredIndices.size())))
        return;

    const auto catalogIndex = filteredIndices[static_cast<size_t> (rowNumber)];
    if (! juce::isPositiveAndBelow (catalogIndex, catalog.size()))
        return;

    const auto& plugin = catalog.getReference (catalogIndex);
    auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (7, 2);

    if (rowIsSelected)
    {
        g.setColour (kBlue.withAlpha (0.18f));
        g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
        g.setColour (kBlue.withAlpha (0.75f));
        g.drawRoundedRectangle (bounds.toFloat(), 6.0f, 1.0f);
    }

    auto meta = bounds.removeFromRight (juce::jmin (260, bounds.getWidth() * 42 / 100));
    bounds.removeFromRight (8);

    g.setColour (kText);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawFittedText (compactPluginName (plugin), bounds,
                      juce::Justification::centredLeft, 1);

    auto metaText = plugin.manufacturerName;
    if (plugin.category.isNotEmpty())
        metaText += (metaText.isNotEmpty() ? "  /  " : "") + plugin.category;
    if (plugin.pluginFormatName.isNotEmpty())
        metaText += (metaText.isNotEmpty() ? "  /  " : "") + plugin.pluginFormatName;

    g.setColour (kMuted);
    g.setFont (juce::FontOptions (10.0f));
    g.drawFittedText (metaText, meta, juce::Justification::centredRight, 1);
}

void PluginBrowserComponent::selectedRowsChanged (int)
{
    loadButton.setEnabled (juce::isPositiveAndBelow (resultList.getSelectedRow(),
                                                      static_cast<int> (filteredIndices.size())));
}

void PluginBrowserComponent::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    resultList.selectRow (row);
    loadSelected();
}

void PluginBrowserComponent::timerCallback()
{
    const auto version = catalogVersion.load (std::memory_order_acquire);
    if (version != displayedCatalogVersion && ! scanning.load (std::memory_order_relaxed))
    {
        displayedCatalogVersion = version;
        refreshCatalog();
    }

    const auto isScanning = scanning.load (std::memory_order_relaxed);
    scanButton.setButtonText (isScanning ? "SCANNING" : "SCAN");
    scanButton.setEnabled (! isScanning);
    pathsButton.setEnabled (! isScanning);

    const auto pathCount = searchPaths.size();
    juce::String text = juce::String (filteredIndices.size()) + " FX";
    text += "  /  " + juce::String (pathCount) + (pathCount == 1 ? " PATH" : " PATHS");
    const auto scanText = getScanStatus();
    if (scanText.isNotEmpty() && scanText != "READY")
        text += "  /  " + scanText;
    statusLabel.setText (text, juce::dontSendNotification);

    repaint();
}

void PluginBrowserComponent::refreshCatalog()
{
    knownPlugins.sort (juce::KnownPluginList::sortAlphabetically, true);
    catalog = knownPlugins.getTypes();
    rebuildFilter();
}

void PluginBrowserComponent::rebuildFilter()
{
    filteredIndices.clear();

    auto tokens = juce::StringArray::fromTokens (searchBox.getText().trim().toLowerCase(), " ", "");
    tokens.removeEmptyStrings();

    for (int i = 0; i < catalog.size(); ++i)
    {
        const auto& plugin = catalog.getReference (i);
        if (plugin.name.containsIgnoreCase ("Gesture Rack")
            || plugin.isInstrument
            || plugin.numInputChannels <= 0)
            continue;

        const auto haystack = (plugin.name + " "
                             + plugin.manufacturerName + " "
                             + plugin.category + " "
                             + plugin.pluginFormatName + " "
                             + plugin.fileOrIdentifier).toLowerCase();

        bool matches = true;
        for (const auto& token : tokens)
            if (! haystack.contains (token))
            {
                matches = false;
                break;
            }

        if (matches)
            filteredIndices.push_back (i);
    }

    resultList.deselectAllRows();
    resultList.updateContent();
    resultList.repaint();
    loadButton.setEnabled (false);
}

void PluginBrowserComponent::loadSelected()
{
    const auto row = resultList.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, static_cast<int> (filteredIndices.size())))
        return;

    const auto catalogIndex = filteredIndices[static_cast<size_t> (row)];
    if (! juce::isPositiveAndBelow (catalogIndex, catalog.size()))
        return;

    if (loadCallback)
        loadCallback (targetSlot, catalog.getReference (catalogIndex));
    if (closeCallback)
        closeCallback();
}

void PluginBrowserComponent::startScan()
{
    if (scanning.exchange (true, std::memory_order_acq_rel))
        return;

    scanProgress.store (0.0f, std::memory_order_relaxed);
    setScanStatus ("STARTING");

    if (scanThread != nullptr)
    {
        if (scanThread->isThreadRunning())
        {
            scanning.store (false, std::memory_order_release);
            return;
        }
        scanThread.reset();
    }

    scanThread = std::make_unique<ScanThread> (*this);
    if (! scanThread->startThread (juce::Thread::Priority::background))
    {
        scanThread.reset();
        scanning.store (false, std::memory_order_release);
        setScanStatus ("SCAN START FAILED");
    }
}

void PluginBrowserComponent::runScan (ScanThread& thread)
{
    const auto paths = getSearchPath();
    if (paths.getNumPaths() == 0)
    {
        setScanStatus ("NO PATHS");
        scanning.store (false, std::memory_order_release);
        return;
    }

    const auto deadMansPedal = getDeadMansPedalFile();
    bool foundScannableFormat = false;

    for (auto* format : formatManager.getFormats())
    {
        if (thread.threadShouldExit())
            break;
        if (format == nullptr || ! format->canScanForPlugins())
            continue;
        if (format->getName() != "VST3")
            continue;

        foundScannableFormat = true;
        juce::PluginDirectoryScanner scanner (knownPlugins,
                                               *format,
                                               paths,
                                               true,
                                               deadMansPedal,
                                               false);

        while (! thread.threadShouldExit())
        {
            juce::String pluginBeingScanned;
            if (! scanner.scanNextFile (true, pluginBeingScanned))
                break;

            scanProgress.store (scanner.getProgress(), std::memory_order_relaxed);
            auto display = juce::File (pluginBeingScanned).getFileNameWithoutExtension();
            if (display.isEmpty())
                display = pluginBeingScanned;
            if (display.length() > 42)
                display = display.substring (0, 39) + "...";
            setScanStatus ("SCAN  " + display);
        }
    }

    knownPlugins.scanFinished();

    if (! thread.threadShouldExit())
    {
        saveCatalog();
        catalogVersion.fetch_add (1, std::memory_order_release);
        scanProgress.store (1.0f, std::memory_order_relaxed);
        setScanStatus (foundScannableFormat ? "DONE" : "VST3 FORMAT UNAVAILABLE");
    }
    else
    {
        setScanStatus ("SCAN STOPPED");
    }

    scanning.store (false, std::memory_order_release);
}

void PluginBrowserComponent::setScanStatus (juce::String text)
{
    const juce::ScopedLock lock (statusLock);
    scanStatus = std::move (text);
}

juce::String PluginBrowserComponent::getScanStatus() const
{
    const juce::ScopedLock lock (statusLock);
    return scanStatus;
}

void PluginBrowserComponent::showPathsMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "ADD FOLDER...");
    menu.addItem (2, "RESET DEFAULTS");

    if (! searchPaths.isEmpty())
    {
        juce::PopupMenu removeMenu;
        for (int i = 0; i < searchPaths.size(); ++i)
            removeMenu.addItem (1000 + i, searchPaths[i]);
        menu.addSeparator();
        menu.addSubMenu ("REMOVE PATH", removeMenu);
    }

    juce::Component::SafePointer<PluginBrowserComponent> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pathsButton),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 1)
                                safe->choosePathsToAdd();
                            else if (result == 2)
                                safe->resetDefaultPaths();
                            else if (result >= 1000)
                                safe->removePath (result - 1000);
                        });
}

void PluginBrowserComponent::choosePathsToAdd()
{
    auto start = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
    if (! searchPaths.isEmpty())
        start = juce::File (searchPaths[0]);

    pathChooser = std::make_unique<juce::FileChooser> ("Add plugin folders", start, "*");
    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectDirectories
                     | juce::FileBrowserComponent::canSelectMultipleItems;

    juce::Component::SafePointer<PluginBrowserComponent> safe (this);
    pathChooser->launchAsync (flags, [safe] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr)
            return;

        for (const auto& folder : chooser.getResults())
            if (folder.isDirectory())
                safe->searchPaths.addIfNotAlreadyThere (folder.getFullPathName());

        safe->savePaths();
        safe->setScanStatus ("PATHS UPDATED");
        safe->repaint();
    });
}

void PluginBrowserComponent::resetDefaultPaths()
{
    searchPaths.clear();

    for (auto* format : formatManager.getFormats())
    {
        if (format == nullptr || format->getName() != "VST3")
            continue;

        const auto defaults = format->getDefaultLocationsToSearch();
        for (int i = 0; i < defaults.getNumPaths(); ++i)
            searchPaths.addIfNotAlreadyThere (defaults[i].getFullPathName());
    }

    savePaths();
    setScanStatus ("DEFAULT PATHS");
    repaint();
}

void PluginBrowserComponent::removePath (int index)
{
    if (! juce::isPositiveAndBelow (index, searchPaths.size()))
        return;

    searchPaths.remove (index);
    savePaths();
    setScanStatus ("PATH REMOVED");
    repaint();
}

juce::FileSearchPath PluginBrowserComponent::getSearchPath() const
{
    juce::FileSearchPath result;
    for (const auto& path : searchPaths)
        result.addIfNotAlreadyThere (juce::File (path));
    result.removeRedundantPaths();
    return result;
}

void PluginBrowserComponent::loadPaths()
{
    searchPaths.clear();
    const auto file = getPathsFile();
    if (file.existsAsFile())
    {
        auto lines = juce::StringArray::fromLines (file.loadFileAsString());
        for (auto line : lines)
        {
            line = line.trim();
            if (line.isNotEmpty())
                searchPaths.addIfNotAlreadyThere (line);
        }
    }

    if (searchPaths.isEmpty())
        resetDefaultPaths();
}

void PluginBrowserComponent::savePaths() const
{
    const auto dir = getSettingsDirectory();
    dir.createDirectory();
    getPathsFile().replaceWithText (searchPaths.joinIntoString ("\n"));
}

void PluginBrowserComponent::loadCatalog()
{
    const auto file = getCatalogFile();
    if (! file.existsAsFile())
        return;

    if (auto xml = juce::XmlDocument::parse (file))
        knownPlugins.recreateFromXml (*xml);
}

void PluginBrowserComponent::saveCatalog()
{
    const auto dir = getSettingsDirectory();
    dir.createDirectory();
    if (auto xml = knownPlugins.createXml())
        getCatalogFile().replaceWithText (xml->toString());
}

juce::File PluginBrowserComponent::getSettingsDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("GestureRack");
}

juce::File PluginBrowserComponent::getCatalogFile() const
{
    return getSettingsDirectory().getChildFile ("plugin_catalog.xml");
}

juce::File PluginBrowserComponent::getPathsFile() const
{
    return getSettingsDirectory().getChildFile ("plugin_paths.txt");
}

juce::File PluginBrowserComponent::getDeadMansPedalFile() const
{
    return getSettingsDirectory().getChildFile ("plugin_scan_dead_mans_pedal.txt");
}

void PluginBrowserComponent::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 12.0f);
    g.setColour (kBorder);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 12.0f, 1.0f);

    g.setColour (kText);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("PLUGINS  /  SLOT " + juce::String (targetSlot + 1),
                14, 10, getWidth() - 28, 20, juce::Justification::centredLeft);

    if (scanning.load (std::memory_order_relaxed))
    {
        auto progress = juce::Rectangle<float> (14.0f, 36.0f,
                                                 static_cast<float> (getWidth() - 28), 3.0f);
        g.setColour (kRaised);
        g.fillRect (progress);
        progress.setWidth (progress.getWidth()
                           * juce::jlimit (0.0f, 1.0f, scanProgress.load (std::memory_order_relaxed)));
        g.setColour (kAccent);
        g.fillRect (progress);
    }
}

void PluginBrowserComponent::resized()
{
    auto bounds = getLocalBounds().reduced (14);
    bounds.removeFromTop (30);
    bounds.removeFromTop (6);

    auto toolbar = bounds.removeFromTop (34);
    closeButton.setBounds (toolbar.removeFromRight (70));
    toolbar.removeFromRight (6);
    loadButton.setBounds (toolbar.removeFromRight (70));
    toolbar.removeFromRight (10);
    scanButton.setBounds (toolbar.removeFromRight (82));
    toolbar.removeFromRight (6);
    pathsButton.setBounds (toolbar.removeFromRight (82));
    toolbar.removeFromRight (10);
    searchBox.setBounds (toolbar);

    bounds.removeFromTop (10);
    auto footer = bounds.removeFromBottom (24);
    statusLabel.setBounds (footer);
    bounds.removeFromBottom (6);
    resultList.setBounds (bounds);
}
