#include "PluginBrowser.h"
#include <algorithm>
#include <cmath>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
namespace ui = gr::ui;

juce::String compactPluginName (const juce::PluginDescription& description)
{
    return description.name.isNotEmpty() ? description.name
        : juce::File (description.fileOrIdentifier).getFileNameWithoutExtension();
}

float approachHover (float current, float target)
{
    const auto speed = target > current ? 0.24f : 0.16f;
    current += (target - current) * speed;
    if (std::abs (target - current) < 0.008f)
        current = target;
    return current;
}
}

class PluginBrowserComponent::ScanThread final : public juce::Thread
{
public:
    ScanThread (PluginBrowserComponent& ownerToUse, bool clearToUse)
        : juce::Thread ("Gesture Rack Safe Scan"), owner (ownerToUse), clearBlacklist (clearToUse) {}
    void run() override { owner.runScan (*this, clearBlacklist); }
private:
    PluginBrowserComponent& owner;
    bool clearBlacklist = false;
};

class PluginBrowserComponent::CategoryListModel final : public juce::ListBoxModel
{
public:
    explicit CategoryListModel (PluginBrowserComponent& ownerToUse) : owner (ownerToUse) {}

    int getNumRows() override { return gr::pluginCategoryCount; }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        owner.paintCategoryItem (row, g, width, height, selected);
    }

    void selectedRowsChanged (int row) override
    {
        owner.categorySelectionChanged (row);
    }

    juce::MouseCursor getMouseCursorForRow (int) override
    {
        return juce::MouseCursor::PointingHandCursor;
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

    addAndMakeVisible (searchBox);
    searchBox.setMultiLine (false, false);
    searchBox.setTextToShowWhenEmpty ("Search plug-in / vendor / category", ui::textMuted);
    searchBox.setFont (ui::font (11.0f));
    searchBox.setColour (juce::TextEditor::backgroundColourId, ui::control);
    searchBox.setColour (juce::TextEditor::textColourId, ui::text);
    searchBox.setColour (juce::TextEditor::highlightColourId, ui::accent.withAlpha (0.24f));
    searchBox.setColour (juce::TextEditor::highlightedTextColourId, ui::text);
    searchBox.setColour (juce::TextEditor::outlineColourId, ui::border);
    searchBox.setColour (juce::TextEditor::focusedOutlineColourId, ui::accent);
    searchBox.setColour (juce::CaretComponent::caretColourId, ui::accent);
    searchBox.onTextChange = [this] { rebuildFilter(); };
    searchBox.onReturnKey = [this] { loadSelected(); };

    for (auto* button : { &pathsButton, &scanButton, &loadButton, &closeButton })
        addAndMakeVisible (*button);
    pathsButton.onClick = [this] { showPathsMenu(); };
    scanButton.onClick = [this] { startScan (false); };
    loadButton.onClick = [this] { loadSelected(); };
    closeButton.onClick = [this] { if (closeCallback) closeCallback(); };
    loadButton.setEnabled (false);

    categoryModel = std::make_unique<CategoryListModel> (*this);
    addAndMakeVisible (categoryList);
    categoryList.setModel (categoryModel.get());
    categoryList.setRowHeight (31);
    categoryList.setColour (juce::ListBox::backgroundColourId, ui::workspace);
    categoryList.setColour (juce::ListBox::outlineColourId, ui::border.withAlpha (0.62f));
    categoryList.setOutlineThickness (1);
    categoryList.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, ui::gray.withAlpha (0.58f));
    categoryList.getVerticalScrollBar().setColour (juce::ScrollBar::backgroundColourId, ui::workspace);
    categoryList.selectRow (0, true, true);

    addAndMakeVisible (resultList);
    resultList.setRowHeight (42);
    resultList.setColour (juce::ListBox::backgroundColourId, ui::workspace);
    resultList.setColour (juce::ListBox::outlineColourId, ui::border.withAlpha (0.62f));
    resultList.setOutlineThickness (1);
    resultList.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, ui::gray.withAlpha (0.58f));
    resultList.getVerticalScrollBar().setColour (juce::ScrollBar::backgroundColourId, ui::workspace);

    addAndMakeVisible (statusLabel);
    statusLabel.setColour (juce::Label::textColourId, ui::textMuted);
    statusLabel.setFont (ui::metaFont());
    statusLabel.setJustificationType (juce::Justification::centredLeft);

    refreshCatalog();
    startTimerHz (60);
    setVisible (false);
}

PluginBrowserComponent::~PluginBrowserComponent()
{
    stopTimer();
    categoryList.setModel (nullptr);
    resultList.setModel (nullptr);
    if (scanThread != nullptr && scanThread->isThreadRunning())
    {
        scanThread->signalThreadShouldExit();
        scanThread->stopThread (5000);
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
        if (closeCallback) closeCallback();
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

juce::MouseCursor PluginBrowserComponent::getMouseCursorForRow (int)
{
    return juce::MouseCursor::PointingHandCursor;
}

void PluginBrowserComponent::paintListBoxItem (int rowNumber, juce::Graphics& g,
                                               int width, int height, bool rowIsSelected)
{
    if (! juce::isPositiveAndBelow (rowNumber, static_cast<int> (filteredIndices.size()))) return;
    const auto catalogIndex = filteredIndices[static_cast<size_t> (rowNumber)];
    if (! juce::isPositiveAndBelow (catalogIndex, catalog.size())) return;

    const auto& plugin = catalog.getReference (catalogIndex);
    const auto hover = juce::isPositiveAndBelow (rowNumber, static_cast<int> (resultHoverAmounts.size()))
                     ? resultHoverAmounts[static_cast<size_t> (rowNumber)] : 0.0f;
    auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (6, 3);

    auto fill = (rowNumber & 1) != 0 ? ui::surfaceHigh.withAlpha (0.18f) : ui::workspace;
    fill = ui::blend (fill, ui::canvas, hover * 0.48f);
    if (rowIsSelected)
        fill = ui::blend (fill, ui::accent, 0.075f);

    if (hover > 0.01f || rowIsSelected || (rowNumber & 1) != 0)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (bounds.toFloat(), 6.0f);
    }

    if (hover > 0.01f || rowIsSelected)
    {
        const auto focus = juce::jmax (hover, rowIsSelected ? 1.0f : 0.0f);
        g.setColour (ui::blend (ui::border, ui::accent, focus).withAlpha (0.88f));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    auto categoryArea = bounds.removeFromRight (130);
    auto meta = bounds.removeFromRight (juce::jmin (270, bounds.getWidth() * 42 / 100));
    bounds.removeFromRight (10);

    g.setColour (rowIsSelected ? ui::accent : ui::text);
    g.setFont (ui::font (11.5f, juce::Font::bold));
    g.drawFittedText (compactPluginName (plugin), bounds.reduced (6, 0),
                      juce::Justification::centredLeft, 1);

    g.setColour (ui::textMuted);
    g.setFont (ui::font (9.5f));
    auto metaText = plugin.manufacturerName;
    if (plugin.pluginFormatName.isNotEmpty())
        metaText += (metaText.isNotEmpty() ? "  ·  " : "") + plugin.pluginFormatName;
    g.drawFittedText (metaText, meta, juce::Justification::centredRight, 1);

    auto categoryBounds = categoryArea.reduced (8, 8).toFloat();
    g.setColour (rowIsSelected ? ui::accent.withAlpha (0.10f) : ui::control);
    g.fillRoundedRectangle (categoryBounds, 5.0f);
    g.setColour (rowIsSelected ? ui::accent.withAlpha (0.82f)
                               : ui::blend (ui::border, ui::accent, hover * 0.65f));
    g.drawRoundedRectangle (categoryBounds.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (rowIsSelected ? ui::accent : ui::textMuted);
    g.setFont (ui::font (8.8f, juce::Font::bold));
    g.drawFittedText (gr::normalizedPluginCategoryName (plugin), categoryArea.reduced (10, 7),
                      juce::Justification::centred, 1);
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

void PluginBrowserComponent::paintCategoryItem (int rowNumber, juce::Graphics& g,
                                                int width, int height, bool rowIsSelected)
{
    const auto& categories = gr::browserPluginCategories();
    if (! juce::isPositiveAndBelow (rowNumber, static_cast<int> (categories.size()))) return;

    const auto category = categories[static_cast<size_t> (rowNumber)];
    const auto hover = categoryHoverAmounts[static_cast<size_t> (rowNumber)];
    auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (5, 2);

    auto fill = ui::workspace;
    fill = ui::blend (fill, ui::canvas, hover * 0.48f);
    if (rowIsSelected)
        fill = ui::blend (fill, ui::accent, 0.075f);

    if (hover > 0.01f || rowIsSelected)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (bounds.toFloat(), 5.0f);
        const auto focus = juce::jmax (hover, rowIsSelected ? 1.0f : 0.0f);
        g.setColour (ui::blend (ui::border, ui::accent, focus).withAlpha (0.88f));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 5.0f, 1.0f);
    }

    auto countArea = bounds.removeFromRight (42);
    auto iconArea = bounds.removeFromLeft (18);

    juce::Path chevron;
    const auto cx = static_cast<float> (iconArea.getCentreX());
    const auto cy = static_cast<float> (iconArea.getCentreY());
    chevron.startNewSubPath (cx - 2.5f, cy - 4.0f);
    chevron.lineTo (cx + 2.5f, cy);
    chevron.lineTo (cx - 2.5f, cy + 4.0f);
    g.setColour (rowIsSelected ? ui::accent : ui::blend (ui::gray, ui::accent, hover));
    g.strokePath (chevron, juce::PathStrokeType (1.2f));

    g.setColour (rowIsSelected ? ui::accent : ui::text);
    g.setFont (ui::font (10.5f, rowIsSelected ? juce::Font::bold : juce::Font::plain));
    g.drawFittedText (gr::pluginCategoryName (category), bounds.reduced (3, 0),
                      juce::Justification::centredLeft, 1);

    g.setColour (rowIsSelected ? ui::accent.withAlpha (0.78f) : ui::textMuted);
    g.setFont (ui::font (9.0f, juce::Font::bold));
    g.drawText (juce::String (getCategoryCount (category)), countArea.reduced (4, 0),
                juce::Justification::centredRight);
}

void PluginBrowserComponent::categorySelectionChanged (int row)
{
    const auto& categories = gr::browserPluginCategories();
    if (! juce::isPositiveAndBelow (row, static_cast<int> (categories.size())))
        return;

    selectedCategory = categories[static_cast<size_t> (row)];
    rebuildFilter();
    categoryList.repaint();
}

int PluginBrowserComponent::getCategoryCount (gr::PluginCategory category) const
{
    const auto index = static_cast<int> (category);
    if (! juce::isPositiveAndBelow (index, gr::pluginCategoryCount))
        return 0;
    return categoryCounts[static_cast<size_t> (index)];
}

void PluginBrowserComponent::rebuildCategoryCounts()
{
    categoryCounts.fill (0);
    for (const auto& plugin : catalog)
    {
        if (! gr::pluginIsRackEffect (plugin))
            continue;

        ++categoryCounts[static_cast<size_t> (gr::PluginCategory::all)];
        const auto category = gr::classifyPlugin (plugin);
        const auto index = static_cast<int> (category);
        if (juce::isPositiveAndBelow (index, gr::pluginCategoryCount))
            ++categoryCounts[static_cast<size_t> (index)];
    }
}

int PluginBrowserComponent::getHoveredRow (const juce::ListBox& list) const
{
    if (! isShowing() || ! list.isShowing())
        return -1;

    const auto screen = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
    if (! list.getScreenBounds().contains (screen))
        return -1;

    const auto local = list.getLocalPoint (nullptr, screen);
    return list.getRowContainingPosition (local.x, local.y);
}

void PluginBrowserComponent::updateHoverAnimation()
{
    const auto hoveredCategory = getHoveredRow (categoryList);
    const auto hoveredResult = getHoveredRow (resultList);
    bool categoryChanged = false;
    bool resultChanged = false;

    for (int i = 0; i < gr::pluginCategoryCount; ++i)
    {
        const auto old = categoryHoverAmounts[static_cast<size_t> (i)];
        categoryHoverAmounts[static_cast<size_t> (i)] =
            approachHover (old, i == hoveredCategory ? 1.0f : 0.0f);
        categoryChanged = categoryChanged
                       || std::abs (old - categoryHoverAmounts[static_cast<size_t> (i)]) > 0.001f;
    }

    for (int i = 0; i < static_cast<int> (resultHoverAmounts.size()); ++i)
    {
        const auto old = resultHoverAmounts[static_cast<size_t> (i)];
        resultHoverAmounts[static_cast<size_t> (i)] =
            approachHover (old, i == hoveredResult ? 1.0f : 0.0f);
        resultChanged = resultChanged
                     || std::abs (old - resultHoverAmounts[static_cast<size_t> (i)]) > 0.001f;
    }

    const auto screen = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
    const auto searchTarget = isShowing() && searchBox.isShowing()
                           && searchBox.getScreenBounds().contains (screen) ? 1.0f : 0.0f;
    const auto oldSearch = searchHoverAmount;
    searchHoverAmount = approachHover (searchHoverAmount, searchTarget);
    if (std::abs (oldSearch - searchHoverAmount) > 0.001f)
        searchBox.setColour (juce::TextEditor::outlineColourId,
                             ui::blend (ui::border, ui::accent, searchHoverAmount));

    if (categoryChanged) categoryList.repaint();
    if (resultChanged) resultList.repaint();
}

void PluginBrowserComponent::timerCallback()
{
    updateHoverAnimation();

    const auto version = catalogVersion.load (std::memory_order_acquire);
    if (version != displayedCatalogVersion && ! scanning.load (std::memory_order_relaxed))
    {
        displayedCatalogVersion = version;
        loadCatalog();
        refreshCatalog();
    }

    const auto isScanning = scanning.load (std::memory_order_relaxed);
    scanButton.setButtonText (isScanning ? "SCANNING" : "SAFE SCAN");
    scanButton.setToggleState (isScanning, juce::dontSendNotification);
    scanButton.setEnabled (! isScanning);
    pathsButton.setEnabled (! isScanning);

    juce::String text = juce::String (filteredIndices.size()) + " FX"
                      + "  ·  " + gr::pluginCategoryName (selectedCategory)
                      + "  ·  " + juce::String (searchPaths.size()) + " PATHS";
    const auto blacklisted = getBlacklistedCount();
    if (blacklisted > 0) text += "  ·  " + juce::String (blacklisted) + " FAILED";
    const auto scanText = getScanStatus();
    if (scanText.isNotEmpty() && scanText != "READY") text += "  ·  " + scanText;
    statusLabel.setText (text, juce::dontSendNotification);
    repaint();
}

void PluginBrowserComponent::refreshCatalog()
{
    knownPlugins.sort (juce::KnownPluginList::sortAlphabetically, true);
    catalog = knownPlugins.getTypes();
    rebuildCategoryCounts();
    categoryList.updateContent();
    categoryList.repaint();
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
        if (! gr::pluginIsRackEffect (plugin))
            continue;

        const auto category = gr::classifyPlugin (plugin);
        if (selectedCategory != gr::PluginCategory::all && category != selectedCategory)
            continue;

        const auto categoryName = gr::pluginCategoryName (category);
        const auto haystack = (plugin.name + " " + plugin.manufacturerName + " "
                             + plugin.category + " " + categoryName + " "
                             + plugin.pluginFormatName + " " + plugin.fileOrIdentifier).toLowerCase();

        bool matches = true;
        for (const auto& token : tokens)
            if (! haystack.contains (token)) { matches = false; break; }

        if (matches)
            filteredIndices.push_back (i);
    }

    resultHoverAmounts.assign (filteredIndices.size(), 0.0f);
    resultList.deselectAllRows();
    resultList.updateContent();
    resultList.repaint();
    loadButton.setEnabled (false);
}

void PluginBrowserComponent::loadSelected()
{
    const auto row = resultList.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, static_cast<int> (filteredIndices.size()))) return;
    const auto catalogIndex = filteredIndices[static_cast<size_t> (row)];
    if (! juce::isPositiveAndBelow (catalogIndex, catalog.size())) return;
    if (loadCallback) loadCallback (targetSlot, catalog.getReference (catalogIndex));
    if (closeCallback) closeCallback();
}

void PluginBrowserComponent::startScan (bool clearBlacklist)
{
    if (scanning.exchange (true, std::memory_order_acq_rel)) return;
    scanProgress.store (0.0f, std::memory_order_relaxed);
    setScanStatus (clearBlacklist ? "RETRY FAILED" : "STARTING SAFE SCAN");
    if (scanThread != nullptr)
    {
        if (scanThread->isThreadRunning())
        {
            scanning.store (false, std::memory_order_release);
            return;
        }
        scanThread.reset();
    }
    scanThread = std::make_unique<ScanThread> (*this, clearBlacklist);
    if (! scanThread->startThread (juce::Thread::Priority::background))
    {
        scanThread.reset();
        scanning.store (false, std::memory_order_release);
        setScanStatus ("SCAN START FAILED");
    }
}

juce::File PluginBrowserComponent::findScannerExecutable() const
{
   #if JUCE_WINDOWS
    static int moduleAddressAnchor = 0;
    HMODULE module = nullptr;
    if (::GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR> (&moduleAddressAnchor),
                              &module) != FALSE)
    {
        wchar_t modulePath[32768] {};
        constexpr auto modulePathCapacity = static_cast<DWORD> (sizeof (modulePath) / sizeof (modulePath[0]));
        const auto chars = ::GetModuleFileNameW (module, modulePath, modulePathCapacity);
        if (chars > 0 && chars < modulePathCapacity)
        {
            const auto moduleFile = juce::File (juce::String (modulePath));
            const auto candidate = moduleFile.getSiblingFile ("GestureRackScanner.exe");
            if (candidate.existsAsFile()) return candidate;
        }
    }
   #endif

    const auto current = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #if JUCE_WINDOWS
    const juce::String scannerName { "GestureRackScanner.exe" };
   #else
    const juce::String scannerName { "GestureRackScanner" };
   #endif
    auto candidate = current.getSiblingFile (scannerName);
    if (candidate.existsAsFile()) return candidate;
    candidate = current.getParentDirectory().getChildFile (scannerName);
    if (candidate.existsAsFile()) return candidate;
    return {};
}

void PluginBrowserComponent::runScan (ScanThread& thread, bool clearBlacklist)
{
    const auto helper = findScannerExecutable();
    if (! helper.existsAsFile())
    {
        setScanStatus ("SCANNER HELPER MISSING");
        scanning.store (false, std::memory_order_release);
        return;
    }
    if (getSearchPath().getNumPaths() == 0)
    {
        setScanStatus ("NO PATHS");
        scanning.store (false, std::memory_order_release);
        return;
    }

    getSettingsDirectory().createDirectory();
    getScannerStatusFile().deleteFile();
    constexpr int maxCrashRecoveries = 64;
    constexpr int hangTimeoutMs = 60000;
    int recovery = 0;
    bool clearOnNextRun = clearBlacklist;

    while (! thread.threadShouldExit() && recovery <= maxCrashRecoveries)
    {
        juce::StringArray args;
        args.add (helper.getFullPathName());
        args.add ("--paths"); args.add (getPathsFile().getFullPathName());
        args.add ("--catalog"); args.add (getCatalogFile().getFullPathName());
        args.add ("--deadman"); args.add (getDeadMansPedalFile().getFullPathName());
        args.add ("--status"); args.add (getScannerStatusFile().getFullPathName());
        if (clearOnNextRun)
        {
            args.add ("--clear-blacklist");
            clearOnNextRun = false;
        }

        juce::ChildProcess process;
        if (! process.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        {
            setScanStatus ("SCANNER LAUNCH FAILED");
            break;
        }

        auto lastHeartbeat = juce::Time::currentTimeMillis();
        auto lastModified = int64_t { 0 };
        bool killedForHang = false;
        while (process.isRunning() && ! thread.threadShouldExit())
        {
            const auto statusFile = getScannerStatusFile();
            if (statusFile.existsAsFile())
            {
                const auto modified = statusFile.getLastModificationTime().toMilliseconds();
                if (modified != lastModified)
                {
                    lastModified = modified;
                    lastHeartbeat = juce::Time::currentTimeMillis();
                    const auto line = statusFile.loadFileAsString().trim();
                    auto fields = juce::StringArray::fromTokens (line, "\t", "");
                    if (fields.size() >= 3 && fields[1] == "SCAN")
                    {
                        if (fields.size() >= 4)
                            scanProgress.store (static_cast<float> (fields[2].getDoubleValue()), std::memory_order_relaxed);
                        auto name = juce::File (fields[fields.size() - 1]).getFileNameWithoutExtension();
                        if (name.length() > 38) name = name.substring (0, 35) + "...";
                        setScanStatus ("SAFE  " + name);
                    }
                }
            }
            if (juce::Time::currentTimeMillis() - lastHeartbeat > hangTimeoutMs)
            {
                process.kill();
                killedForHang = true;
                setScanStatus ("HUNG PLUGIN ISOLATED");
                break;
            }
            juce::Thread::sleep (100);
        }

        if (thread.threadShouldExit())
        {
            if (process.isRunning()) process.kill();
            setScanStatus ("SCAN STOPPED");
            break;
        }

        process.waitForProcessToFinish (2000);
        const auto exitCode = process.getExitCode();
        if (! killedForHang && exitCode == 0)
        {
            scanProgress.store (1.0f, std::memory_order_relaxed);
            setScanStatus ("DONE");
            catalogVersion.fetch_add (1, std::memory_order_release);
            break;
        }

        ++recovery;
        catalogVersion.fetch_add (1, std::memory_order_release);
        if (recovery > maxCrashRecoveries)
        {
            setScanStatus ("TOO MANY FAILED PLUGINS");
            break;
        }
        setScanStatus ("RECOVERING  " + juce::String (recovery));
        juce::Thread::sleep (150);
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

int PluginBrowserComponent::getBlacklistedCount() const
{
    return knownPlugins.getBlacklistedFiles().size();
}

void PluginBrowserComponent::showPathsMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "ADD FOLDER...");
    menu.addItem (2, "RESET DEFAULTS");
    menu.addItem (3, "RETRY FAILED PLUGINS", getBlacklistedCount() > 0);
    if (! searchPaths.isEmpty())
    {
        juce::PopupMenu removeMenu;
        for (int i = 0; i < searchPaths.size(); ++i) removeMenu.addItem (1000 + i, searchPaths[i]);
        menu.addSeparator();
        menu.addSubMenu ("REMOVE PATH", removeMenu);
    }

    juce::Component::SafePointer<PluginBrowserComponent> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pathsButton),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result == 0) return;
                            if (result == 1) safe->choosePathsToAdd();
                            else if (result == 2) safe->resetDefaultPaths();
                            else if (result == 3) safe->startScan (true);
                            else if (result >= 1000) safe->removePath (result - 1000);
                        });
}

void PluginBrowserComponent::choosePathsToAdd()
{
    auto start = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
    if (! searchPaths.isEmpty()) start = juce::File (searchPaths[0]);
    pathChooser = std::make_unique<juce::FileChooser> ("Add plugin folders", start, "*");
    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectDirectories
                            | juce::FileBrowserComponent::canSelectMultipleItems;
    juce::Component::SafePointer<PluginBrowserComponent> safe (this);
    pathChooser->launchAsync (chooserFlags, [safe] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr) return;
        for (const auto& folder : chooser.getResults())
            if (folder.isDirectory()) safe->searchPaths.addIfNotAlreadyThere (folder.getFullPathName());
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
        if (format == nullptr || format->getName() != "VST3") continue;
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
    if (! juce::isPositiveAndBelow (index, searchPaths.size())) return;
    searchPaths.remove (index);
    savePaths();
    setScanStatus ("PATH REMOVED");
    repaint();
}

juce::FileSearchPath PluginBrowserComponent::getSearchPath() const
{
    juce::FileSearchPath result;
    for (const auto& path : searchPaths) result.addIfNotAlreadyThere (juce::File (path));
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
            if (line.isNotEmpty()) searchPaths.addIfNotAlreadyThere (line);
        }
    }
    if (searchPaths.isEmpty()) resetDefaultPaths();
}

void PluginBrowserComponent::savePaths() const
{
    getSettingsDirectory().createDirectory();
    getPathsFile().replaceWithText (searchPaths.joinIntoString ("\n"));
}

void PluginBrowserComponent::loadCatalog()
{
    knownPlugins.clear();
    const auto file = getCatalogFile();
    if (! file.existsAsFile()) return;
    if (auto xml = juce::XmlDocument::parse (file)) knownPlugins.recreateFromXml (*xml);
}

juce::File PluginBrowserComponent::getSettingsDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GestureRack");
}
juce::File PluginBrowserComponent::getCatalogFile() const { return getSettingsDirectory().getChildFile ("plugin_catalog.xml"); }
juce::File PluginBrowserComponent::getPathsFile() const { return getSettingsDirectory().getChildFile ("plugin_paths.txt"); }
juce::File PluginBrowserComponent::getDeadMansPedalFile() const { return getSettingsDirectory().getChildFile ("plugin_scan_dead_mans_pedal.txt"); }
juce::File PluginBrowserComponent::getScannerStatusFile() const { return getSettingsDirectory().getChildFile ("plugin_scan_status.txt"); }

void PluginBrowserComponent::paint (juce::Graphics& g)
{
    g.fillAll (ui::canvas);
    ui::drawPanel (g, getLocalBounds().toFloat(), true);

    ui::drawSectionTitle (g, "PLUGINS", { 16, 12, 110, 22 });
    g.setColour (ui::textMuted);
    g.setFont (ui::font (9.0f, juce::Font::bold));
    g.drawText ("SLOT 0" + juce::String (targetSlot + 1) + "  ·  ISOLATED SCANNER  ·  AUTO CATEGORY",
                126, 12, getWidth() - 144, 22, juce::Justification::centredLeft);

    if (categoryList.getWidth() > 0)
        ui::drawSectionTitle (g, "CATEGORIES",
                              { categoryList.getX(), categoryList.getY() - 22, categoryList.getWidth(), 18 });
    if (resultList.getWidth() > 0)
        ui::drawSectionTitle (g, "RESULTS",
                              { resultList.getX(), resultList.getY() - 22, resultList.getWidth(), 18 });

    if (scanning.load (std::memory_order_relaxed))
    {
        auto progress = juce::Rectangle<float> (16.0f, 42.0f, static_cast<float> (getWidth() - 32), 2.0f);
        g.setColour (ui::control);
        g.fillRect (progress);
        progress.setWidth (progress.getWidth() * juce::jlimit (0.0f, 1.0f, scanProgress.load (std::memory_order_relaxed)));
        g.setColour (ui::accent);
        g.fillRect (progress);
    }
}

void PluginBrowserComponent::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    bounds.removeFromTop (40);

    auto toolbar = bounds.removeFromTop (34);
    closeButton.setBounds (toolbar.removeFromRight (72)); toolbar.removeFromRight (6);
    loadButton.setBounds (toolbar.removeFromRight (72)); toolbar.removeFromRight (10);
    scanButton.setBounds (toolbar.removeFromRight (100)); toolbar.removeFromRight (6);
    pathsButton.setBounds (toolbar.removeFromRight (78)); toolbar.removeFromRight (10);
    searchBox.setBounds (toolbar);

    bounds.removeFromTop (12);
    auto footer = bounds.removeFromBottom (22);
    statusLabel.setBounds (footer);
    bounds.removeFromBottom (6);

    const auto categoryWidth = juce::jlimit (190, 238, getWidth() / 5);
    auto categoryArea = bounds.removeFromLeft (categoryWidth);
    categoryArea.removeFromTop (22);
    categoryList.setBounds (categoryArea);

    bounds.removeFromLeft (10);
    bounds.removeFromTop (22);
    resultList.setBounds (bounds);
}
